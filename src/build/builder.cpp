// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "builder.hpp"
#include "autotools_backend.hpp"
#include "cmake_backend.hpp"

#include <memory>
#include <stdexcept>
#include <string>

namespace athena::build {

/*
 * Select the build backend corresponding to the requested
 * build-system name.
 *
 * The dispatcher is responsible only for selecting the backend.
 * The actual build and installation logic is implemented by
 * the concrete backend classes.
 *
 * For example:
 *
 *     "autotools" -> AutotoolsBackend
 *     "cmake"     -> CMakeBackend
 *     "meson"     -> MesonBackend
 *
 * Additional backends can therefore be added without moving
 * their implementation into this file.
 */
std::unique_ptr<BuildBackend> create_backend(
    const std::string& build_system
)
{
    /*
     * Select the Autotools backend.
     */
    if (build_system == "autotools") {
        return std::make_unique<AutotoolsBackend>();
    }

    /*
     * Select the CMake backend.
     */
    if (build_system == "cmake") {
        return std::make_unique<CMakeBackend>();
    }

    /*
     * The requested build system is not implemented yet.
     */
    throw std::runtime_error(
        "Sistema di build non supportato: " +
        build_system
    );
}

/*
 * Build a package using the requested build-system backend.
 *
 * The dispatcher selects the appropriate backend and delegates
 * the actual compilation to it.
 */
void build(
    const std::filesystem::path& source_directory,
    const std::filesystem::path& build_directory,
    const std::string& build_system
)
{
    /*
     * Create the backend corresponding to the package's
     * declared build system.
     */
    const auto backend =
        create_backend(build_system);

    /*
     * Delegate the build operation to the selected backend.
     */
    backend->build(
        source_directory,
        build_directory
    );
}

/*
 * Install a previously built package into a staging directory.
 *
 * The dispatcher again selects the appropriate backend and
 * delegates the installation operation.
 */
void install(
    const std::filesystem::path& source_directory,
    const std::filesystem::path& build_directory,
    const std::filesystem::path& staging_directory,
    const std::string& build_system
)
{
    /*
     * Create the backend corresponding to the package's
     * declared build system.
     */
    const auto backend =
        create_backend(build_system);

    /*
     * Delegate the installation operation to the selected backend.
     */
    backend->install(
        source_directory,
        build_directory,
        staging_directory
    );
}

}
