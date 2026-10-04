#include "paths.hpp"
#include <utility>

namespace athena::paths {

namespace {

/*
 * Keep the production default in one place while allowing tests to
 * redirect the complete Athena filesystem hierarchy into a temporary
 * directory.
 */
athena::core::PathLayout current_layout =
    athena::core::PathLayout::from_root("/var/lib/athena");

}

/*
 * Root directory of the Athena-PM filesystem hierarchy.
 *
 * All paths managed by Athena are derived from this directory
 * instead of being hard-coded throughout the codebase.
 */
std::filesystem::path root()
{
    return current_layout.root;
}

/*
 * Override the filesystem root used by all Athena path helpers.
 *
 * This is intentionally process-local: it is a testing/isolation
 * mechanism, not persistent configuration.
 */
void set_root(const std::filesystem::path& path)
{
    current_layout = athena::core::PathLayout::from_root(path);
}

void set_layout(const athena::core::PathLayout& layout)
{
    layout.validate();
    current_layout = layout;
}

/*
 * Restore the normal production filesystem root.
 */
void reset_root()
{
    current_layout = athena::core::PathLayout::from_root("/var/lib/athena");
}

/*
 * Directory containing downloaded source archives.
 */
std::filesystem::path downloads()
{
    return current_layout.cache / "downloads";
}

/*
 * Directory containing extracted package source trees.
 */
std::filesystem::path sources()
{
    return current_layout.cache / "sources";
}

/*
 * Directory used for build output and temporary staging trees.
 */
std::filesystem::path build()
{
    return current_layout.build;
}

/*
 * Directory containing the installed package store.
 *
 * The store is one of the central components of Athena-PM and will
 * later evolve toward a hash-based, immutable package store.
 */
std::filesystem::path store()
{
    return current_layout.store;
}

/*
 * Create the complete Athena-PM directory hierarchy.
 *
 * create_directories() is safe when a directory already exists,
 * so initialization can be called repeatedly.
 */
void initialize()
{
    current_layout.validate();
    std::filesystem::create_directories(downloads());
    std::filesystem::create_directories(sources());
    std::filesystem::create_directories(build());
    std::filesystem::create_directories(store());
    std::filesystem::create_directories(current_layout.state);
    std::filesystem::create_directories(current_layout.repository_snapshots);
    std::filesystem::create_directories(current_layout.repository_state);
    std::filesystem::create_directories(current_layout.generation_state);
}

}
