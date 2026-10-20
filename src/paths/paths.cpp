// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "paths.hpp"
#include "../core/detail/filesystem.hpp"
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
    using athena::core::detail::create_directory_tree;
    create_directory_tree(downloads(), 0755);
    create_directory_tree(sources(), 0755);
    create_directory_tree(build(), 0755);
    create_directory_tree(store(), 0755);
    create_directory_tree(current_layout.state, 0700, true);
    create_directory_tree(current_layout.repository_snapshots, 0700, true);
    create_directory_tree(current_layout.repository_state, 0700, true);
    create_directory_tree(current_layout.generation_state, 0700, true);
}

}
