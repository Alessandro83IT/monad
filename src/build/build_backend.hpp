// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

/*
 * Common interface for all Athena package build systems.
 *
 * Every build backend must implement the two operations required
 * by the package installation pipeline:
 *
 *     1. build   - configure and compile the package;
 *     2. install - install the compiled package into a staging tree.
 *
 * Athena provides the filesystem locations required by the backend.
 * The backend therefore does not need to know how Athena's global
 * filesystem hierarchy is organized.
 */
namespace athena::build {

/*
 * Abstract interface implemented by each build-system backend.
 *
 * Examples of concrete implementations are:
 *
 *     AutotoolsBackend
 *     CMakeBackend
 *     MesonBackend
 *     MakeBackend
 *
 * Each backend is responsible only for the commands specific to
 * its build system.
 */
class BuildBackend {

public:

    /*
     * Virtual destructor required for safe destruction through
     * a pointer or reference to the base class.
     */
    virtual ~BuildBackend() = default;

    /*
     * Configure and build the package.
     *
     * source_directory contains the extracted package source.
     *
     * build_directory is a dedicated directory where the backend
     * may place generated build files and compilation artifacts.
     *
     * Backends that build directly inside the source tree may not
     * need this directory, but it is provided as part of the common
     * interface so that out-of-source build systems such as CMake
     * can use it naturally.
     */
    virtual void build(
        const std::filesystem::path& source_directory,
        const std::filesystem::path& build_directory
    ) = 0;

    /*
     * Install the previously built package into a staging tree.
     *
     * The backend must never install directly into the live system.
     *
     * staging_directory acts as a temporary filesystem root whose
     * contents will later be transferred into the Athena package
     * store.
     */
    virtual void install(
        const std::filesystem::path& source_directory,
        const std::filesystem::path& build_directory,
        const std::filesystem::path& staging_directory
    ) = 0;
};

}
