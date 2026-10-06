// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "build_backend.hpp"

namespace athena::build {

/*
 * Build backend for packages using the CMake build system.
 *
 * CMake normally supports out-of-source builds, meaning that
 * generated build files and compilation artifacts are stored
 * separately from the package source tree.
 */
class CMakeBackend : public BuildBackend {

public:

    /*
     * Configure and compile the package.
     *
     * source_directory contains the package source.
     *
     * build_directory is the dedicated out-of-source CMake
     * build directory.
     */
    void build(
        const std::filesystem::path& source_directory,
        const std::filesystem::path& build_directory
    ) override;

    /*
     * Install the compiled package into a staging directory.
     *
     * CMake's install mechanism is redirected through DESTDIR
     * so that files are not installed directly into the live system.
     */
    void install(
        const std::filesystem::path& source_directory,
        const std::filesystem::path& build_directory,
        const std::filesystem::path& staging_directory
    ) override;
};

}
