// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "build_backend.hpp"

namespace athena::build {

/*
 * Build backend for packages using the GNU Autotools build system.
 *
 * This backend encapsulates the commands required to configure,
 * compile and install an Autotools-based package.
 */
class AutotoolsBackend : public BuildBackend {

public:

    /*
     * Configure and compile the package.
     *
     * build_directory is part of the common BuildBackend interface.
     * Autotools currently builds directly inside the source tree,
     * so the directory is not used yet.
     */
    void build(
        const std::filesystem::path& source_directory,
        const std::filesystem::path& build_directory
    ) override;

    /*
     * Install the compiled package into a staging directory.
     */
    void install(
        const std::filesystem::path& source_directory,
        const std::filesystem::path& build_directory,
        const std::filesystem::path& staging_directory
    ) override;
};

}
