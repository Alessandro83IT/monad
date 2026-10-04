#pragma once

#include "repository_index.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace athena::repository {

class ArchPackageMetadataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/*
 * Adapter for Arch Linux repository databases.
 *
 * The backend converts Arch repository metadata into Athena's native
 * Package model. Repository database transport is kept separate from
 * parsing so callers can explicitly download, cache, verify and then
 * load a database.
 */
class ArchRepository {

public:

    /*
     * Open an Arch repository database.
     *
     * The database is normally a .db.tar or .db.tar.zst archive
     * containing one <package>/desc file per package version.
     *
     * base_url is optional. When supplied, %FILENAME% is converted
     * into a complete package URL.
     */
    explicit ArchRepository(
        std::filesystem::path database,
        std::string base_url = ""
    );

    /*
     * Download an Arch repository database to the configured local path.
     *
     * The download is written to a temporary file first, so a failed or
     * interrupted transfer cannot corrupt an existing cached database.
     */
    void download(const std::string& url);

    /*
     * Download an Arch package described by the repository metadata.
     *
     * The package is written to the supplied destination through a
     * temporary file so an interrupted transfer cannot leave a package
     * that looks complete to the caller.
     */
    void download_package(
        const athena::package::Package& package,
        const std::filesystem::path& destination
    ) const;

    /*
     * Verify the SHA-256 digest recorded in the Arch repository metadata.
     */
    void verify_package_sha256(
        const athena::package::Package& package,
        const std::filesystem::path& package_path
    ) const;

    /*
     * Verify the detached OpenPGP signature of an Arch package.
     *
     * Arch package signatures are published alongside the package as
     * <package>.sig and are verified against the supplied trusted keyring.
     */
    void verify_package_signature(
        const athena::package::Package& package,
        const std::filesystem::path& package_path,
        const std::filesystem::path& keyring
    ) const;

    /*
     * Extract an Arch package into a temporary/staging directory.
     *
     * Archive members are validated before extraction so absolute paths
     * and parent-directory traversal cannot escape the destination.
     */
    void extract_package(
        const std::filesystem::path& package_path,
        const std::filesystem::path& destination
    ) const;

    /*
     * Read Arch .PKGINFO metadata from an extracted package and convert it
     * into Athena's native Package representation.
     */
    athena::package::Package load_package_metadata(
        const std::filesystem::path& extracted_package
    ) const;

    /*
     * Parse the Arch database and return the resulting Athena index.
     *
     * Parsing is explicit rather than implicit in the constructor so
     * callers can construct the backend before performing I/O.
     */
    void load();

    /*
     * Return the parsed package index.
     */
    const RepositoryIndex& index() const;

    /*
     * Find every available version of a package.
     */
    std::vector<athena::package::Package> find(
        const std::string& name
    ) const;

    /*
     * Check whether at least one package version is available.
     */
    bool contains(
        const std::string& name
    ) const;

private:

    std::filesystem::path database_;
    std::string base_url_;
    RepositoryIndex index_;
};

} // namespace athena::repository
