#include "generation.hpp"
#include "../installed/environment.hpp"

#include "../activation/activation.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <map>
#include "../store/object_store.hpp"
#include "../store/manifest.hpp"
#include "../artifact/scanner.hpp"
#include "../artifact/identity.hpp"
#include <stdexcept>

namespace athena::generation {

GenerationManager::GenerationManager(
    std::filesystem::path state_directory
)
    : state_directory_(std::move(state_directory)),
      generations_directory_(
          state_directory_ / "generations"
      )
{
}

GenerationManager::GenerationManager(
    std::filesystem::path generation_directory,
    GenerationDirectoryTag
)
    : state_directory_(generation_directory.parent_path()),
      generations_directory_(std::move(generation_directory))
{
}

/*
 * Ensure that the persistent generation directory exists.
 */
void GenerationManager::ensure_directories() const
{
    std::filesystem::create_directories(
        generations_directory_
    );
}

/*
 * Create Generation 0 on first initialization.
 *
 * The operation is idempotent: an already initialized manager
 * simply returns the existing current generation.
 */
Generation GenerationManager::initialize()
{
    ensure_directories();

    const auto current_file =
        generations_directory_ / "current";

    if (std::filesystem::exists(current_file)) {
        return current();
    }

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
    ensure_directories();

    std::uint64_t next = 0;

    for (
        const auto& entry :
        std::filesystem::directory_iterator(
            generations_directory_
        )
    ) {
        if (!entry.is_directory()) {
            continue;
        }

        try {
            const auto id =
                std::stoull(
                    entry.path().filename().string()
                );

            if (id >= next) {
                next = id + 1;
            }
        }
        catch (const std::invalid_argument&) {
            /*
             * Ignore non-generation directories.
             */
        }
        catch (const std::out_of_range&) {
            throw std::runtime_error(
                "Identificatore di generation fuori intervallo: " +
                entry.path().string()
            );
        }
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
    const auto document = serialize(generation);

    const auto directory =
        generations_directory_ /
        std::to_string(generation.id);

    /*
     * Never overwrite an existing generation. This is the central
     * immutability guarantee of the generation layer.
     */
    if (std::filesystem::exists(directory)) {
        throw std::runtime_error(
            "La generation esiste già: " +
            std::to_string(generation.id)
        );
    }

    std::filesystem::create_directories(directory);

    const auto file =
        directory / "generation.toml";

    std::ofstream output(file);

    if (!output) {
        throw std::runtime_error(
            "Impossibile creare la generation: " +
            file.string()
        );
    }

    output << document;
    output.close();
    if (!output) throw std::runtime_error("Failed to persist generation");
}

/*
 * Persist the pointer to the current generation.
 */
void GenerationManager::write_current(
    std::uint64_t id
) const
{
    const auto file =
        generations_directory_ / "current";

    std::ofstream output(file);

    if (!output) {
        throw std::runtime_error(
            "Impossibile aggiornare la generation corrente: " +
            file.string()
        );
    }

    output << id << '\n';
}

/*
 * Load an immutable generation from disk.
 */
Generation GenerationManager::get(
    std::uint64_t id
) const
{
    const auto file =
        generations_directory_ /
        std::to_string(id) /
        "generation.toml";

    if (!std::filesystem::exists(file)) {
        throw std::runtime_error(
            "Generation non trovata: " +
            std::to_string(id)
        );
    }

    std::ifstream input(file);
    if (!input) throw std::runtime_error("Failed to read generation: " + file.string());
    std::string document((std::istreambuf_iterator<char>(input)), {});
    if (input.bad()) throw std::runtime_error("Failed to read generation: " + file.string());
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
    ensure_directories();

    std::vector<Generation> generations;

    for (
        const auto& entry :
        std::filesystem::directory_iterator(
            generations_directory_
        )
    ) {
        if (!entry.is_directory()) {
            continue;
        }

        std::uint64_t id;
        try {
            id = std::stoull(entry.path().filename().string());
        }
        catch (const std::invalid_argument&) {
            continue; // Auxiliary directories, never malformed metadata.
        }
        catch (const std::out_of_range&) {
            throw std::runtime_error("Generation identifier out of range: " + entry.path().string());
        }
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
Generation GenerationManager::create(
    const std::vector<GenerationEntry>& entries,
    const std::vector<realization::RealizationRecord>& realizations,
    const std::vector<installed::PolicyRequirement>& policies
)
{
    ensure_directories();

    const auto generation = make_generation(next_id(), entries, realizations, policies);

    write_generation(generation);

    return generation;
}

/*
 * Return the generation selected by the persistent current pointer.
 */
Generation GenerationManager::current() const
{
    const auto file =
        generations_directory_ / "current";

    if (!std::filesystem::exists(file)) {
        throw std::runtime_error(
            "Nessuna generation inizializzata."
        );
    }

    std::ifstream input(file);

    if (!input) {
        throw std::runtime_error(
            "Impossibile leggere la generation corrente: " +
            file.string()
        );
    }

    std::uint64_t id = 0;

    input >> id;

    if (input.fail()) {
        throw std::runtime_error(
            "Identificatore della generation corrente non valido: " +
            file.string()
        );
    }

    return get(id);
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
