// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "build_backend.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace athena::build {

/*
 * Create the build backend corresponding to the requested
 * build-system name.
 *
 * The dispatcher hides the concrete backend implementation from
 * the rest of Athena.
 *
 * For example:
 *
 *     "autotools" -> AutotoolsBackend
 *     "cmake"     -> CMakeBackend
 *     "meson"     -> MesonBackend
 *
 * An exception is thrown when the requested build system is not
 * supported by Athena.
 */
std::unique_ptr<BuildBackend> create_backend(
    const std::string& build_system
);

/*
 * Build a package from its extracted source directory.
 *
 * source_directory contains the extracted package source.
 *
 * build_directory is a dedicated directory where the selected
 * backend may place generated build files and compilation artifacts.
 *
 * The build system is selected from the package definition.
 * The builder acts as a dispatcher and delegates the actual
 * compilation to the selected build backend.
 *
 * The package is not installed into the live system during this
 * operation.
 */
void build(
    const std::filesystem::path& source_directory,
    const std::filesystem::path& build_directory,
    const std::string& build_system
);

/*
 * Install a previously built package into a staging directory.
 *
 * build_directory contains the build artifacts produced by the
 * selected backend.
 *
 * staging_directory acts as a temporary filesystem root.
 * This allows Athena to collect the files belonging to the
 * package before placing them in the package store.
 *
 * Files are therefore never installed directly into the running
 * system by this function.
 */
void install(
    const std::filesystem::path& source_directory,
    const std::filesystem::path& build_directory,
    const std::filesystem::path& staging_directory,
    const std::string& build_system
);

}
