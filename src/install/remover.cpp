#include "remover.hpp"

#include "generation_builder.hpp"
#include "removal_planner.hpp"

#include "../dependency/parser.hpp"
#include "../generation/generation.hpp"
#include "../paths/paths.hpp"
#include "../package/package.hpp"
#include "../store/metadata.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace athena::install {

namespace {

athena::package::Package package_from_store(
    const athena::generation::GenerationEntry& entry
)
{
    const std::filesystem::path store_path(entry.store_path);

    if (!std::filesystem::exists(store_path) ||
        !std::filesystem::is_directory(store_path)) {
        throw std::runtime_error(
            "Generation references missing store directory: " +
            store_path.string()
        );
    }

    const auto metadata = athena::store::read_metadata(store_path);

    /*
     * The generation and store metadata must agree about the logical
     * package name. Otherwise removal could validate one package while
     * the generation actually activates another one.
     */
    if (metadata.name != entry.package) {
        throw std::runtime_error(
            "Generation/store package mismatch for '" +
            entry.package + "'"
        );
    }

    athena::package::Package package;
    package.name = metadata.name;
    package.version = metadata.version;
    package.description = "";
    package.source = metadata.source;
    package.sha256 = metadata.sha256;
    package.build_system = metadata.build_system;

    /*
     * Dependency expressions are persisted in store metadata so an
     * installed environment can be validated without consulting the
     * repository. This is essential for removal, because the packages
     * remaining in the generation are the source of truth.
     */
    for (const auto& serialized : metadata.dependencies) {
        package.dependencies.push_back(
            athena::dependency::parse_dependency(serialized)
        );
    }

    return package;
}

} // namespace

void remove_packages(
    const std::vector<std::string>& package_names,
    const std::filesystem::path& target_root
)
{
    remove_packages(package_names, target_root,
        athena::core::PathLayout::from_root(athena::paths::root()));
}

void remove_packages(
    const std::vector<std::string>& package_names,
    const std::filesystem::path& target_root,
    const athena::core::PathLayout& paths
)
{
    paths.validate();
    std::filesystem::create_directories(paths.store);
    std::filesystem::create_directories(paths.generation_state);

    athena::generation::GenerationManager generations(
        paths.generation_state,
        athena::generation::GenerationManager::GenerationDirectoryTag{}
    );

    generations.initialize();

    const auto current = generations.current();

    /*
     * Reconstruct the complete installed environment from the current
     * generation. We intentionally do not scan the Store globally:
     * historical generations may reference versions that are no longer
     * the newest version of a package.
     */
    std::vector<athena::package::Package> installed;
    installed.reserve(current.entries.size());

    for (const auto& entry : current.entries) {
        installed.push_back(package_from_store(entry));
    }

    RemovalPlanner planner(installed);
    const RemovePlan removal = planner.plan(package_names);

    if (removal.empty()) {
        return;
    }

    /*
     * Build the candidate generation without modifying the immutable
     * Store. Store objects remain available for historical generations
     * and are reclaimed later by garbage collection.
     */
    const auto target_entries = remove_target_entries(
        current,
        removal
    );

    const auto target = generations.create(target_entries);

    /*
     * GenerationManager owns the filesystem transaction. This keeps
     * removal symmetric with installation and ensures that "current"
     * changes only after activation succeeds.
     */
    generations.switch_to(target.id, target_root);

    std::cout << "Rimozione completata. "
              << "Nuova generation: "
              << target.id
              << '\n';
}

} // namespace athena::install
