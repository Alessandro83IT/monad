#pragma once

#include <filesystem>
#include "../../include/athena/core/path_layout.hpp"

namespace athena::paths {

/*
 * Return the root directory used by Athena-PM for its local data.
 *
 * The current prototype stores all Athena data under:
 *
 *     /var/lib/athena
 *
 * The exact layout may evolve as the package store and generation
 * system become more sophisticated.
 */
/*
 * Root directory of the Athena filesystem hierarchy.
 *
 * The optional override is primarily useful for tests and isolated
 * installation environments. Production code keeps the default
 * /var/lib/athena root.
 */
std::filesystem::path root();

/*
 * Redirect all Athena-managed paths to another filesystem root.
 *
 * This is process-local and is primarily used by isolated tests.
 */
void set_root(const std::filesystem::path& path);

/* Legacy adapter for pre-M0 modules. New core consumers pass PathLayout explicitly. */
void set_layout(const athena::core::PathLayout& layout);

/*
 * Restore the normal production filesystem root.
 */
void reset_root();

/*
 * Directory containing downloaded package archives.
 */
std::filesystem::path downloads();

/*
 * Directory containing extracted package source trees.
 */
std::filesystem::path sources();

/*
 * Directory used for temporary build and staging data.
 */
std::filesystem::path build();

/*
 * Directory containing installed Athena package trees.
 */
std::filesystem::path store();

/*
 * Create the directory structure required by Athena-PM.
 *
 * This function should be called before other modules start using
 * the Athena filesystem hierarchy.
 */
void initialize();

}
