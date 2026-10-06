// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cmake_backend.hpp"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace athena::build {

/*
 * Configure and compile a CMake-based package.
 *
 * Athena uses an out-of-source build:
 *
 *     cmake -S <source> -B <build>
 *     cmake --build <build>
 *
 * This keeps generated CMake files and compilation artifacts
 * completely separate from the package source tree.
 */
void CMakeBackend::build(
    const std::filesystem::path& source_directory,
    const std::filesystem::path& build_directory
)
{
    /*
     * Start from a clean build directory.
     *
     * This prevents configuration files or artifacts from an
     * earlier build from affecting the current package build.
     */
    std::filesystem::remove_all(build_directory);
    std::filesystem::create_directories(build_directory);

    /*
     * Configure the package using CMake.
     *
     * /usr is used as the installation prefix because the final
     * package layout is intended to follow the normal Linux
     * filesystem hierarchy.
     */
    const std::string configure_command =
        "cmake "
        "-S \"" + source_directory.string() +
        "\" "
        "-B \"" + build_directory.string() +
        "\" "
        "-DCMAKE_BUILD_TYPE=Release "
        "-DCMAKE_INSTALL_PREFIX=/usr";

    /*
     * Execute the CMake configuration step.
     */
    const int configure_result =
        std::system(configure_command.c_str());

    /*
     * A non-zero exit status means that CMake configuration failed.
     */
    if (configure_result != 0) {
        throw std::runtime_error(
            "Configurazione CMake fallita in: " +
            source_directory.string()
        );
    }

    /*
     * Compile the package using the generated CMake build tree.
     */
    const std::string build_command =
        "cmake --build \"" +
        build_directory.string() +
        "\"";

    /*
     * Execute the compilation step.
     */
    const int build_result =
        std::system(build_command.c_str());

    /*
     * A non-zero exit status means that compilation failed.
     */
    if (build_result != 0) {
        throw std::runtime_error(
            "Compilazione CMake fallita in: " +
            source_directory.string()
        );
    }
}

/*
 * Install a CMake-based package into a staging directory.
 *
 * The package is never installed directly into the live filesystem.
 *
 * DESTDIR redirects paths such as:
 *
 *     /usr/bin/example
 *
 * to:
 *
 *     <staging>/usr/bin/example
 */
void CMakeBackend::install(
    const std::filesystem::path& source_directory,
    const std::filesystem::path& build_directory,
    const std::filesystem::path& staging_directory
)
{
    /*
     * The source directory is not required during the CMake
     * installation step because the build directory already
     * contains the generated installation information.
     */
    (void)source_directory;

    /*
     * Start with an empty staging directory.
     */
    std::filesystem::remove_all(staging_directory);
    std::filesystem::create_directories(staging_directory);

    /*
     * Install the package into the staging tree.
     *
     * CMake uses /usr as the package prefix while DESTDIR
     * redirects the resulting filesystem tree into staging.
     */
    const std::string command =
        "DESTDIR=\"" +
        staging_directory.string() +
        "\" cmake --install \"" +
        build_directory.string() +
        "\" --prefix /usr";

    /*
     * Execute the installation step.
     */
    const int result =
        std::system(command.c_str());

    /*
     * Report installation failures to the caller.
     */
    if (result != 0) {
        throw std::runtime_error(
            "Installazione CMake fallita nella build directory: " +
            build_directory.string()
        );
    }
}

}
