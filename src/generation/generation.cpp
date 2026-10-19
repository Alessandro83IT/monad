// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "generation.hpp"
#include "../installed/environment.hpp"

#include "../activation/activation.hpp"
#include "../activation/plan_retention.hpp"
#include "../activation/current_selection.hpp"

#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <sstream>
#include <system_error>
#include <athena/core/locking.hpp>
#include "../core/detail/filesystem.hpp"
#include "publication.hpp"
#include <set>
#include <map>
#include "../store/object_store.hpp"
#include "../store/transaction_pins.hpp"
#include "../store/manifest.hpp"
#include "../artifact/scanner.hpp"
#include "../artifact/identity.hpp"
#include <stdexcept>

namespace athena::generation {
namespace {
std::filesystem::path bind_authority(const std::filesystem::path& locator) {
    const auto absolute = std::filesystem::absolute(locator);
    auto bound = absolute.root_path();
    for (const auto& component : absolute.relative_path()) {
        if (component.empty() || component == ".") continue;
        // Never collapse parent traversal across a component that may be a link.
        core::detail::leaf(component.string());
        bound /= component;
    }
    return bound;
}
std::string read_metadata(const std::filesystem::path& directory, std::string_view name) {
    namespace d = core::detail;
    auto parent = d::open_directory(directory);
    auto file = d::open_at(parent.get(), name, O_RDONLY | O_NONBLOCK);
    d::regular(file.get());
    std::string document;
    char buffer[4096];
    for (;;) {
        const auto size = ::read(file.get(), buffer, sizeof(buffer));
        if (size < 0 && errno == EINTR) continue;
        if (size < 0) throw std::system_error(errno, std::generic_category(), "read generation metadata");
        if (size == 0) return document;
        if (name == "current" && static_cast<std::size_t>(size) > activation::max_current_size - document.size())
            throw activation::SchemaError(activation::SchemaErrorCode::Bounds, "Current record exceeds D1 byte bound");
        document.append(buffer, static_cast<std::size_t>(size));
    }
}
}

GenerationManager::GenerationManager(
    std::filesystem::path state_directory
)
    : state_directory_(bind_authority(state_directory)),
      generations_directory_(
          state_directory_ / "generations"
      )
{
}

GenerationManager::GenerationManager(
    std::filesystem::path generation_directory,
    GenerationDirectoryTag
)
    : generations_directory_(bind_authority(generation_directory))
{
    state_directory_ = generations_directory_.parent_path();
}

/*
 * Ensure that the persistent generation directory exists.
 */
void GenerationManager::ensure_durable_directories() const
{
    namespace d = core::detail;
    // Existing authority ancestors must already be durable. Validate before mkdir.
    auto parent = d::open_durable_directory(generations_directory_);
    auto state = d::open_directory(state_directory_);
    d::require_durable_directory(state.get());
    d::require_durable_directory(parent.get());
    d::directory_barrier(parent.get());
    d::directory_barrier(state.get());
}

/*
 * Create Generation 0 on first initialization.
 *
 * The operation is idempotent: an already initialized manager
 * simply returns the existing current generation.
 */
Generation GenerationManager::initialize()
{
    ensure_durable_directories();
    core::FileLock lock(core::state_lock_path(generations_directory_), core::LockKind::State);

    auto authority = core::detail::open_directory(generations_directory_);
    struct stat current_entry{};
    if (::fstatat(authority.get(), "current", &current_entry, AT_SYMLINK_NOFOLLOW) == 0) {
        return current();
    }
    if (errno != ENOENT) throw std::system_error(errno, std::generic_category(), "Inspect current for initialization");
    struct stat binding{};
    if (::fstatat(authority.get(), ".athena-projection-authority.toml", &binding, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT)
        throw std::runtime_error("Bound projection requires explicit D6 generation 0 selection");

    const Generation initial{
        0,
        {}
    };

    write_generation(initial);
    write_current(initial.id);

    return initial;
}

/*
 * Determine the next monotonically increasing generation ID.
 */
std::uint64_t GenerationManager::next_id() const
{
    std::uint64_t next = 0;
    for (const auto id : publication::existing_ids(generations_directory_)) {
        if (id == publication::max_id)
            throw std::overflow_error("Generation identifier exhausted");
        next = std::max(next, id + 1);
    }

    return next;
}

/*
 * Persist an immutable generation.
 */
void GenerationManager::write_generation(
    const Generation& generation
) const
{
    // Caller holds exclusive State; helper does not acquire nested locks.
    publication::persist(generations_directory_, generation.id, serialize(generation));
}

/*
 * Persist the pointer to the current generation.
 */
void GenerationManager::write_current(
    std::uint64_t id
) const
{
    namespace d = core::detail;
    auto parent = d::open_directory(generations_directory_);
    struct stat existing{};
    if (::fstatat(parent.get(), "current", &existing, AT_SYMLINK_NOFOLLOW) == 0) {
        // Never repair unsafe historical authority by replacing or chmodding it.
        if (!S_ISREG(existing.st_mode) || existing.st_uid != ::geteuid() ||
            existing.st_nlink != 1 || (existing.st_mode & 07022))
            throw std::runtime_error("Unsafe historical current metadata");
    } else if (errno != ENOENT) {
        throw std::system_error(errno, std::generic_category(), "Inspect current before publication");
    }
    // Exclusive 0600 temporary, complete write, file fsync, rename, directory fsync.
    // No process-global umask changes and no chmod of existing metadata.
    d::atomic_replace(parent.get(), "current", std::to_string(id) + "\n");
}

/*
 * Load an immutable generation from disk.
 */
Generation GenerationManager::get(
    std::uint64_t id
) const
{
    if (id > publication::max_id) throw std::overflow_error("Generation identifier exceeds TOML integer range");
    const auto document = read_metadata(generations_directory_ / std::to_string(id), "generation.toml");
    auto generation = deserialize_generation(document);
    if (generation.id != id) throw std::runtime_error("Generation identifier mismatch");
    return generation;
}

/*
 * Discard a generation that is no longer part of a successful
 * transaction.
 *
 * Generations are immutable once committed, but a newly created
 * generation may still belong to an in-progress transaction. The
 * transaction owner may explicitly discard that generation if
 * activation fails.
 *
 * The current generation is protected because deleting it would
 * invalidate the persistent environment state.
 */
void GenerationManager::discard(
    std::uint64_t id
)
{
    core::FileLock state(core::state_lock_path(generations_directory_), core::LockKind::State);
    const auto references = activation::scan_plan_references(generations_directory_, state);
    if (references.generations.contains(id))
        throw std::runtime_error("Generation is retained by an activation plan: " + std::to_string(id));
    const auto current_generation =
        current();

    if (current_generation.id == id) {
        throw std::runtime_error(
            "Impossibile eliminare la generation corrente: " +
            std::to_string(id)
        );
    }

    const auto directory =
        generations_directory_ /
        std::to_string(id);

    if (!std::filesystem::exists(directory)) {
        throw std::runtime_error(
            "Generation non trovata: " +
            std::to_string(id)
        );
    }

    /*
     * The generation metadata is removed only after the current
     * pointer has been verified. Store entries are deliberately not
     * touched here: they are immutable objects and are managed by
     * the garbage collector.
     */
    std::filesystem::remove_all(directory);
}

/*
 * Load every persisted generation.
 *
 * Directory names that are not numeric generation identifiers are
 * ignored, matching the behavior already used by next_id(). The
 * returned vector is sorted by generation ID to keep callers
 * deterministic.
 */
std::vector<Generation> GenerationManager::list() const
{
    std::vector<Generation> generations;
    for (const auto id : publication::existing_ids(generations_directory_)) {
        // Validation errors must propagate: every persisted generation is a GC root.
        generations.push_back(get(id));
    }

    std::sort(
        generations.begin(),
        generations.end(),
        [](const Generation& left, const Generation& right) {
            return left.id < right.id;
        }
    );

    return generations;
}

/*
 * Create a new immutable generation.
 */
Generation GenerationManager::create_pinned(const std::vector<GenerationEntry>& entries,
    const std::vector<realization::RealizationRecord>& records,
    const std::vector<installed::PolicyRequirement>& policies, store::TransactionPins& transaction,
    const core::detail::Syscalls& ops) {
    if (transaction.authority().generations() != generations_directory_)
        throw std::runtime_error("generation transaction authority mismatch");
    ensure_durable_directories();
    core::FileLock state(core::state_lock_path(generations_directory_), core::LockKind::State);
    store::StoreGcLock gc(transaction.authority().store());
    transaction.authority().validate_binding(false, ops);
    transaction.validate_live();
    for (const auto& record : records) {
        transaction.add_locked(record.artifact_id, gc, ops);
        store::ObjectStore(transaction.authority().store()).verify(record.artifact_id);
    }
    const auto generation = make_generation(next_id(), entries, records, policies);
    try { publication::persist(generations_directory_, generation.id, serialize(generation), ops); }
    catch (const core::detail::PersistenceError& error) {
        if (error.outcome_uncertain) transaction.stopped_ = true;
        throw;
    }
    return generation;
}

Generation GenerationManager::create(const std::vector<GenerationEntry>& entries,
    const std::vector<realization::RealizationRecord>& records,
    const std::vector<installed::PolicyRequirement>& policies) {
    if (!records.empty()) throw std::runtime_error("realization generation publication requires transaction pins");
    return create_impl(entries, records, policies);
}

Generation GenerationManager::create_unprotected_for_test(
    const std::vector<GenerationEntry>& entries,
    const std::vector<realization::RealizationRecord>& realizations,
    const std::vector<installed::PolicyRequirement>& policies
)
{
    return create_impl(entries, realizations, policies);
}

Generation GenerationManager::create_impl(
    const std::vector<GenerationEntry>& entries,
    const std::vector<realization::RealizationRecord>& realizations,
    const std::vector<installed::PolicyRequirement>& policies)
{
    ensure_durable_directories();

    core::FileLock lock(core::state_lock_path(generations_directory_), core::LockKind::State);
    const auto generation = make_generation(next_id(), entries, realizations, policies);

    write_generation(generation);

    return generation;
}

/*
 * Return the generation selected by the persistent current pointer.
 */
Generation GenerationManager::current() const
{
    return get(activation::selected_generation(activation::parse_current_selection(
        read_metadata(generations_directory_, "current"))));
}

/*
 * Switch the persistent current-generation pointer.
 *
 * The target generation must already exist. This prevents current
 * from ever referring to an invalid generation.
 */
/*
 * Switch the filesystem to an existing generation.
 *
 * A generation is represented by immutable store directories. The
 * target filesystem is updated by activating entries that belong to
 * the new generation and deactivating entries that belong only to
 * the previous generation.
 *
 * The persistent current pointer is changed only after all required
 * filesystem operations have succeeded.
 */
namespace {
// Runtime identity is tagged: no inference across legacy/object namespaces.
// The ObjectStore context is fixed for one switch, so an object key is its ID.
struct ActivationSource {
    bool object;
    std::string identity;
    std::filesystem::path directory;
    activation::Projection projection;
    auto key() const { return std::pair{object, identity}; }
};
}
void GenerationManager::switch_to(std::uint64_t id, const std::filesystem::path& root) {
    switch_impl(id, root, nullptr);
}
void GenerationManager::switch_to(std::uint64_t id, const std::filesystem::path& root,
                                  const store::ObjectStore& objects) {
    switch_impl(id, root, &objects);
}
void GenerationManager::switch_impl(std::uint64_t id, const std::filesystem::path& root,
                                    const store::ObjectStore* objects) {
    // Keep both persisted activation source generations alive against discard.
    core::FileLock state(core::state_lock_path(generations_directory_), core::LockKind::State, core::LockMode::Shared);
    auto authority = core::detail::open_directory(generations_directory_);
    struct stat binding{};
    if (::fstatat(authority.get(), ".athena-projection-authority.toml", &binding, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT)
        throw std::runtime_error("Bound projection requires D6/D7 selection and reconciliation");
    if (std::holds_alternative<activation::CurrentV1>(activation::parse_current_selection(read_metadata(generations_directory_, "current"))))
        throw std::runtime_error("CurrentV1 cannot be overwritten by legacy switch");
    const auto target = get(id);
    const auto previous = current();
    // Historical pre-M2.4 generations remain switchable under their original
    // integrity contract. Any generation carrying M2.4 state must validate its
    // complete semantics before target mutation; do not consult repositories.
    if (target.compatibility == GenerationCompatibility::Modern || !target.policies.empty() ||
        std::any_of(target.realizations.begin(), target.realizations.end(), [](const auto& r) { return r.semantics.has_value(); }) ||
        std::any_of(target.entries.begin(), target.entries.end(), [](const auto& e) { return e.semantics.has_value(); })) {
        auto semantic_target = target;
        installed::capture_legacy_semantics(semantic_target);
        installed::validate_environment(semantic_target);
    }
    const bool has_objects = !target.realizations.empty() || !previous.realizations.empty();
    if (has_objects && !objects)
        throw std::runtime_error("Realization activation requires an authoritative ObjectStore context");
    // Bind the supplied object namespace to this generation authority before
    // looking up or scanning any ArtifactId source. State -> StoreGc is the
    // same order used by publication and collection.
    if (has_objects) {
        store::StoreGcLock gc(objects->root());
        store::AuthorityContext authority(objects->root(), generations_directory_);
        authority.validate_binding(false);
    }

    // Full verification precedes any target mutation, including unchanged
    // memberships. Physical read-only permissions are not integrity evidence.
    std::map<std::string, activation::Projection> verified;
    auto resolve = [&](const Generation& generation) {
        std::vector<ActivationSource> sources;
        std::set<std::pair<bool, std::string>> seen;
        for (const auto& entry : generation.entries) {
            ActivationSource source{false, entry.store_path, entry.store_path, {}};
            if (std::filesystem::exists(source.directory))
                for (const auto& file : store::read_manifest(source.directory))
                    source.projection.links.push_back(file.path);
            sources.push_back(std::move(source));
        }
        for (const auto& record : generation.realizations) {
            const auto& identity = record.artifact_id.value();
            const auto path = objects->object_path(record.artifact_id);
            if (!verified.contains(identity)) {
                objects->verify(record.artifact_id);
                activation::Projection projection;
                // No administrative manifest is inserted in the hashed tree.
                const auto model = artifact::scan(path);
                if (artifact::derive_artifact_id(model) != record.artifact_id)
                    throw store::ObjectError(store::ObjectErrorCode::CollisionCorruption,
                                             "Object changed while preparing activation projection");
                for (const auto& entry : model.entries) {
                    if (std::holds_alternative<artifact::Directory>(entry.value))
                        projection.directories.push_back(entry.path);
                    else projection.links.push_back(entry.path);
                }
                verified.emplace(identity, std::move(projection));
            }
            ActivationSource source{true, identity, path, verified.at(identity)};
            if (seen.insert(source.key()).second) sources.push_back(std::move(source));
        }
        return sources;
    };
    const auto old_sources = resolve(previous);
    const auto new_sources = resolve(target);
    if (has_objects) {
        const auto& record = !target.realizations.empty() ? target.realizations.front() : previous.realizations.front();
        const auto object_namespace = objects->object_path(record.artifact_id).parent_path().lexically_normal();
        auto outside_namespace = [&](const std::filesystem::path& destination) {
            const auto relative = std::filesystem::absolute(destination).lexically_normal().lexically_relative(object_namespace);
            if (!relative.empty() && *relative.begin() != "..")
                throw std::runtime_error("Activation target must be outside the authoritative object namespace");
        };
        outside_namespace(root);
        for (const auto* sources : {&old_sources, &new_sources})
            for (const auto& source : *sources) {
                for (const auto& path : source.projection.links) outside_namespace(root / path);
                for (const auto& path : source.projection.directories) outside_namespace(root / path);
            }
    }
    if (previous.id == target.id) return;
    auto contains = [](const auto& sources, const auto& source) {
        return std::any_of(sources.begin(), sources.end(), [&](const auto& item) { return item.key() == source.key(); });
    };
    auto activate = [&](const ActivationSource& source) {
        if (has_objects) activation::activate(source.directory, root, source.projection);
        else activation::activate(source.directory, root);
    };
    auto deactivate = [&](const ActivationSource& source) {
        if (has_objects) activation::deactivate(source.directory, root, source.projection);
        else activation::deactivate(source.directory, root);
    };
    std::vector<const ActivationSource*> removed, added;
    removed.reserve(old_sources.size()); added.reserve(new_sources.size());
    std::set<std::filesystem::path> new_directories;
    const auto absolute_root = std::filesystem::absolute(root).lexically_normal();
    // Liveness includes explicit empty directories and every implicit parent.
    // Only paths described by previous sources are candidates for removal;
    // unrelated target directories are never adopted as cleanup candidates.
    auto required_directories = [&](const auto& sources) {
        std::set<std::filesystem::path> result;
        for (const auto& source : sources) {
            auto remember = [&](std::filesystem::path directory) {
                directory = (absolute_root / directory).lexically_normal();
                const auto relative = directory.lexically_relative(absolute_root);
                if (relative.empty() || *relative.begin() == "..") return;
                for (; directory != absolute_root; directory = directory.parent_path())
                    result.insert(directory);
            };
            for (const auto& path : source.projection.directories) remember(path);
            for (const auto& path : source.projection.links) remember(std::filesystem::path(path).parent_path());
        }
        return result;
    };
    const auto old_directories = required_directories(old_sources);
    const auto target_directories = required_directories(new_sources);
    std::vector<std::filesystem::path> removed_directories;
    auto remember_directories = [&](const ActivationSource& source) {
        auto remember = [&](std::filesystem::path directory) {
            directory = std::filesystem::absolute(directory).lexically_normal();
            const auto relative = directory.lexically_relative(absolute_root);
            if (relative.empty() || *relative.begin() == "..") return;
            for (;;) {
                if (std::filesystem::symlink_status(directory).type() == std::filesystem::file_type::not_found)
                    new_directories.insert(directory);
                // create_directories can also create missing ancestors of
                // target_root; journal those too, without adopting existing ones.
                if (directory == directory.parent_path()) break;
                directory = directory.parent_path();
            }
        };
        for (const auto& path : source.projection.directories) remember(root / path);
        for (const auto& path : source.projection.links) remember((root / path).parent_path());
    };
    try {
        for (const auto& source : old_sources) if (!contains(new_sources, source)) {
            // Include the attempted removal so best-effort restoration also
            // covers an I/O error after partial deactivation.
            removed.push_back(&source);
            deactivate(source);
        }
        // Reverse lexical order removes descendants first. remove() only
        // removes empty real directories: user contents remain a conflict.
        for (auto it = old_directories.rbegin(); it != old_directories.rend(); ++it) {
            if (target_directories.contains(*it)) continue;
            // Never follow a replaced target ancestor into a payload or user tree.
            bool real_parents = true;
            for (auto parent = it->parent_path(); !parent.empty(); parent = parent.parent_path()) {
                if (!std::filesystem::is_directory(std::filesystem::symlink_status(parent))) {
                    real_parents = false;
                    break;
                }
                if (parent == parent.parent_path()) break;
            }
            if (!real_parents) continue;
            if (!std::filesystem::is_directory(std::filesystem::symlink_status(*it))) continue;
            if (!std::filesystem::is_empty(*it)) continue;
            removed_directories.push_back(*it);
            if (!std::filesystem::remove(*it)) removed_directories.pop_back();
        }
        for (const auto& source : new_sources) if (!contains(old_sources, source)) {
            remember_directories(source);
            activate(source);
            added.push_back(&source);
        }
        write_current(target.id);
    } catch (...) {
        // After rename, a failed directory barrier leaves selection uncertain.
        // Rolling back payloads here could contradict the published current.
        try { throw; }
        catch (const core::detail::PersistenceError& error) {
            if (error.outcome_uncertain) throw;
        }
        catch (...) {}
        for (auto it = added.rbegin(); it != added.rend(); ++it) {
            try { deactivate(**it); } catch (...) {}
        }
        // Remove only directories absent before additions and still empty.
        // In particular new scaffolding must not block a restored old file link.
        // Lexical reverse order puts descendants before their prefix parents.
        for (auto it = new_directories.rbegin(); it != new_directories.rend(); ++it) {
            std::error_code error;
            if (std::filesystem::is_directory(std::filesystem::symlink_status(*it, error)))
                std::filesystem::remove(*it, error);
        }
        // Recreate removed scaffolding ancestor-first, even when the old
        // directory was implicit or its source remained otherwise unchanged.
        for (auto it = removed_directories.rbegin(); it != removed_directories.rend(); ++it) {
            try { std::filesystem::create_directories(*it); } catch (...) {}
        }
        for (auto it = removed.rbegin(); it != removed.rend(); ++it) {
            try { activate(**it); } catch (...) {}
        }
        throw; // Preserve the original failure even if recovery fails.
    }
}

}
