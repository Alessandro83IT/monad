#pragma once

#include "../package/package.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace athena::store {

/*
 * Metadata associated with an installed Athena package.
 *
 * Metadata is stored inside the package's .athena directory
 * and allows Athena to inspect an installed package without
 * having to re-read its original package definition.
 */
struct Metadata {

    // Package name.
    std::string name;

    // Installed package version.
    std::string version;

    // Source archive URL used to build the package.
    std::string source;

    // SHA-256 checksum of the source archive.
    std::string sha256;

    // Build system used to create the package.
    std::string build_system;

    /*
     * Serialized dependency expressions belonging to the package.
     *
     * Keeping the expressions in the same metadata document makes
     * .athena/metadata.toml the authoritative description of the
     * installed package, including the information required later
     * to validate package removal.
     */
    std::vector<std::string> dependencies{};

    /*
     * Semantic capabilities and features exposed by the installed
     * package. The field is optional in the serialized metadata so
     * older store entries remain readable.
     */
    athena::package::PackageProvides provides{};
};

/*
 * Write package metadata to the store.
 *
 * The metadata is stored as:
 *
 *     <store-directory>/.athena/metadata.toml
 */
void write_metadata(
    const std::filesystem::path& store_directory,
    const Metadata& metadata
);

/*
 * Read package metadata from an installed store entry.
 *
 * The returned Metadata object can then be used by the CLI or
 * other parts of Athena to inspect the installed package.
 */
Metadata read_metadata(
    const std::filesystem::path& store_directory
);

}
