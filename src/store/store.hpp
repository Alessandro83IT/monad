// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../package/package.hpp"
#include "../../include/athena/core/path_layout.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace athena::store {

// A conservative, read-only observation of a legacy store staging entry.
// Presence does not prove that the entry is abandoned or safe to remove.
struct RecoveryCandidate {
    std::filesystem::path path;
    std::filesystem::file_type type;
};

// Enumerate names used by legacy store publication without changing them.
// Callers must preserve every returned candidate until provenance is reviewed.
std::vector<RecoveryCandidate> inspect_recovery_candidates(
    const std::filesystem::path& store_directory
);

/*
 * Install a staged package into the Athena package store.
 *
 * The package is copied from the staging directory into a dedicated
 * store directory and its metadata is recorded alongside the files.
 *
 * The current store uses:
 *
 *     <package-name>-<package-version>
 *
 * as the directory name.
 *
 * This naming scheme is temporary. The long-term design is a
 * hash-based store inspired by functional package managers.
 */
std::filesystem::path install(
    const std::filesystem::path& staging_directory,
    const athena::package::Package& package
);
std::filesystem::path install(
    const std::filesystem::path& staging_directory,
    const athena::package::Package& package,
    const std::filesystem::path& store_directory
);

/*
 * Find an installed package by name.
 *
 * The current implementation searches the store for a directory
 * whose name starts with "<package-name>-".
 *
 * The returned path points to the installed package tree.
 */
std::filesystem::path find(
    const std::string& package_name
);
std::filesystem::path find(
    const std::string& package_name,
    const std::filesystem::path& store_directory
);

/*
 * Find an exact package version in the immutable store.
 *
 * Unlike the name-only lookup, this function never selects another
 * version: it returns only the requested package/version entry.
 */
std::optional<std::filesystem::path> find(
    const std::string& package_name,
    const std::string& package_version
);
std::optional<std::filesystem::path> find(
    const std::string& package_name,
    const std::string& package_version,
    const std::filesystem::path& store_directory
);

/*
 * List all installed package store entries.
 *
 * The returned paths point to package directories under the Athena
 * store. Only directories containing valid Athena metadata are
 * included.
 */
std::vector<std::filesystem::path> list();
std::vector<std::filesystem::path> list(const std::filesystem::path& store_directory);

}
