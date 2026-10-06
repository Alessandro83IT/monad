// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "archive.hpp"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace athena::archive {

/*
 * Extract a package source archive.
 *
 * The current implementation uses the system tar utility and
 * supports gzip-compressed tar archives (.tar.gz).
 *
 * The archive is extracted into a temporary directory inside the
 * requested destination. This prevents files and directories from
 * previous package extractions from affecting the detection of the
 * current package's top-level directory.
 *
 * After extraction, Athena expects the archive to contain exactly
 * one top-level directory. That directory is returned as the source
 * directory used by the build system.
 */
std::filesystem::path extract(
    const std::filesystem::path& archive,
    const std::filesystem::path& destination
)
{
    /*
     * Ensure that the extraction destination exists.
     */
    std::filesystem::create_directories(destination);

    /*
     * Create a unique temporary extraction directory.
     *
     * The directory is created inside the destination so that the
     * archive module does not need to know anything about Athena's
     * global filesystem layout.
     */
    const std::filesystem::path extraction_directory =
        destination /
        ("." + archive.stem().stem().string() + "-extract");

    /*
     * Remove a possible directory left by an interrupted previous
     * extraction and recreate it from scratch.
     */
    std::filesystem::remove_all(extraction_directory);
    std::filesystem::create_directories(extraction_directory);

    /*
     * Extract the archive into the isolated extraction directory.
     *
     * -x    extract files
     * -z    decompress gzip data
     * -f    specify the archive file
     * -C    change to the extraction directory before extraction
     */
    const std::string command =
        "tar -xzf \"" + archive.string() +
        "\" -C \"" + extraction_directory.string() + "\"";

    const int result = std::system(command.c_str());

    if (result != 0) {
        std::filesystem::remove_all(extraction_directory);

        throw std::runtime_error(
            "Estrazione fallita: " + archive.string()
        );
    }

    /*
     * Find the top-level directory created by the extraction.
     *
     * Because the archive was extracted into an isolated directory,
     * only entries belonging to the current archive are considered.
     */
    std::filesystem::path extracted_directory;

    for (const auto& entry :
         std::filesystem::directory_iterator(extraction_directory)) {

        if (entry.is_directory()) {

            /*
             * More than one top-level directory means that the
             * archive does not have the source layout expected by
             * Athena.
             */
            if (!extracted_directory.empty()) {
                std::filesystem::remove_all(extraction_directory);

                throw std::runtime_error(
                    "L'archivio contiene più directory principali: " +
                    archive.string()
                );
            }

            extracted_directory = entry.path();
        }
    }

    /*
     * An archive without a top-level directory is not compatible
     * with the current source extraction model.
     */
    if (extracted_directory.empty()) {
        std::filesystem::remove_all(extraction_directory);

        throw std::runtime_error(
            "Nessuna directory principale trovata dopo l'estrazione: " +
            archive.string()
        );
    }

    /*
     * Return the isolated package source directory.
     *
     * The extraction directory itself is deliberately kept because
     * the returned source directory depends on it.
     */
    return extracted_directory;
}

}
