// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace athena::store {

/*
 * A single file installed by an Athena package.
 *
 * Paths are stored relative to the package's store directory.
 */
struct ManifestEntry {

    // Relative path of the installed file.
    std::string path;
};

/*
 * Write the manifest of an installed package.
 *
 * The manifest is stored as:
 *
 *     <store-directory>/.athena/manifest.toml
 */
void write_manifest(
    const std::filesystem::path& store_directory,
    const std::vector<ManifestEntry>& entries
);

/*
 * Read the manifest associated with an installed package.
 */
std::vector<ManifestEntry> read_manifest(
    const std::filesystem::path& store_directory
);

}
