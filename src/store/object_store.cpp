// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "object_store.hpp"
#include "object_store_detail.hpp"
#include "transaction_pins.hpp"
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
#include <linux/openat2.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <dirent.h>
#include <memory>

namespace athena::store {
namespace {
namespace fs = std::filesystem;
namespace durable = core::detail;
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
    int release() noexcept { return std::exchange(value_, -1); }
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
Fd namespace_directory(const fs::path& root, bool create, bool allow_missing = false, const durable::Syscalls& ops = durable::linux_syscalls()) {
    auto current = open_directory(root, Code::StoreDirectory);
    if (create) durable::require_durable_directory(current.get(), ops);
    for (const char* name : {"objects", "v1", "sha256"}) {
        bool created = false;
        if (create) {
            created = ::mkdirat(current.get(), name, 0755) == 0;
            if (!created && errno != EEXIST) fail(Code::StoreDirectory, "create object namespace");
        }
        Fd next(::openat(current.get(), name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (next.get() < 0) {
            if (allow_missing && errno == ENOENT) return Fd(-1);
            fail(Code::StoreDirectory, "open object namespace");
        }
        if (created && ::fchmod(next.get(), 0755) != 0) fail(Code::StoreDirectory, "set new namespace permissions");
        if (create) {
            durable::require_durable_directory(next.get(), ops);
            durable::directory_barrier(next.get(), false, ops);
            durable::directory_barrier(current.get(), false, ops);
        }
        current = std::move(next);
    }
    return current;
}
// Pin every parent and refuse links even after model validation.
Fd parent(int root, const std::string& path) {
    artifact::validate_path(path);
    Fd current(::fcntl(root, F_DUPFD_CLOEXEC, 0));
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
void require_same_gc_mount(const struct statx& expected, const struct statx& actual) {
    if (!(expected.stx_mask & STATX_MNT_ID) || !(actual.stx_mask & STATX_MNT_ID))
        throw ObjectError(Code::Cleanup, "GC mount identity unavailable (no fallback)");
    if (expected.stx_mnt_id != actual.stx_mnt_id ||
        expected.stx_dev_major != actual.stx_dev_major || expected.stx_dev_minor != actual.stx_dev_minor)
        throw ObjectError(Code::Cleanup, "GC refuses to cross a mount boundary");
}
std::string cleanup_private(const fs::path& container) noexcept { return cleanup(container); }
durable::Fd publication_namespace(const fs::path& root, const durable::Syscalls& ops) {
    auto directory = namespace_directory(root, true, false, ops);
    return durable::Fd::adopt(directory.release(), ops);
}
void materialize(const fs::path& root, const artifact::NormalizedArtifact& model, const durable::Syscalls& ops) {
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
            std::string_view remaining(file->content);
            while (!remaining.empty()) {
                const auto size = std::min<std::size_t>(remaining.size(), 65536);
                durable::complete_write(node.get(), remaining.substr(0, size), ops);
                remaining.remove_prefix(size);
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
void synchronize(const fs::path& root, const artifact::NormalizedArtifact& model,
                 const durable::Syscalls& ops) {
    artifact::validate(model);
    auto descriptor = open_directory(root, Code::Materialization);
    // Reverse canonical order visits every descendant before its directory.
    for (auto it = model.entries.rbegin(); it != model.entries.rend(); ++it) {
        if (std::holds_alternative<artifact::SymbolicLink>(it->value)) continue;
        auto directory = parent(descriptor.get(), it->path);
        const bool dir = std::holds_alternative<artifact::Directory>(it->value);
        auto node = durable::open_at(directory.get(), leaf(it->path), O_RDONLY | (dir ? O_DIRECTORY : 0));
        if (dir) durable::directory_barrier(node.get(), false, ops);
        else durable::file_barrier(node.get(), ops);
    }
    durable::directory_barrier(descriptor.get(), false, ops);
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
    root_ = administrative::locator(root_);
}
fs::path ObjectStore::object_path(const core::ArtifactId& id) const {
    return root_ / "objects/v1/sha256" / digest(id);
}

namespace {
struct CloseDirectory { void operator()(DIR* stream) const { ::closedir(stream); } };
using DirectoryStream = std::unique_ptr<DIR, CloseDirectory>;
DirectoryStream directory_stream(int fd) {
    const int copy = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (copy < 0) fail(Code::StoreDirectory, "duplicate directory");
    DIR* stream = ::fdopendir(copy);
    if (!stream) {
        const int saved = errno;
        ::close(copy);
        errno = saved;
        fail(Code::StoreDirectory, "open directory stream");
    }
    return DirectoryStream(stream);
}
// All traversal is relative to pinned directories. Links are unlinked as
// payload entries, never traversed. Finalized objects have read-only dirs.
struct statx gc_identity(int fd, const char* name, int flags) {
    struct statx identity {};
    if (::syscall(SYS_statx, fd, name, flags, STATX_BASIC_STATS | STATX_MNT_ID, &identity) != 0)
        fail(Code::Cleanup, "statx GC identity (no fallback)");
    if ((identity.stx_mask & (STATX_MNT_ID | STATX_INO | STATX_TYPE | STATX_MODE)) !=
        (STATX_MNT_ID | STATX_INO | STATX_TYPE | STATX_MODE))
        throw ObjectError(Code::Cleanup, "GC directory identity unavailable (no fallback)");
    return identity;
}
void remove_directory(int parent_fd, const std::string& name, const struct statx& expected_mount, const durable::Syscalls& ops) {
    // NO_XDEV rejects bind mounts too, before opening/traversing their root.
    // Require Linux openat2; an unsupported kernel fails closed.
    struct open_how how {};
    how.flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
    how.resolve = RESOLVE_NO_XDEV | RESOLVE_NO_SYMLINKS;
    Fd node(::syscall(SYS_openat2, parent_fd, name.c_str(), &how, sizeof(how)));
    if (node.get() < 0) fail(Code::Cleanup, "open object directory for removal");
    const auto pinned = gc_identity(node.get(), "", AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
    object_detail::require_same_gc_mount(expected_mount, pinned);
    if ((ops.chmod ? ops.chmod : ::fchmod)(node.get(), (pinned.stx_mode & 0777) | 0700) != 0)
        fail(Code::Cleanup, "restore directory write permissions");
    auto stream = directory_stream(node.get());
    for (;;) {
        errno = 0;
        auto* entry = ::readdir(stream.get());
        if (!entry) {
            if (errno) fail(Code::Cleanup, "read object directory");
            break;
        }
        const std::string child(entry->d_name);
        if (child == "." || child == "..") continue;
        struct stat status {};
        if (::fstatat(node.get(), child.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0)
            fail(Code::Cleanup, "inspect object child");
        if (S_ISDIR(status.st_mode)) remove_directory(node.get(), child, expected_mount, ops);
        else if ((ops.unlink ? ops.unlink : ::unlinkat)(node.get(), child.c_str(), 0) != 0)
            fail(Code::Cleanup, "unlink object child");
    }
    durable::directory_barrier(node.get(), true, ops);
    const auto current = gc_identity(parent_fd, name.c_str(), AT_SYMLINK_NOFOLLOW);
    object_detail::require_same_gc_mount(expected_mount, current);
    if (!S_ISDIR(current.stx_mode) || current.stx_dev_major != pinned.stx_dev_major ||
        current.stx_dev_minor != pinned.stx_dev_minor || current.stx_ino != pinned.stx_ino)
        throw ObjectError(Code::Cleanup, "object directory replaced during removal");
    if ((ops.unlink ? ops.unlink : ::unlinkat)(parent_fd, name.c_str(), AT_REMOVEDIR) != 0)
        fail(Code::Cleanup, "unlink object directory");
}
}

void ObjectStore::reclaim_private_trash_payload(int parent, std::string_view name, const durable::Syscalls& ops) {
    durable::leaf(name);
    const auto mount = gc_identity(parent, "", AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
    remove_directory(parent, std::string(name), mount, ops);
}

std::vector<core::ArtifactId> ObjectStore::list() const {
    auto directory = namespace_directory(root_, false, true);
    if (directory.get() < 0) return {};
    auto stream = directory_stream(directory.get());
    std::vector<core::ArtifactId> result;
    for (;;) {
        errno = 0;
        auto* entry = ::readdir(stream.get());
        if (!entry) {
            if (errno) fail(Code::StoreDirectory, "read object namespace");
            break;
        }
        const std::string name(entry->d_name);
        if (name.size() != 64 ||
            !std::all_of(name.begin(), name.end(), [](unsigned char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            })) continue;
        struct stat status {};
        if (::fstatat(directory.get(), name.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0)
            fail(Code::StoreDirectory, "inspect object namespace entry");
        if (S_ISDIR(status.st_mode)) result.emplace_back("sha256:" + name);
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.value() < right.value();
    });
    return result;
}

void ObjectStore::verify(const core::ArtifactId& expected) const {
    const auto path = object_path(expected);
    auto directory = namespace_directory(root_, false);
    check_identity(path, expected, Code::ExistingObjectVerification, Code::CollisionCorruption);
}
struct PreparedObject::Impl {
    core::ArtifactId id;
    fs::path store, destination, container;
    artifact::NormalizedArtifact model;
    durable::Fd namespace_fd, private_fd;
    std::string private_name;
    durable::Syscalls syscalls = durable::linux_syscalls();
    Impl(core::ArtifactId value) : id(std::move(value)) {}
    ~Impl() { if (namespace_fd.get() >= 0 && !private_name.empty())
        durable::cleanup_private_tree(namespace_fd.get(), private_name, syscalls); }
};
PreparedObject::PreparedObject(std::unique_ptr<Impl> value) : impl_(std::move(value)) {}
PreparedObject::~PreparedObject() = default;
PreparedObject::PreparedObject(PreparedObject&&) noexcept = default;
PreparedObject& PreparedObject::operator=(PreparedObject&&) noexcept = default;
const core::ArtifactId& PreparedObject::artifact_id() const { return impl_->id; }
fs::path ObjectStore::publish_unprotected_for_test(const fs::path& source, const core::ArtifactId& expected, const durable::Syscalls& ops) const {
    return publish_durable_unprotected_for_test(source, expected, ops).path;
}
ObjectPublication ObjectStore::publish_durable_unprotected_for_test(const fs::path& source, const core::ArtifactId& expected, const durable::Syscalls& ops) const {
    auto prepared = prepare(source, expected, ops);
    return finish_prepared(prepared, ops);
}
PreparedObject ObjectStore::prepare(const fs::path& source, const core::ArtifactId& expected, const durable::Syscalls& ops) const {
    const auto destination = object_path(expected);
    if (source.empty() || source.native().find('\0') != std::string::npos)
        throw ObjectError(Code::InvalidSource, "source locator must be nonempty and NUL-free");
    std::error_code locator_error;
    const auto source_locator = fs::absolute(source, locator_error).lexically_normal();
    if (locator_error) throw ObjectError(Code::InvalidSource, "source locator: " + locator_error.message());
    const auto relative_namespace = destination.parent_path().lexically_normal().lexically_relative(source_locator);
    if (!relative_namespace.empty() && *relative_namespace.begin() != "..")
        throw ObjectError(Code::InvalidSource, "publication namespace must not be inside source staging");
    auto value = std::make_unique<PreparedObject::Impl>(expected);
    value->syscalls = ops;
    try {
        value->model = artifact::scan(source);
        if (artifact::derive_artifact_id(value->model) != expected)
            throw ObjectError(Code::SourceIdentityMismatch, "source does not match expected ArtifactId");
    } catch (const artifact::Error& error) { throw ObjectError(Code::InvalidSource, error.what()); }
    value->store = root_; value->destination = destination;
    value->namespace_fd = object_detail::publication_namespace(root_, ops);
    value->private_name = durable::private_directory(value->namespace_fd.get(), ops);
    value->container = destination.parent_path() / value->private_name;
    try {
        value->private_fd = durable::open_at(value->namespace_fd.get(), value->private_name, O_RDONLY | O_DIRECTORY);
        if (::mkdirat(value->private_fd.get(), "payload", 0700) != 0) fail(Code::PrivateStaging, "create private payload");
        const auto payload = value->container / "payload";
        object_detail::materialize(payload, value->model, ops);
        object_detail::finalize(payload, value->model);
        object_detail::synchronize(payload, value->model, ops);
        durable::directory_barrier(value->private_fd.get(), false, ops);
        if (check_identity(payload, expected, Code::Materialization, Code::Materialization) != value->model)
            throw ObjectError(Code::Materialization, "materialized model differs from verified source");
    } catch (durable::PersistenceError& error) {
        error.cleanup_failure = durable::cleanup_private_tree(value->namespace_fd.get(), value->private_name, ops); throw;
    } catch (ObjectError& error) {
        error.record_cleanup_failure(durable::cleanup_private_tree(value->namespace_fd.get(), value->private_name, ops)); throw;
    }
    return PreparedObject(std::move(value));
}
ObjectPublication ObjectStore::publish_prepared_locked(PreparedObject& prepared, const TransactionPins& transaction,
                                                      const durable::Syscalls& ops) const {
    if (transaction.authority().store() != root_ || !transaction.artifacts().contains(prepared.artifact_id().value()))
        throw std::runtime_error("publication requires durable transaction pin in this store");
    transaction.authority().validate_binding(false, ops);
    return finish_prepared(prepared, ops);
}
ObjectPublication ObjectStore::finish_prepared(PreparedObject& prepared, const durable::Syscalls& ops) const {
    auto& value = *prepared.impl_;
    if (value.store != root_) throw std::runtime_error("prepared object authority mismatch");
    auto current = object_detail::publication_namespace(root_, ops);
    const auto pinned = durable::stat_fd(value.namespace_fd.get()), named = durable::stat_fd(current.get());
    if (pinned.st_dev != named.st_dev || pinned.st_ino != named.st_ino)
        throw std::runtime_error("prepared object namespace substituted");
    const auto& expected = value.id;
    auto result = durable::Publication::AlreadyExists;
    try {
        result = durable::publish_tree(value.private_fd.get(), "payload", current.get(), digest(expected), ops);
        const bool won = result == durable::Publication::Published;
        try {
            const auto final_model = check_identity(value.destination, expected,
                won ? Code::PostPublicationIntegrity : Code::ExistingObjectVerification,
                won ? Code::PostPublicationIntegrity : Code::CollisionCorruption);
            if (final_model != value.model)
                throw ObjectError(won ? Code::PostPublicationIntegrity : Code::CollisionCorruption,
                                  "destination model differs under the same digest");
            if (!won) {
                try { object_detail::synchronize(value.destination, final_model, ops); }
                catch (const durable::PersistenceError& e) {
                    throw durable::PersistenceError(e.stage, true, e.error, "existing winner synchronization");
                }
                durable::directory_barrier(current.get(), true, ops);
            }
        } catch (...) {
            if (won) std::throw_with_nested(durable::PersistenceError(durable::PersistenceError::Stage::Publication, true, EIO, "published object verification"));
            throw;
        }
    } catch (durable::PersistenceError& error) {
        error.cleanup_failure = durable::cleanup_private_tree(current.get(), value.private_name, ops); throw;
    } catch (ObjectError& error) {
        error.record_cleanup_failure(durable::cleanup_private_tree(current.get(), value.private_name, ops)); throw;
    }
    durable::cleanup_private_tree(current.get(), value.private_name, ops);
    value.private_name.clear();
    return {value.destination, result};
}
}
