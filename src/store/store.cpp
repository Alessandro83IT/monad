#include "store.hpp"
#include "../dependency/serializer.hpp"

#include "../paths/paths.hpp"
#include "../version/version.hpp"
#include "manifest.hpp"
#include "metadata.hpp"

#include <filesystem>
#include <stdexcept>
#include <vector>
#include <algorithm>

namespace athena::store {

/*
 * Install a staged package into the Athena package store.
 *
 * The package is copied from the staging tree into a dedicated
 * directory under /var/lib/athena.
 *
 * The current implementation uses the package name and version
 * as the store directory name. A future implementation will use
 * content-derived hashes to make store paths immutable and
 * collision-resistant.
 */
std::filesystem::path install(
    const std::filesystem::path& staging_directory,
    const athena::package::Package& package
)
{
    /*
     * Construct the destination path for the package.
     *
     * Example:
     *
     *     /var/lib/athena/store/hello-2.12
     */
    const std::filesystem::path destination =
        athena::paths::store() /
        (package.name + "-" + package.version);

    if (std::filesystem::exists(destination)) {
        /*
         * A complete store entry is immutable and must never be
         * overwritten. An older/incomplete entry without a manifest,
         * however, is not a valid store entry and can be replaced by
         * the freshly built package.
         */
        const std::filesystem::path manifest_file =
            destination /
            ".athena" /
            "manifest.toml";

        if (std::filesystem::exists(manifest_file)) {
            throw std::runtime_error(
                "Il pacchetto è già presente nello store: " +
                destination.string()
            );
        }

        std::filesystem::remove_all(destination);
    }

    /*
     * The staging directory contains a DESTDIR-style installation.
     *
     * For a package configured with:
     *
     *     --prefix=/usr
     *
     * the resulting files are expected under:
     *
     *     <staging>/usr/
     */
    const std::filesystem::path installed_root =
        staging_directory / "usr";

    if (!std::filesystem::is_directory(installed_root)) {
        throw std::runtime_error(
            "Directory di installazione non trovata: " +
            installed_root.string()
        );
    }

    /*
     * Create the package's store directory before copying its files.
     */
    std::filesystem::create_directories(destination);

    /*
     * Keep track of every regular file installed into the package
     * store. Paths are recorded relative to the package root.
     */
    std::vector<ManifestEntry> manifest_entries;

    /*
     * Recursively walk through the staged installation tree.
     *
     * Directories themselves are not recorded in the manifest.
     * Only regular files are recorded because directories can be
     * recreated or removed automatically when their contents change.
     */
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(installed_root)) {

        if (!entry.is_regular_file()) {
            continue;
        }

        /*
         * Determine the file path relative to the staged /usr root.
         *
         * Example:
         *
         *     /staging/usr/bin/foo
         *
         * becomes:
         *
         *     bin/foo
         */
        const std::filesystem::path relative_path =
            std::filesystem::relative(
                entry.path(),
                installed_root
            );

        /*
         * Copy the file into the package's store tree while
         * preserving its relative directory structure.
         */
        const std::filesystem::path destination_file =
            destination / relative_path;

        std::filesystem::create_directories(
            destination_file.parent_path()
        );

        std::filesystem::copy_file(
            entry.path(),
            destination_file
        );

        /*
         * Record the relative path in the package manifest.
         */
        manifest_entries.push_back(
            ManifestEntry{
                relative_path.generic_string()
            }
        );
    }

    /*
     * Record the information needed to identify and inspect the
     * installed package.
     *
     * Metadata is stored separately from the package files under
     * the .athena directory.
     */
    /*
     * Persist the dependency expressions together with the package
     * metadata. This makes the installed store entry self-contained:
     * future operations such as remove can inspect the package's
     * dependency requirements without consulting the repository.
     */
    std::vector<std::string> dependencies;

    dependencies.reserve(
        package.dependencies.size()
    );

    for (const auto& dependency : package.dependencies) {
        dependencies.push_back(
            athena::dependency::serialize_dependency(
                dependency
            )
        );
    }

    const Metadata metadata{
        package.name,
        package.version,
        package.source,
        package.sha256,
        package.build_system,
        std::move(dependencies)
    };

    write_metadata(
        destination,
        metadata
    );

    /*
     * Store the list of files belonging to this package.
     *
     * This manifest will later allow the remove command to determine
     * exactly which files belong to the package.
     */
    write_manifest(
        destination,
        manifest_entries
    );

    return destination;
}

/*
 * Find an exact package/version entry in the immutable store.
 *
 * This lookup is used by the installer to reuse an immutable store
 * entry that has already been built. It deliberately checks Athena
 * metadata rather than trusting the directory name alone.
 */
std::optional<std::filesystem::path> find(
    const std::string& package_name,
    const std::string& package_version
)
{
    const std::filesystem::path destination =
        athena::paths::store() /
        (package_name + "-" + package_version);

    /*
     * A missing exact entry is a normal condition during installation.
     * Invalid metadata, on the other hand, remains an error and must not
     * be silently treated as a cache miss.
     */
    if (!std::filesystem::is_directory(destination)) {
        return std::nullopt;
    }

    const std::filesystem::path metadata_file =
        destination /
        ".athena" /
        "metadata.toml";

    if (!std::filesystem::exists(metadata_file)) {
        /*
         * An incomplete store directory is not a valid immutable
         * object. Treat it as a cache miss so the installer can
         * rebuild and replace it transactionally.
         */
        return std::nullopt;
    }

    /*
     * A store entry is reusable only when it contains the manifest
     * required by the activation layer. Older or incomplete entries
     * must fall back to a fresh build instead of failing later during
     * generation activation.
     */
    const std::filesystem::path manifest_file =
        destination /
        ".athena" /
        "manifest.toml";

    if (!std::filesystem::exists(manifest_file)) {
        return std::nullopt;
    }

    const auto metadata =
        athena::store::read_metadata(destination);

    if (metadata.name != package_name ||
        metadata.version != package_version) {
        throw std::runtime_error(
            "Metadata dello store non corrispondenti al pacchetto richiesto: " +
            destination.string()
        );
    }

    return destination;
}

/*
 * Find an installed package by name.
 *
 * Multiple versions may exist in the immutable store.
 * The newest version is selected using Athena's version comparison.
 */
std::filesystem::path find(
    const std::string& package_name
)
{
    const std::filesystem::path store_directory =
        athena::paths::store();

    if (!std::filesystem::exists(store_directory)) {
        throw std::runtime_error(
            "Store non trovato: " +
            store_directory.string()
        );
    }

    const std::string expected_prefix =
        package_name + "-";

    std::filesystem::path best_match;
    std::string best_version;

    for (const auto& entry :
         std::filesystem::directory_iterator(store_directory)) {

        if (!entry.is_directory()) {
            continue;
        }

        const std::string directory_name =
            entry.path().filename().string();

        if (directory_name.rfind(expected_prefix, 0) != 0) {
            continue;
        }

        const std::filesystem::path metadata_file =
            entry.path() /
            ".athena" /
            "metadata.toml";

        if (!std::filesystem::exists(metadata_file)) {
            continue;
        }

        const auto metadata =
            athena::store::read_metadata(
                entry.path()
            );

        if (metadata.name != package_name) {
            continue;
        }

        if (best_match.empty() ||
            athena::version::greater(
                metadata.version,
                best_version
            )) {

            best_match = entry.path();
            best_version = metadata.version;
        }
    }

    if (!best_match.empty()) {
        return best_match;
    }

    throw std::runtime_error(
        "Pacchetto non installato: " +
        package_name
    );
}

/*
 * List all installed package store entries.
 *
 * Only directories containing Athena metadata are considered
 * installed packages.
 */
std::vector<std::filesystem::path> list()
{
    const std::filesystem::path store_directory =
        athena::paths::store();

    if (!std::filesystem::exists(store_directory)) {
        throw std::runtime_error(
            "Store non trovato: " +
            store_directory.string()
        );
    }

    std::vector<std::filesystem::path> packages;

    for (const auto& entry :
         std::filesystem::directory_iterator(store_directory)) {

        if (!entry.is_directory()) {
            continue;
        }

        const std::filesystem::path metadata_file =
            entry.path() /
            ".athena" /
            "metadata.toml";

        if (!std::filesystem::exists(metadata_file)) {
            continue;
        }

        packages.push_back(entry.path());
    }

    /*
     * Keep the output deterministic.
     *
     * This is important because filesystem directory iteration
     * does not guarantee a particular order.
     */
    std::sort(
        packages.begin(),
        packages.end()
    );

    return packages;
}

}
