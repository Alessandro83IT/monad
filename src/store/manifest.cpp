// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "manifest.hpp"

#include <fstream>
#include <stdexcept>
#include <toml++/toml.hpp>

namespace athena::store {

namespace {
void validate_manifest_path(const std::string& value)
{
    const std::filesystem::path path(value);
    if (value.empty() || path.is_absolute() || path.has_root_name() ||
        path.generic_string() != value || path.begin() == path.end() ||
        *path.begin() == ".athena")
        throw std::runtime_error("Percorso non valido nel manifest: " + value);
    for (const auto& component : path)
        if (component == "." || component == "..")
            throw std::runtime_error("Percorso non valido nel manifest: " + value);
}
}

/*
 * Write the manifest of an installed package.
 *
 * The manifest contains paths relative to the package's store
 * directory. This allows Athena to determine exactly which files
 * belong to a package when removal is implemented later.
 */
void write_manifest(
    const std::filesystem::path& store_directory,
    const std::vector<ManifestEntry>& entries
)
{
    /*
     * Create the directory used for Athena-specific metadata.
     */
    const std::filesystem::path metadata_directory =
        store_directory / ".athena";

    std::filesystem::create_directories(metadata_directory);

    /*
     * Store the manifest alongside the package metadata.
     */
    const std::filesystem::path manifest_file =
        metadata_directory / "manifest.toml";

    std::ofstream output(manifest_file);

    if (!output) {
        throw std::runtime_error(
            "Impossibile creare il manifest: " +
            manifest_file.string()
        );
    }

    /*
     * Write one TOML table for every installed file.
     *
     * Example:
     *
     *     [[files]]
     *     path = "bin/example"
     *
     *     [[files]]
     *     path = "share/example/data"
     */
    toml::table document;
    toml::array files;
    for (const auto& entry : entries) {
        validate_manifest_path(entry.path);
        toml::table file;
        file.insert("path", entry.path);
        files.push_back(std::move(file));
    }
    document.insert("files", std::move(files));
    output << document;
    if (!output) throw std::runtime_error("Errore di scrittura del manifest: " + manifest_file.string());
}

/*
 * Read the manifest associated with an installed package.
 */
std::vector<ManifestEntry> read_manifest(
    const std::filesystem::path& store_directory
)
{
    /*
     * Locate the manifest file.
     */
    const std::filesystem::path manifest_file =
        store_directory /
        ".athena" /
        "manifest.toml";

    try {

        /*
         * Parse the TOML manifest.
         */
        const auto table =
            toml::parse_file(manifest_file.string());

        std::vector<ManifestEntry> entries;

        /*
         * Locate the array of [[files]] tables.
         */
        const auto* files =
            table["files"].as_array();

        if (files == nullptr) {
            throw std::runtime_error(
                "Il manifest non contiene la sezione 'files': " +
                manifest_file.string()
            );
        }

        /*
         * Convert each TOML entry into an Athena ManifestEntry.
         */
        for (const auto& item : *files) {

            const auto* file_table =
                item.as_table();

            if (file_table == nullptr) {
                throw std::runtime_error(
                    "Voce non valida nel manifest: " +
                    manifest_file.string()
                );
            }

            const auto path =
                (*file_table)["path"].value<std::string>();

            if (!path.has_value()) {
                throw std::runtime_error(
                    "Voce del manifest priva di 'path': " +
                    manifest_file.string()
                );
            }

            validate_manifest_path(*path);
            entries.push_back(ManifestEntry{*path});
        }

        return entries;
    }
    catch (const toml::parse_error& error) {

        /*
         * Hide the TOML-specific exception from higher-level modules
         * and provide an Athena-specific error message.
         */
        throw std::runtime_error(
            "Impossibile leggere il manifest '" +
            store_directory.string() + "': " +
            error.what()
        );
    }
}

}
