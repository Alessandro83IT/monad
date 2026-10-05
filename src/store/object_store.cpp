#include "object_store.hpp"
#include "object_store_detail.hpp"
#include "../artifact/scanner.hpp"
#include "../artifact/identity.hpp"
#include <cerrno>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <utility>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace athena::store {
namespace {
namespace fs = std::filesystem;
using Code = ObjectErrorCode;
class Fd {
public:
    explicit Fd(int value) : value_(value) {}
    ~Fd() { if (value_ >= 0) ::close(value_); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept : value_(std::exchange(other.value_, -1)) {}
    Fd& operator=(Fd&& other) noexcept {
        if (value_ >= 0) ::close(value_);
        value_ = std::exchange(other.value_, -1); return *this;
    }
    int get() const { return value_; }
private:
    int value_;
};
[[noreturn]] void fail(Code code, const char* operation) {
    const int error = errno;
    throw ObjectError(code, std::string(operation) + ": " + std::strerror(error));
}
std::string digest(const core::ArtifactId& id) {
    const auto& text = id.value();
    if (text.size() != 71 || !text.starts_with("sha256:"))
        throw ObjectError(Code::MalformedArtifactId, "expected sha256: and 64 lowercase hexadecimal digits");
    for (std::size_t i = 7; i < text.size(); ++i)
        if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f')))
            throw ObjectError(Code::MalformedArtifactId, "invalid lowercase SHA-256 ArtifactId");
    return text.substr(7);
}
Fd open_directory(const fs::path& path, Code code) {
    Fd current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (current.get() < 0) fail(code, "open filesystem root");
    for (const auto& component : path) {
        const auto name = component.native();
        if (name == "/" || name == "." || name.empty()) continue;
        Fd next(::openat(current.get(), name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (next.get() < 0) fail(code, "open real directory component");
        current = std::move(next);
    }
    return current;
}
Fd namespace_directory(const fs::path& root, bool create) {
    auto current = open_directory(root, Code::StoreDirectory);
    for (const char* name : {"objects", "v1", "sha256"}) {
        bool created = false;
        if (create) {
            created = ::mkdirat(current.get(), name, 0755) == 0;
            if (!created && errno != EEXIST) fail(Code::StoreDirectory, "create object namespace");
        }
        Fd next(::openat(current.get(), name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (next.get() < 0) fail(Code::StoreDirectory, "open object namespace");
        if (created && ::fchmod(next.get(), 0755) != 0) fail(Code::StoreDirectory, "set new namespace permissions");
        current = std::move(next);
    }
    return current;
}
// Pin every parent and refuse links even after model validation.
Fd parent(int root, const std::string& path) {
    artifact::validate_path(path);
    Fd current(::dup(root));
    if (current.get() < 0) fail(Code::Materialization, "duplicate private root");
    std::size_t start = 0;
    for (auto end = path.find('/'); end != path.npos; end = path.find('/', start)) {
        const auto name = path.substr(start, end - start);
        Fd next(::openat(current.get(), name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (next.get() < 0) fail(Code::Materialization, "open private entry parent");
        current = std::move(next); start = end + 1;
    }
    return current;
}
std::string leaf(const std::string& path) { return path.substr(path.rfind('/') == path.npos ? 0 : path.rfind('/') + 1); }
artifact::NormalizedArtifact check_identity(const fs::path& path, const core::ArtifactId& expected, Code inspection, Code mismatch) {
    try {
        auto model = artifact::scan(path);
        if (artifact::derive_artifact_id(model) != expected)
            throw ObjectError(mismatch, "object content differs from expected ArtifactId: " + path.string());
        return model;
    } catch (const artifact::Error& error) {
        throw ObjectError(inspection, error.what());
    }
}
// Only our own private container. Never called on source or final paths.
std::string cleanup(const fs::path& container) noexcept {
    try {
        const auto payload = container / "payload";
        std::error_code error;
        if (fs::is_directory(fs::symlink_status(payload, error))) {
            if (::chmod(payload.c_str(), 0700) != 0) fail(Code::Cleanup, "restore private root for cleanup");
            for (const auto& entry : fs::recursive_directory_iterator(payload))
                if (fs::is_directory(entry.symlink_status()) && ::chmod(entry.path().c_str(), 0700) != 0)
                    fail(Code::Cleanup, "restore private directory for cleanup");
        }
        fs::remove_all(container);
        return {};
    } catch (const std::exception& error) { return error.what(); }
}
}

namespace object_detail {
std::string cleanup_private(const fs::path& container) noexcept { return cleanup(container); }
void materialize(const fs::path& root, const artifact::NormalizedArtifact& model) {
    artifact::validate(model);
    auto descriptor = open_directory(root, Code::Materialization);
    for (const auto& entry : model.entries) {
        auto directory = parent(descriptor.get(), entry.path);
        const auto name = leaf(entry.path);
        if (std::holds_alternative<artifact::Directory>(entry.value)) {
            if (::mkdirat(directory.get(), name.c_str(), 0700) != 0) fail(Code::Materialization, "mkdir payload");
            Fd node(::openat(directory.get(), name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
            if (node.get() < 0 || ::fchmod(node.get(), 0755) != 0) fail(Code::Materialization, "normalize directory mode");
        } else if (const auto* file = std::get_if<artifact::RegularFile>(&entry.value)) {
            Fd node(::openat(directory.get(), name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
            if (node.get() < 0) fail(Code::Materialization, "create payload file");
            std::size_t offset = 0;
            while (offset < file->content.size()) {
                const auto size = ::write(node.get(), file->content.data() + offset,
                    std::min<std::size_t>(file->content.size() - offset, 65536));
                if (size < 0 && errno == EINTR) continue;
                if (size <= 0) fail(Code::Materialization, "write payload file");
                offset += static_cast<std::size_t>(size);
            }
            if (::fchmod(node.get(), file->executable ? 0755 : 0644) != 0) fail(Code::Materialization, "normalize file mode");
        } else {
            const auto& target = std::get<artifact::SymbolicLink>(entry.value).target;
            if (::symlinkat(target.c_str(), directory.get(), name.c_str()) != 0) fail(Code::Materialization, "create literal symlink");
        }
    }
    if (::fchmod(descriptor.get(), 0755) != 0) fail(Code::Materialization, "normalize root mode");
}
void finalize(const fs::path& root, const artifact::NormalizedArtifact& model) {
    artifact::validate(model);
    auto descriptor = open_directory(root, Code::Materialization);
    for (auto it = model.entries.rbegin(); it != model.entries.rend(); ++it) {
        if (std::holds_alternative<artifact::SymbolicLink>(it->value)) continue;
        auto directory = parent(descriptor.get(), it->path);
        const auto name = leaf(it->path);
        const bool is_directory = std::holds_alternative<artifact::Directory>(it->value);
        Fd node(::openat(directory.get(), name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC |
                        (is_directory ? O_DIRECTORY : 0)));
        if (node.get() < 0) fail(Code::Materialization, "open finalization node");
        const auto* file = std::get_if<artifact::RegularFile>(&it->value);
        if (::fchmod(node.get(), is_directory || file->executable ? 0555 : 0444) != 0)
            fail(Code::Materialization, "remove payload write permissions");
    }
    if (::fchmod(descriptor.get(), 0555) != 0) fail(Code::Materialization, "finalize root");
}
bool rename_no_replace(int source_parent, const char* source, int destination_parent, const char* destination) {
    if (::syscall(SYS_renameat2, source_parent, source, destination_parent, destination, RENAME_NOREPLACE) == 0) return true;
    if (errno == EEXIST) return false;
    fail(Code::Publication, "renameat2 RENAME_NOREPLACE (no fallback)");
}
}

ObjectStore::ObjectStore(fs::path root) : root_(std::move(root)) {
    if (!root_.is_absolute() || root_.native().find('\0') != std::string::npos)
        throw ObjectError(Code::StoreDirectory, "store root must be an absolute NUL-free locator");
    for (const auto& part : root_)
        if (part == "..") throw ObjectError(Code::StoreDirectory, "store root must not contain parent traversal");
}
fs::path ObjectStore::object_path(const core::ArtifactId& id) const {
    return root_ / "objects/v1/sha256" / digest(id);
}
void ObjectStore::verify(const core::ArtifactId& expected) const {
    const auto path = object_path(expected);
    auto directory = namespace_directory(root_, false);
    check_identity(path, expected, Code::ExistingObjectVerification, Code::CollisionCorruption);
}
fs::path ObjectStore::publish(const fs::path& source, const core::ArtifactId& expected) const {
    const auto destination = object_path(expected); // validate before filesystem effects
    if (source.empty() || source.native().find('\0') != std::string::npos)
        throw ObjectError(Code::InvalidSource, "source locator must be nonempty and NUL-free");
    std::error_code locator_error;
    const auto source_locator = fs::absolute(source, locator_error).lexically_normal();
    if (locator_error) throw ObjectError(Code::InvalidSource, "source locator: " + locator_error.message());
    const auto relative_namespace = destination.parent_path().lexically_normal().lexically_relative(source_locator);
    if (!relative_namespace.empty() && *relative_namespace.begin() != "..")
        throw ObjectError(Code::InvalidSource, "publication namespace must not be inside source staging");
    artifact::NormalizedArtifact model;
    try {
        model = artifact::scan(source);
        if (artifact::derive_artifact_id(model) != expected)
            throw ObjectError(Code::SourceIdentityMismatch, "source does not match expected ArtifactId");
    } catch (const artifact::Error& error) { throw ObjectError(Code::InvalidSource, error.what()); }
    auto directory = namespace_directory(root_, true);
    // Container is immediately beneath the destination directory: same fs,
    // including when objects/v1/sha256 is a mount point. Payload is private
    // even after its root is finalized to world-traversable permissions.
    std::string pattern = (destination.parent_path() / ".publish-XXXXXX").native();
    if (!::mkdtemp(pattern.data())) fail(Code::PrivateStaging, "mkdtemp publication container");
    const fs::path container = pattern;
    try {
        auto private_directory = open_directory(container, Code::PrivateStaging);
        if (::fchmod(private_directory.get(), 0700) != 0 || ::mkdirat(private_directory.get(), "payload", 0700) != 0)
            fail(Code::PrivateStaging, "create private payload");
        const auto payload = container / "payload";
        object_detail::materialize(payload, model);
        object_detail::finalize(payload, model);
        if (check_identity(payload, expected, Code::Materialization, Code::Materialization) != model)
            throw ObjectError(Code::Materialization, "materialized model differs from verified source");
        const bool won = object_detail::rename_no_replace(private_directory.get(), "payload", directory.get(), digest(expected).c_str());
        const auto final_model = check_identity(destination, expected,
            won ? Code::PostPublicationIntegrity : Code::ExistingObjectVerification,
            won ? Code::PostPublicationIntegrity : Code::CollisionCorruption);
        if (final_model != model)
            throw ObjectError(won ? Code::PostPublicationIntegrity : Code::CollisionCorruption,
                              "destination model differs from verified source under the same digest");
    } catch (ObjectError& error) {
        error.record_cleanup_failure(object_detail::cleanup_private(container));
        throw;
    } catch (...) {
        const auto primary = std::current_exception();
        const auto secondary = object_detail::cleanup_private(container);
        if (!secondary.empty()) {
            try { std::rethrow_exception(primary); }
            catch (...) { std::throw_with_nested(ObjectError(Code::Cleanup, secondary)); }
        }
        std::rethrow_exception(primary);
    }
    const auto secondary = object_detail::cleanup_private(container);
    if (!secondary.empty()) throw ObjectError(Code::Cleanup, secondary);
    return destination;
}
}
