// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autotools_backend.hpp"

#include "../process/run.hpp"

#include <stdexcept>

namespace athena::build {

/*
 * Build an Autotools-based package.
 *
 * The standard Autotools sequence used by Athena is:
 *
 *     ./configure --prefix=/usr
 *     make
 *
 * The package is configured with /usr as its installation prefix.
 *
 * Autotools currently builds directly inside the source directory,
 * so build_directory is not used yet. It is nevertheless part of
 * the common backend interface so that other build systems, such as
 * CMake, can use a dedicated out-of-source build directory.
 */
void AutotoolsBackend::build(
    const std::filesystem::path& source_directory,
    const std::filesystem::path& build_directory
)
{
    /*
     * Autotools does not currently use the separate build directory.
     *
     * Keep the parameter in the implementation because it is part
     * of the common BuildBackend interface.
     */
    (void)build_directory;

    /*
     * Enter the package source directory, configure the package,
     * then compile it.
     */
    const int configure_result = athena::process::run(
        {"./configure", "--prefix=/usr"}, source_directory
    );
    const int result = configure_result == 0
        ? athena::process::run({"make"}, source_directory)
        : configure_result;

    /*
     * A non-zero exit status means that configuration or compilation
     * failed.
     */
    if (result != 0) {
        throw std::runtime_error(
            "Compilazione Autotools fallita in: " +
            source_directory.string()
        );
    }
}

/*
 * Install an Autotools-based package into a staging directory.
 *
 * DESTDIR is used to redirect the installation away from the live
 * filesystem.
 *
 * For example:
 *
 *     /usr/bin/hello
 *
 * becomes:
 *
 *     <staging>/usr/bin/hello
 */
void AutotoolsBackend::install(
    const std::filesystem::path& source_directory,
    const std::filesystem::path& build_directory,
    const std::filesystem::path& staging_directory
)
{
    /*
     * Autotools currently does not need the separate build directory.
     */
    (void)build_directory;

    /*
     * Start with an empty staging directory so that files from an
     * earlier build cannot accidentally become part of this package.
     */
    std::filesystem::remove_all(staging_directory);
    std::filesystem::create_directories(staging_directory);

    /*
     * Install the already-built package using DESTDIR.
     */
    const int result = athena::process::run(
        {"env", "DESTDIR=" + std::filesystem::absolute(staging_directory).string(),
         "make", "install"}, source_directory
    );

    /*
     * Report installation failures to the caller.
     */
    if (result != 0) {
        throw std::runtime_error(
            "Installazione Autotools fallita in: " +
            source_directory.string()
        );
    }
}

}
