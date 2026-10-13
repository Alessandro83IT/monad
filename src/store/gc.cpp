// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gc.hpp"
#include "object_store.hpp"
#include "gc_trash.hpp"

#include "../generation/generation.hpp"
#include "../generation/publication.hpp"
#include "../activation/plan_retention.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <optional>
#include <sys/stat.h>
#include <unistd.h>

namespace athena::store {

namespace {
// Only the legacy locator may contain links. This is a read-only presence
// probe, never a deletion authority. ObjectStore still opens its root strictly.
bool has_object_namespace(const std::filesystem::path& legacy_root) {
    int fd = ::open(legacy_root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) throw std::runtime_error("Open GC store locator: " + std::string(std::strerror(errno)));
    try {
        for (const char* name : {"objects", "v1", "sha256"}) {
            struct stat status {};
            if (::fstatat(fd, name, &status, AT_SYMLINK_NOFOLLOW) != 0) {
                if (errno == ENOENT) { ::close(fd); return false; }
                throw std::runtime_error("Inspect GC namespace: " + std::string(std::strerror(errno)));
            }
            // A suspicious namespace component is present, not absent.
            // Let strict ObjectStore inspection reject it before deletion.
            if (!S_ISDIR(status.st_mode)) { ::close(fd); return true; }
            const int next = ::openat(fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (next < 0) throw std::runtime_error("Open GC namespace: " + std::string(std::strerror(errno)));
            ::close(fd);
            fd = next;
        }
        ::close(fd);
        return true;
    } catch (...) { ::close(fd); throw; }
}
}

/*
 * Store entries are identified by their canonical absolute paths.
 *
 * Generations may contain equivalent paths written with different
 * relative components, so normalizing both sides prevents an entry
 * from being collected merely because its textual path differs.
 */
static std::filesystem::path normalize_path(
    const std::filesystem::path& path
)
{
    return std::filesystem::absolute(path).lexically_normal();
}

GarbageCollector::GarbageCollector(std::filesystem::path store, std::filesystem::path state)
    : GarbageCollector(std::move(store), std::move(state) / "generations", GenerationDirectoryTag{}) {}
GarbageCollector::GarbageCollector(std::filesystem::path store, std::filesystem::path generations, GenerationDirectoryTag)
    : store_directory_(administrative::locator(store)), state_directory_(administrative::locator(generations)),
      bound_store_(store_directory_) {
    // Explicit legacy compatibility binding; a modern namespace never permits linked locators.
    if (std::filesystem::exists(store_directory_)) {
        const bool linked = std::filesystem::is_symlink(std::filesystem::symlink_status(store_directory_));
        const auto canonical = std::filesystem::canonical(store_directory_);
        if (!has_object_namespace(store_directory_)) bound_store_ = canonical;
        else if (linked) {
            // A prior legacy-only pass may have created the modern admin
            // namespace through this validated locator. Continue only when
            // both durable binding records prove that exact canonical target.
            try {
                AuthorityContext(canonical, state_directory_).validate_binding(false);
                bound_store_ = canonical;
            } catch (...) { /* Strict modern validation will reject the locator. */ }
        }
    }
}
GarbageCollector::GarbageCollector(AuthorityContext authority)
    : GarbageCollector(authority.store(), authority.generations(), GenerationDirectoryTag{}) {}
bool GarbageCollector::is_store_entry(const std::filesystem::path& path) {
    namespace d = core::detail;
    const auto status = std::filesystem::symlink_status(path);
    if (!std::filesystem::is_directory(status)) return false;
    auto entry = d::open_directory(path, false, true);
    struct stat metadata{};
    if (::fstatat(entry.get(), ".athena", &metadata, AT_SYMLINK_NOFOLLOW) < 0) {
        if (errno == ENOENT) return false; throw std::runtime_error("legacy ownership inspection");
    }
    if (!S_ISDIR(metadata.st_mode)) throw std::runtime_error("unsafe legacy metadata directory");
    auto admin = d::open_at(entry.get(), ".athena", O_RDONLY | O_DIRECTORY);
    administrative::same_mount(entry.get(), admin.get());
    if (::fstatat(admin.get(), "metadata.toml", &metadata, AT_SYMLINK_NOFOLLOW) < 0) {
        if (errno == ENOENT) return false; throw std::runtime_error("legacy metadata inspection");
    }
    if (!S_ISREG(metadata.st_mode) || metadata.st_uid != ::geteuid() || (metadata.st_mode & 0022) || metadata.st_nlink != 1)
        throw std::runtime_error("unsafe legacy ownership marker");
    return true;
}
namespace {
struct Snapshot {
    std::set<std::string> artifacts, legacy;
    bool transactions;
};
Snapshot roots(const AuthorityContext& authority, const std::filesystem::path& legacy_locator, const core::FileLock& state) {
    // Exactly one enumeration under shared State -> exclusive StoreGc.
    generation::GenerationManager manager(authority.generations(), generation::GenerationManager::GenerationDirectoryTag{});
    std::vector<generation::Generation> generations;
    for (const auto id : generation::publication::existing_ids_for_gc(authority.generations()))
        generations.push_back(manager.get(id));
    const auto pins = scan_pins(authority);
    Snapshot result{pins.artifacts, {}, pins.transactions};
    const auto plans = activation::scan_plan_references(authority.generations(), state, authority.store());
    result.artifacts.insert(plans.artifacts.begin(), plans.artifacts.end());
    const auto root = normalize_path(legacy_locator);
    auto retain_legacy = [&](std::filesystem::path path) {
        path = normalize_path(path);
        const auto relative = path.lexically_relative(root);
        if (relative.empty() || relative == "." || relative.is_absolute() || *relative.begin() == "..")
            throw std::runtime_error("generation/plan legacy path outside bound store");
        // GC detaches only top-level candidates. Protect the containing candidate
        // from persisted references, without inferring ownership or expanding all
        // ancestors (which could make long documents quadratic in space).
        result.legacy.insert(path.string());
        result.legacy.insert((root / *relative.begin()).string());
    };
    // Translate canonical bound-store paths into the historical display locator.
    // This preserves C's explicitly bound legacy-only symlink compatibility.
    for (const auto& path : plans.legacy)
        retain_legacy(legacy_locator / std::filesystem::path(path).lexically_relative(authority.store()));
    std::error_code object_namespace_error;
    const auto object_namespace = std::filesystem::canonical(authority.objects(), object_namespace_error);
    for (const auto& generation : generations) {
        generation.validate();
        for (const auto& record : generation.realizations) {
            record.validate(); result.artifacts.insert(record.artifact_id.value());
        }
        for (const auto& entry : generation.entries) {
            const auto path = normalize_path(entry.store_path), relative = path.lexically_relative(root);
            if (relative.empty() || relative == "." || relative.is_absolute() || *relative.begin() == "..")
                throw std::runtime_error("generation legacy path outside bound store");
            retain_legacy(path);
            // A legacy locator may intentionally be a symlink to an ArtifactId
            // directory. Preserve that historical alias behavior while also
            // retaining the underlying object as a G root.
            if (!object_namespace_error) {
                std::error_code resolved_error;
                const auto resolved = std::filesystem::canonical(entry.store_path, resolved_error);
                if (!resolved_error) {
                    const auto object_relative = resolved.lexically_relative(object_namespace);
                    if (!object_relative.empty() && !object_relative.is_absolute() && *object_relative.begin() != "..") {
                        const auto digest = object_relative.begin()->string();
                        if (digest.size() == 64 && std::all_of(digest.begin(), digest.end(), [](unsigned char c) {
                                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                            })) result.artifacts.insert("sha256:" + digest);
                    }
                }
            }
        }
    }
    return result;
}
void validate_generation_authority(const AuthorityContext& authority) {
    generation::GenerationManager manager(authority.generations(), generation::GenerationManager::GenerationDirectoryTag{});
    const auto ids = generation::publication::existing_ids_for_gc(authority.generations());
    if (ids.empty()) throw std::runtime_error("uninitialized generation authority");
    for (const auto id : ids) { auto value = manager.get(id); value.validate(); }
    const auto current = manager.current();
    if (std::find(ids.begin(), ids.end(), current.id) == ids.end())
        throw std::runtime_error("current generation is outside persisted authority");
}
}
std::vector<std::filesystem::path> GarbageCollector::collect(const core::detail::Syscalls& ops) {
    namespace d = core::detail;
    if (!std::filesystem::exists(store_directory_)) return {};
    AuthorityContext authority(bound_store_, state_directory_);
    // Binding initialization mutates both authorities and therefore uses
    // exclusive State. Reachability snapshots below use shared State.
    struct stat witness{};
    const auto witness_path = state_directory_ / ".athena-store-authority.toml";
    if (::lstat(witness_path.c_str(), &witness) < 0) {
        if (errno != ENOENT) throw std::runtime_error("generation binding witness inspection failed");
        core::FileLock state(core::state_lock_path(state_directory_), core::LockKind::State);
        core::FileLock store(core::store_gc_lock_path(bound_store_), core::LockKind::StoreGc);
        // Validate the concrete generation namespace before writing either
        // side of a new authority binding. Empty, uninitialized, malformed,
        // or state-parent locators are never promoted into authorities.
        validate_generation_authority(authority);
        authority.validate_binding(true, ops);
    }
    std::vector<core::ArtifactId> objects;
    std::vector<std::filesystem::path> legacy;
    {
        core::FileLock state(core::state_lock_path(state_directory_), core::LockKind::State, core::LockMode::Shared);
        core::FileLock store(core::store_gc_lock_path(bound_store_), core::LockKind::StoreGc);
        authority.validate_binding(false, ops);
        const auto snapshot = roots(authority, store_directory_, state); // Validate ALL roots before first detach.
        ObjectStore object_store(bound_store_);
        for (const auto& id : object_store.list()) if (!snapshot.artifacts.contains(id.value())) objects.push_back(id);
        if (!snapshot.transactions) for (const auto& entry : std::filesystem::directory_iterator(bound_store_)) {
            const auto display = store_directory_ / entry.path().filename();
            if (is_store_entry(entry.path()) && !snapshot.legacy.contains(normalize_path(display).string())) legacy.push_back(display);
        }
    }
    std::vector<std::filesystem::path> removed;
    // Each claimant is acquired BEFORE authority locks. Re-snapshot before each
    // detach; never reuse a released reachability decision.
    for (const auto& id : objects) {
        GcTrash trash(authority.objects(), true, ops);
        bool detached = false;
        {
            core::FileLock state(core::state_lock_path(state_directory_), core::LockKind::State, core::LockMode::Shared);
            core::FileLock store(core::store_gc_lock_path(bound_store_), core::LockKind::StoreGc);
            authority.validate_binding(false, ops);
            const auto snapshot = roots(authority, store_directory_, state);
            if (!snapshot.artifacts.contains(id.value())) {
                const auto candidates = ObjectStore(bound_store_).list();
                if (std::find(candidates.begin(), candidates.end(), id) != candidates.end()) {
                    trash.detach_locked(id.value().substr(7), ops); detached = true;
                } else trash.confirm_locked(ops);
            } else trash.confirm_locked(ops);
        }
        if (detached) removed.push_back(ObjectStore(store_directory_).object_path(id));
        trash.reclaim(authority, ops);
    }
    for (const auto& path : legacy) {
        GcTrash trash(bound_store_, false, ops); bool detached = false;
        {
            core::FileLock state(core::state_lock_path(state_directory_), core::LockKind::State, core::LockMode::Shared);
            core::FileLock store(core::store_gc_lock_path(bound_store_), core::LockKind::StoreGc);
            authority.validate_binding(false, ops); const auto snapshot = roots(authority, store_directory_, state);
            if (!snapshot.transactions && !snapshot.legacy.contains(normalize_path(path).string()) &&
                is_store_entry(bound_store_ / path.filename())) {
                trash.detach_locked(path.filename().string(), ops); detached = true;
            } else trash.confirm_locked(ops);
        }
        if (detached) removed.push_back(path);
        trash.reclaim(authority, ops);
    }
    std::sort(removed.begin(), removed.end()); return removed;
}
void GarbageCollector::reclaim_trash(const core::detail::Syscalls& ops) {
    namespace d = core::detail;
    AuthorityContext authority(bound_store_, state_directory_);
    for (const auto& source : {authority.objects(), bound_store_}) {
        for (const auto& id : GcTrash::containers(source)) {
            {
                core::FileLock store(core::store_gc_lock_path(bound_store_), core::LockKind::StoreGc);
                authority.validate_binding(false, ops);
                auto parent = d::open_directory(source / ".athena-gc-trash", false, true);
                struct stat status{};
                if (::fstatat(parent.get(), id.c_str(), &status, AT_SYMLINK_NOFOLLOW) < 0) {
                    if (errno == ENOENT) continue; throw std::runtime_error("trash cleanup entry inspection");
                }
                auto container = d::open_at(parent.get(), id, O_RDONLY | O_DIRECTORY);
                administrative::private_directory(container.get()); administrative::same_mount(parent.get(), container.get());
                if (administrative::entries(container.get()).empty()) {
                    administrative::unchanged(parent.get(), id, container.get());
                    if ((ops.unlink ? ops.unlink : ::unlinkat)(parent.get(), id.c_str(), AT_REMOVEDIR) < 0)
                        throw std::runtime_error("empty trash final cleanup");
                    d::directory_barrier(parent.get(), true, ops); continue;
                }
            }
            try {
                GcTrash trash(source, source == authority.objects(), id);
                {
                    core::FileLock state(core::state_lock_path(state_directory_), core::LockKind::State, core::LockMode::Shared);
                    core::FileLock store(core::store_gc_lock_path(bound_store_), core::LockKind::StoreGc);
                    authority.validate_binding(false, ops); trash.confirm_locked(ops);
                }
                trash.reclaim(authority, ops);
            } catch (const std::exception&) {
                // A competing successful reclaimer may have removed the name
                // between enumeration and claim. Recheck under StoreGc.
                const auto failure = std::current_exception();
                core::FileLock store(core::store_gc_lock_path(bound_store_), core::LockKind::StoreGc);
                auto parent = d::open_directory(source / ".athena-gc-trash", false, true); struct stat status{};
                if (::fstatat(parent.get(), id.c_str(), &status, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT) continue;
                try { std::rethrow_exception(failure); }
                catch (const std::system_error& error) {
                    if (error.code().value() == EWOULDBLOCK || error.code().value() == EAGAIN) continue;
                    throw;
                }
            }
        }
    }
}
}
