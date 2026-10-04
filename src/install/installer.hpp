#pragma once

#include "install_plan.hpp"
#include "../../include/athena/core/path_layout.hpp"

#include <filesystem>

namespace athena::install {

/*
 * Install a package described by a package definition file.
 *
 * The package is resolved as a single-package plan and executed
 * through the same transactional generation pipeline used by
 * multi-package installations.
 */
void install_package(
    const std::filesystem::path& package_file,
    const std::filesystem::path& target_root
);
void install_package(
    const std::filesystem::path& package_file,
    const std::filesystem::path& target_root,
    const athena::core::PathLayout& paths
);

/*
 * Execute an already-resolved InstallPlan.
 *
 * The dependency resolver decides which packages are required and
 * preserves their dependency-before-dependent ordering.
 *
 * The installer first places every package in the immutable store,
 * then creates a new Generation and activates that generation
 * transactionally. Individual packages are therefore never activated
 * directly by the installation loop.
 */
void install_plan(
    const InstallPlan& plan,
    const std::filesystem::path& target_root
);
void install_plan(
    const InstallPlan& plan,
    const std::filesystem::path& target_root,
    const athena::core::PathLayout& paths
);

}
