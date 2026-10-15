// SPDX-License-Identifier: GPL-3.0-or-later
#include "../parsing/bounded_toml.hpp"
#include "transaction_pins.hpp"
#include "../generation/publication.hpp"
#include "object_store_detail.hpp"
#include "../generation/generation.hpp"
#include <athena_toml/athena_toml.hpp>
#include <algorithm>
#include <sstream>
#include <array>
#include <dirent.h>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <unistd.h>
#include <system_error>

namespace athena::store {
namespace d = core::detail;
namespace fs = std::filesystem;
namespace administrative {
bool hex_name(std::string_view name, std::size_t size) {
    return name.size() == size && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
fs::path locator(const fs::path& path) {
    auto absolute = fs::absolute(path); fs::path result = absolute.root_path();
    for (const auto& part : absolute.relative_path()) {
        if (part.empty() || part == ".") continue;
        d::leaf(part.string()); result /= part;
    }
    return result;
}
std::vector<std::string> entries(int fd) {
    // Fresh open file description: duplicated directory fds share readdir offsets.
    int copy = ::openat(fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (copy < 0) throw std::runtime_error("open administrative stream");
    DIR* raw = ::fdopendir(copy);
    if (!raw) { ::close(copy); throw std::runtime_error("administrative fdopendir"); }
    auto close_stream = [](DIR* value) { ::closedir(value); };
    std::unique_ptr<DIR, decltype(close_stream)> stream(raw, close_stream);
    std::vector<std::string> result;
    for (;;) {
        errno = 0; auto* entry = ::readdir(raw);
        if (!entry) { if (errno) throw std::runtime_error("administrative readdir"); break; }
        std::string name(entry->d_name);
        if (name != "." && name != "..") result.push_back(std::move(name));
    }
    std::sort(result.begin(), result.end()); return result;
}
void private_directory(int fd) {
    const auto s = d::stat_fd(fd);
    if (!S_ISDIR(s.st_mode) || s.st_uid != ::geteuid() || (s.st_mode & 07777) != 0700)
        throw std::runtime_error("unsafe administrative directory");
}
void private_file(int fd) {
    const auto s = d::stat_fd(fd);
    if (!S_ISREG(s.st_mode) || s.st_uid != ::geteuid() || (s.st_mode & 07777) != 0600 || s.st_nlink != 1)
        throw std::runtime_error("unsafe administrative file");
}
void unchanged(int parent, std::string_view name, int fd) {
    d::leaf(name); struct stat named{}; const std::string leaf(name);
    if (::fstatat(parent, leaf.c_str(), &named, AT_SYMLINK_NOFOLLOW) < 0)
        throw std::system_error(errno, std::generic_category(), "administrative entry missing");
    const auto pinned = d::stat_fd(fd);
    if (named.st_dev != pinned.st_dev || named.st_ino != pinned.st_ino || named.st_mode != pinned.st_mode)
        throw std::runtime_error("administrative entry substituted");
}
std::string read_file(int parent, std::string_view name) {
    auto file = d::open_at(parent, name, O_RDONLY); private_file(file.get()); same_mount(parent, file.get());
    unchanged(parent, name, file.get());
    if (d::stat_fd(file.get()).st_size > static_cast<off_t>(max_record_size))
        throw std::runtime_error("oversized administrative record");
    std::string result; char buffer[4096];
    for (;;) {
        const auto n = ::read(file.get(), buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) throw std::system_error(errno, std::generic_category(), "read administrative record");
        if (!n) break;
        if (result.size() + n > max_record_size) throw std::runtime_error("oversized administrative record");
        result.append(buffer, n);
    }
    unchanged(parent, name, file.get()); return result;
}
d::Fd ensure_directory(int parent, std::string_view name, const d::Syscalls& ops) {
    d::require_durable_directory(parent, ops); d::leaf(name); const std::string n(name);
    if ((ops.mkdir ? ops.mkdir : ::mkdirat)(parent, n.c_str(), 0700) < 0 && errno != EEXIST)
        throw std::system_error(errno, std::generic_category(), "administrative mkdir");
    auto result = d::open_at(parent, name, O_RDONLY | O_DIRECTORY);
    private_directory(result.get()); same_mount(parent, result.get());
    d::require_durable_directory(result.get(), ops);
    d::directory_barrier(result.get(), false, ops); d::directory_barrier(parent, true, ops);
    return result;
}
std::string random_id(const d::Syscalls& ops) {
    std::array<unsigned char, 16> bytes{}; std::size_t offset = 0;
    while (offset != bytes.size()) {
        const auto n = (ops.random ? ops.random : ::getrandom)(bytes.data() + offset, bytes.size() - offset, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::system_error(n == 0 ? EIO : errno, std::generic_category(), "transaction random ID");
        if (static_cast<std::size_t>(n) > bytes.size() - offset) throw std::logic_error("invalid random count");
        offset += n;
    }
    std::string result; constexpr char digits[] = "0123456789abcdef";
    for (auto b : bytes) { result += digits[b >> 4]; result += digits[b & 15]; }
    return result;
}
void same_mount(int a, int b) {
    struct statx x{}, y{};
    if (::syscall(SYS_statx, a, "", AT_EMPTY_PATH, STATX_BASIC_STATS | STATX_MNT_ID, &x) < 0 ||
        ::syscall(SYS_statx, b, "", AT_EMPTY_PATH, STATX_BASIC_STATS | STATX_MNT_ID, &y) < 0)
        throw std::runtime_error("administrative mount identity unavailable");
    object_detail::require_same_gc_mount(x, y);
}
void rename_detach(int source, std::string_view name, int dest, std::string_view target, const d::Syscalls& ops) {
    d::leaf(name); d::leaf(target); same_mount(source, dest);
    d::require_durable_directory(source, ops); d::require_durable_directory(dest, ops);
    const std::string a(name), b(target);
    if (ops.rename(source, a.c_str(), dest, b.c_str(), RENAME_NOREPLACE) < 0)
        throw d::PersistenceError(d::PersistenceError::Stage::Publication, false, errno, "detach rename");
    d::directory_barrier(source, true, ops); d::directory_barrier(dest, true, ops);
}
}
namespace a = administrative;
AuthorityContext::AuthorityContext(fs::path store, fs::path generations)
    : store_(a::locator(store)), generations_(a::locator(generations)) {}
namespace {
std::string binding_document(const AuthorityContext& authority) {
    athena_toml::table table; table.insert("schema_version", 1);
    table.insert("store_authority", authority.store().string());
    table.insert("generation_authority", authority.generations().string());
    std::ostringstream out; out << table << '\n'; return out.str();
}
}
bool AuthorityContext::inspect_binding(const StoreGcLock& lock) const {
    if (lock.store() != store_) throw std::invalid_argument("binding inspection lock mismatch");
    auto generations = d::open_directory(generations_, false, true);
    auto present = [](int parent, const char* name) {
        struct stat st{};
        if (::fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW) == 0) return true;
        if (errno == ENOENT) return false;
        throw std::system_error(errno, std::generic_category(), "inspect authority binding");
    };
    if (present(generations.get(), "generations"))
        throw std::runtime_error("ambiguous generation authority directory");
    const bool reverse = present(generations.get(), ".athena-store-authority.toml");
    auto objects = d::open_directory(store_, false, true);
    for (const char* part : {"objects", "v1", "sha256"}) {
        if (!present(objects.get(), part)) {
            if (reverse) throw std::runtime_error("bound object namespace missing");
            return false;
        }
        auto child = d::open_at(objects.get(), part, O_RDONLY | O_DIRECTORY);
        const auto status = d::stat_fd(child.get());
        if (status.st_uid != ::geteuid() || (status.st_mode & 07022))
            throw std::runtime_error("unsafe object namespace authority");
        a::same_mount(objects.get(), child.get()); a::unchanged(objects.get(), part, child.get());
        objects = std::move(child);
    }
    if (!present(objects.get(), ".athena-transactions")) {
        if (reverse) throw std::runtime_error("bound transaction registry missing");
        return false;
    }
    auto registry = d::open_at(objects.get(), ".athena-transactions", O_RDONLY | O_DIRECTORY);
    a::private_directory(registry.get()); a::same_mount(objects.get(), registry.get());
    if (!reverse || !present(registry.get(), "authority.toml"))
        throw std::runtime_error("partial store/generation binding; recovery required");
    auto matches = [&](std::string_view document) {
        const auto table = parsing::parse_model(document, parsing::Profile::StoreBinding);
        return table.size() == 3 && table["schema_version"].is_integer() &&
            table["schema_version"].value<int64_t>() == 1 &&
            table["store_authority"].value<std::string>() == store_.string() &&
            table["generation_authority"].value<std::string>() == generations_.string();
    };
    if (!matches(a::read_file(registry.get(), "authority.toml")) ||
        !matches(a::read_file(generations.get(), ".athena-store-authority.toml")))
        throw std::runtime_error("store/generation authority binding mismatch");
    return true;
}
void AuthorityContext::validate_binding(bool create, const d::Syscalls& ops) const {
    validate_binding(create, ops, d::ReplacementFailure::CleanupPrivate);
}
void AuthorityContext::validate_binding(bool create, const d::Syscalls& ops, d::ReplacementFailure failure) const {
    // A reverse record detects loss of the entire store-local registry. Without
    // this witness, a missing pin registry could be mistaken for a fresh store.
    auto generations = d::open_directory(generations_, false, true);
    // Binding is meaningful only for an initialized generation namespace.
    // In particular, never bind the state parent when it contains the real
    // generations child: that ambiguity would turn G into an empty set.
    struct stat nested{};
    if (::fstatat(generations.get(), "generations", &nested, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT)
        throw std::runtime_error("ambiguous generation authority directory");
    struct stat current{};
    if (::fstatat(generations.get(), "current", &current, AT_SYMLINK_NOFOLLOW) < 0 ||
        !S_ISREG(current.st_mode) || current.st_uid != ::geteuid() || current.st_nlink != 1 ||
        (current.st_mode & 07022))
        throw std::runtime_error("uninitialized generation authority");
    if (create && generation::publication::existing_ids_for_gc(generations_).empty())
        throw std::runtime_error("generation authority has no persisted roots");
    const auto expected = binding_document(*this);
    auto matches = [&](std::string_view document) {
        const auto table = parsing::parse_model(document, parsing::Profile::StoreBinding);
        return table.size() == 3 && table["schema_version"].is_integer() &&
            table["schema_version"].value<int64_t>() == 1 &&
            table["store_authority"].value<std::string>() == store_.string() &&
            table["generation_authority"].value<std::string>() == generations_.string();
    };
    struct stat reverse_status{};
    const bool reverse = ::fstatat(generations.get(), ".athena-store-authority.toml", &reverse_status, AT_SYMLINK_NOFOLLOW) == 0;
    if (!reverse && errno != ENOENT) throw std::runtime_error("generation binding inspection failed");
    if (reverse && !matches(a::read_file(generations.get(), ".athena-store-authority.toml")))
        throw std::runtime_error("generation/store authority binding mismatch");
    struct stat registry_status{};
    if (reverse) {
        // Strictly open existing directories before any create=true path.
        auto existing_objects = d::open_directory(this->objects(), false, true);
        if (::fstatat(existing_objects.get(), ".athena-transactions", &registry_status, AT_SYMLINK_NOFOLLOW) < 0)
            throw std::runtime_error("bound transaction registry missing; recovery required");
    }
    auto objects = create ? object_detail::publication_namespace(store_, ops) : d::open_directory(this->objects(), false, true);
    auto registry = create ? a::ensure_directory(objects.get(), ".athena-transactions", ops) :
        d::open_at(objects.get(), ".athena-transactions", O_RDONLY | O_DIRECTORY);
    a::private_directory(registry.get()); a::same_mount(objects.get(), registry.get());
    struct stat status{};
    if (::fstatat(registry.get(), "authority.toml", &status, AT_SYMLINK_NOFOLLOW) < 0) {
        if (errno != ENOENT || !create || reverse) throw std::runtime_error("missing authority binding");
        if (!a::entries(registry.get()).empty()) throw std::runtime_error("ambiguous unbound transaction registry");
        d::atomic_replace(registry.get(), "authority.toml", expected, ops, failure);
    }
    if (!matches(a::read_file(registry.get(), "authority.toml")))
        throw std::runtime_error("store/generation authority binding mismatch");
    if (!reverse) {
        if (!create || a::entries(registry.get()) != std::vector<std::string>{"authority.toml"})
            throw std::runtime_error("generation binding missing; recovery required");
        d::require_durable_directory(generations.get(), ops);
        d::atomic_replace(generations.get(), ".athena-store-authority.toml", expected, ops, failure);
        auto parent = d::open_directory(generations_.parent_path(), false, true);
        d::directory_barrier(parent.get(), true, ops);
    }
}
std::string serialize_pins(std::string_view id, const std::set<std::string>& ids) {
    if (!a::hex_name(id, 32)) throw std::invalid_argument("invalid transaction ID");
    std::string result = "schema_version = 1\ntransaction_id = \"" + std::string(id) + "\"\nartifacts = [\n";
    for (const auto& artifact : ids) {
        if (!artifact.starts_with("sha256:") || !a::hex_name(std::string_view(artifact).substr(7), 64))
            throw std::invalid_argument("invalid pinned ArtifactId");
        result += "    \"" + artifact + "\",\n";
    }
    result += "]\n";
    if (result.size() > a::max_record_size) throw std::runtime_error("oversized pin record");
    return result;
}
std::set<std::string> parse_pins(std::string_view document, std::string_view id) {
    if (document.size() > a::max_record_size || !a::hex_name(id, 32)) throw std::runtime_error("invalid pin record bounds");
    auto table = parsing::parse_model(document, parsing::Profile::Pins);
    if (table.size() != 3 || table["schema_version"].value<int64_t>() != 1 ||
        table["transaction_id"].value<std::string>() != id || !table["artifacts"].is_array())
        throw std::runtime_error("invalid pin schema");
    std::set<std::string> result; std::string previous;
    for (const auto& node : *table["artifacts"].as_array()) {
        auto value = node.value<std::string>();
        if (!value || !value->starts_with("sha256:") || !a::hex_name(std::string_view(*value).substr(7), 64) ||
            (!previous.empty() && previous >= *value)) throw std::runtime_error("invalid/duplicate/unsorted pin ArtifactId");
        previous = *value; result.insert(*value);
    }
    // Reject valid-TOML truncations, alternate spellings, comments and hidden fields.
    if (serialize_pins(id, result) != document) throw std::runtime_error("noncanonical pin record");
    return result;
}
PinSnapshot scan_pins(const AuthorityContext& authority) {
    authority.validate_binding(false);
    auto registry = d::open_directory(authority.registry(), false, true); a::private_directory(registry.get());
    PinSnapshot result;
    for (const auto& name : a::entries(registry.get())) {
        if (name == "authority.toml") continue;
        // Only registered IDs and disjoint known preparation directories are accepted.
        if (name.starts_with(".prepare-") && a::hex_name(std::string_view(name).substr(9), 32)) {
            auto prepared = d::open_at(registry.get(), name, O_RDONLY | O_DIRECTORY);
            a::private_directory(prepared.get()); a::same_mount(registry.get(), prepared.get());
            continue;
        }
        if (!a::hex_name(name, 32)) throw std::runtime_error("unexpected transaction registry entry");
        auto transaction = d::open_at(registry.get(), name, O_RDONLY | O_DIRECTORY);
        a::private_directory(transaction.get()); a::same_mount(registry.get(), transaction.get());
        if (a::entries(transaction.get()) != std::vector<std::string>{"lease", "pin.toml"})
            throw std::runtime_error("ambiguous transaction directory");
        if (a::read_file(transaction.get(), "lease") != name + "\n") throw std::runtime_error("invalid transaction lease");
        const auto ids = parse_pins(a::read_file(transaction.get(), "pin.toml"), name);
        a::unchanged(registry.get(), name, transaction.get());
        result.transactions = true; result.artifacts.insert(ids.begin(), ids.end());
    }
    return result;
}
TransactionPins::TransactionPins(AuthorityContext authority, const d::Syscalls& ops) : authority_(std::move(authority)) {
    // Namespace/binding setup holds no lease yet; preparation is never an active transaction.
    {
        core::FileLock state(core::state_lock_path(authority_.generations()), core::LockKind::State);
        core::FileLock store(core::store_gc_lock_path(authority_.store()), core::LockKind::StoreGc);
        authority_.validate_binding(true, ops);
    }
    auto registry = d::open_directory(authority_.registry(), false, true);
    std::string prepared;
    for (;;) {
        id_ = a::random_id(ops); prepared = ".prepare-" + id_;
        struct stat existing{};
        if (::fstatat(registry.get(), id_.c_str(), &existing, AT_SYMLINK_NOFOLLOW) == 0) continue;
        if (errno != ENOENT) throw std::runtime_error("transaction collision inspection");
        if ((ops.mkdir ? ops.mkdir : ::mkdirat)(registry.get(), prepared.c_str(), 0700) == 0) break;
        if (errno != EEXIST) throw std::system_error(errno, std::generic_category(), "prepare transaction");
    }
    directory_ = d::open_at(registry.get(), prepared, O_RDONLY | O_DIRECTORY); a::private_directory(directory_.get());
    lease_identity_ = d::open_at(directory_.get(), "lease", O_CREAT | O_EXCL | O_RDWR, 0600);
    d::complete_write(lease_identity_.get(), id_ + "\n", ops); d::file_barrier(lease_identity_.get(), ops);
    lease_ = std::make_unique<core::FileLock>(authority_.registry() / prepared / "lease", core::LockKind::TransactionLease, core::LockMode::Exclusive, "transaction lease", true, true);
    a::unchanged(directory_.get(), "lease", lease_->descriptor());
    auto file = d::open_at(directory_.get(), "pin.toml", O_CREAT | O_EXCL | O_WRONLY, 0600);
    d::complete_write(file.get(), serialize_pins(id_, {}), ops); d::file_barrier(file.get(), ops);
    d::directory_barrier(directory_.get(), false, ops);
    core::FileLock store(core::store_gc_lock_path(authority_.store()), core::LockKind::StoreGc);
    authority_.validate_binding(false, ops);
    // Registration collision never replaces another transaction.
    a::rename_detach(registry.get(), prepared, registry.get(), id_, ops);
}
void TransactionPins::validate_live() const {
    if (stopped_ || retired_) throw std::runtime_error("transaction cannot continue");
    auto registry = d::open_directory(authority_.registry(), false, true);
    a::unchanged(registry.get(), id_, directory_.get());
    a::private_directory(directory_.get()); a::unchanged(directory_.get(), "lease", lease_identity_.get());
    a::private_file(lease_identity_.get());
    if (a::entries(directory_.get()) != std::vector<std::string>{"lease", "pin.toml"} ||
        parse_pins(a::read_file(directory_.get(), "pin.toml"), id_) != artifacts_)
        throw std::runtime_error("transaction state substituted");
}
void TransactionPins::add_locked(const core::ArtifactId& id, const StoreGcLock& lock, const d::Syscalls& ops) {
    try {
        if (lock.store() != authority_.store()) throw std::runtime_error("pin lock authority mismatch");
        validate_live(); authority_.validate_binding(false, ops);
        auto next = artifacts_; next.insert(id.value());
        const auto document = serialize_pins(id_, next);
        if (next != artifacts_) d::atomic_replace(directory_.get(), "pin.toml", document, ops);
        artifacts_ = std::move(next);
    } catch (...) { stopped_ = true; throw; }
}
void TransactionPins::add(const core::ArtifactId& id, const d::Syscalls& ops) {
    StoreGcLock store(authority_.store());
    add_locked(id, store, ops);
}
ObjectPublication TransactionPins::publish(const ObjectStore& objects, PreparedObject& prepared, const d::Syscalls& ops) {
    if (objects.root() != authority_.store()) throw std::runtime_error("transaction ObjectStore mismatch");
    StoreGcLock store(authority_.store());
    add_locked(prepared.artifact_id(), store, ops);
    try { return objects.publish_prepared_locked(prepared, *this, ops); }
    catch (...) { stopped_ = true; throw; }
}
void TransactionPins::retire_locked(const d::Syscalls& ops) {
    validate_live(); authority_.validate_binding(false, ops);
    auto registry = d::open_directory(authority_.registry(), false, true);
    auto objects = d::open_directory(authority_.objects(), false, true);
    auto retired = a::ensure_directory(objects.get(), ".athena-retired-transactions", ops);
    a::rename_detach(registry.get(), id_, retired.get(), id_, ops);
    retired_ = true;
    // Lease remains open through durable retirement. Detached cleanup is best effort.
    lease_.reset(); lease_identity_ = {}; directory_ = {};
    d::cleanup_private_tree(retired.get(), id_, ops);
    d::directory_barrier(retired.get(), true, ops);
}
void TransactionPins::handoff(std::uint64_t id, const d::Syscalls& ops) {
    try {
        core::FileLock state(core::state_lock_path(authority_.generations()), core::LockKind::State, core::LockMode::Shared);
        core::FileLock store(core::store_gc_lock_path(authority_.store()), core::LockKind::StoreGc);
        generation::GenerationManager manager(authority_.generations(), generation::GenerationManager::GenerationDirectoryTag{});
        const auto generation = manager.get(id); std::set<std::string> roots;
        for (const auto& record : generation.realizations) roots.insert(record.artifact_id.value());
        if (!std::includes(roots.begin(), roots.end(), artifacts_.begin(), artifacts_.end()))
            throw std::runtime_error("generation does not supersede transaction pins");
        // Re-establish publication barrier before relinquishing the only durable pins.
        auto parent = d::open_directory(authority_.generations());
        auto target = d::open_at(parent.get(), std::to_string(id), O_RDONLY | O_DIRECTORY);
        auto metadata = d::open_at(target.get(), "generation.toml", O_RDONLY);
        d::file_barrier(metadata.get(), ops); d::directory_barrier(target.get(), true, ops);
        d::directory_barrier(parent.get(), true, ops);
        retire_locked(ops);
    } catch (...) { stopped_ = true; throw; }
}
void TransactionPins::abandon(const d::Syscalls& ops) {
    core::FileLock store(core::store_gc_lock_path(authority_.store()), core::LockKind::StoreGc);
    try { retire_locked(ops); } catch (...) { stopped_ = true; throw; }
}
}
