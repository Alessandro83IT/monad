// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

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
#include <cerrno>
#include <system_error>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <array>
#include <cerrno>
#include <sys/syscall.h>
#include <linux/fs.h>

namespace athena::store {

std::vector<RecoveryCandidate> inspect_recovery_candidates(
    const std::filesystem::path& store_directory)
{
    std::error_code error;
    const auto root_status = std::filesystem::symlink_status(store_directory, error);
    if (error == std::errc::no_such_file_or_directory) return {};
    if (error) throw std::system_error(error, "inspect store recovery root");
    if (std::filesystem::is_symlink(root_status) || !std::filesystem::is_directory(root_status))
        throw std::runtime_error("Store recovery root is not a real directory");

    std::vector<RecoveryCandidate> candidates;
    for (const auto& entry : std::filesystem::directory_iterator(store_directory)) {
        const auto name = entry.path().filename().string();
        if (!name.starts_with(".monad-store-stage-")) continue;
        const auto status = entry.symlink_status(error);
        if (error) throw std::system_error(error, "inspect store recovery candidate");
        candidates.push_back({entry.path(), status.type()});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        return left.path.filename().native() < right.path.filename().native();
    });
    return candidates;
}

namespace {
void copy_staged_file_no_follow(const std::filesystem::path& root,
                                const std::filesystem::path& relative,
                                const std::filesystem::path& output)
{
    int parent = ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (parent < 0) throw std::system_error(errno, std::generic_category(), "open staging root");
    auto close_fd = [](int fd) { if (fd >= 0) ::close(fd); };
    std::vector<std::string> components;
    for (const auto& part : relative) components.push_back(part.string());
    if (components.empty()) { close_fd(parent); throw std::runtime_error("Empty staging path"); }
    for (std::size_t i = 0; i + 1 < components.size(); ++i) {
        const int next = ::openat(parent, components[i].c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        const int saved = errno;
        close_fd(parent);
        if (next < 0) throw std::system_error(saved, std::generic_category(), "open staging directory without following links");
        parent = next;
    }
    const int source = ::openat(parent, components.back().c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    const int source_error = errno;
    close_fd(parent);
    if (source < 0) throw std::system_error(source_error, std::generic_category(), "open staged file without following links");
    struct stat status{};
    if (::fstat(source, &status) != 0) {
        const int error = errno;
        close_fd(source);
        throw std::system_error(error, std::generic_category(), "staged entry is not a regular file");
    }
    if (!S_ISREG(status.st_mode)) { close_fd(source); throw std::runtime_error("Staged entry is not a regular file"); }
    const int target = ::open(output.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (target < 0) {
        const int error = errno; close_fd(source);
        throw std::system_error(error, std::generic_category(), "create private store file");
    }
    std::array<char, 65536> buffer{};
    try {
        for (;;) {
            const ssize_t count = ::read(source, buffer.data(), buffer.size());
            if (count < 0) { if (errno == EINTR) continue; throw std::system_error(errno, std::generic_category(), "read staged file"); }
            if (count == 0) break;
            ssize_t offset = 0;
            while (offset < count) {
                const ssize_t written = ::write(target, buffer.data() + offset, static_cast<std::size_t>(count - offset));
                if (written < 0) { if (errno == EINTR) continue; throw std::system_error(errno, std::generic_category(), "write store file"); }
                offset += written;
            }
        }
        if (::fchmod(target, status.st_mode & 0777) != 0)
            throw std::system_error(errno, std::generic_category(), "set store file permissions");
    } catch (...) { close_fd(source); close_fd(target); throw; }
    close_fd(source); close_fd(target);
}
}

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
    return install(staging_directory, package, athena::paths::store());
}

std::filesystem::path install(
    const std::filesystem::path& staging_directory,
    const athena::package::Package& package,
    const std::filesystem::path& store_directory
)
{
    athena::package::validate_identity(package.name, package.version);
    /*
     * Construct the destination path for the package.
     *
     * Example:
     *
     *     /var/lib/athena/store/hello-2.12
     */
    const std::filesystem::path destination =
        store_directory /
        (package.name + "-" + package.version);

    std::filesystem::create_directories(store_directory);
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(store_directory)))
        throw std::runtime_error("La directory dello store non può essere un collegamento simbolico");
    if (std::filesystem::exists(destination))
        throw std::runtime_error("Destinazione store già esistente; recovery manuale conservativa richiesta: " + destination.string());

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

    const auto installed_root_status = std::filesystem::symlink_status(installed_root);
    if (std::filesystem::is_symlink(installed_root_status) ||
        !std::filesystem::is_directory(installed_root_status)) {
        throw std::runtime_error(
            "Directory di installazione non trovata: " +
            installed_root.string()
        );
    }

    // The legacy package store and manifest represent regular files only.
    // Reject links explicitly: directory_entry::is_regular_file follows a
    // symlink, which previously copied an arbitrary target into the store.
    for (const auto& entry : std::filesystem::recursive_directory_iterator(installed_root)) {
        const auto status = entry.symlink_status();
        if (std::filesystem::is_symlink(status))
            throw std::runtime_error("I collegamenti simbolici nello staging non sono supportati: " + entry.path().string());
        if (!std::filesystem::is_directory(status) && !std::filesystem::is_regular_file(status))
            throw std::runtime_error("Tipo di file non supportato nello staging: " + entry.path().string());
    }

    // Build a private sibling tree. Only a complete tree is published, and
    // the final rename is no-replace so a concurrent publisher cannot win a
    // check/rename race or have its object overwritten.
    std::string stage_pattern = (store_directory / ".monad-store-stage-XXXXXX").string();
    if (!::mkdtemp(stage_pattern.data()))
        throw std::system_error(errno, std::generic_category(), "create private store staging directory");
    const std::filesystem::path private_stage(stage_pattern);
    bool published = false;
    struct StageCleanup {
        const std::filesystem::path& path;
        bool& published;
        ~StageCleanup() { if (!published) { std::error_code ignored; std::filesystem::remove_all(path, ignored); } }
    } cleanup{private_stage, published};

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

        const auto status = entry.symlink_status();
        if (std::filesystem::is_symlink(status)) {
            throw std::runtime_error("Lo staging è cambiato durante la copia: collegamento simbolico " + entry.path().string());
        }
        if (!std::filesystem::is_regular_file(status)) {
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
        const std::filesystem::path relative_path = entry.path().lexically_relative(installed_root);

        /*
         * Copy the file into the package's store tree while
         * preserving its relative directory structure.
         */
        const std::filesystem::path destination_file =
            private_stage / relative_path;

        std::filesystem::create_directories(
            destination_file.parent_path()
        );

        copy_staged_file_no_follow(installed_root, relative_path, destination_file);

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
        std::move(dependencies),
        package.provides
    };

    write_metadata(
        private_stage,
        metadata
    );

    /*
     * Store the list of files belonging to this package.
     *
     * This manifest will later allow the remove command to determine
     * exactly which files belong to the package.
     */
    write_manifest(
        private_stage,
        manifest_entries
    );

    const long result = ::syscall(SYS_renameat2, AT_FDCWD, private_stage.c_str(),
                                  AT_FDCWD, destination.c_str(), RENAME_NOREPLACE);
    if (result != 0) {
        if (errno == EEXIST)
            throw std::runtime_error("Destinazione store già esistente durante la pubblicazione: " + destination.string());
        throw std::system_error(errno, std::generic_category(), "atomic store publication failed");
    }
    published = true;
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
    return find(package_name, package_version, athena::paths::store());
}

std::optional<std::filesystem::path> find(
    const std::string& package_name,
    const std::string& package_version,
    const std::filesystem::path& store_directory
)
{
    athena::package::validate_identity(package_name, package_version);
    const std::filesystem::path destination =
        store_directory /
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
    return find(package_name, athena::paths::store());
}

std::filesystem::path find(
    const std::string& package_name,
    const std::filesystem::path& store_directory
)
{
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
    return list(athena::paths::store());
}

std::vector<std::filesystem::path> list(const std::filesystem::path& store_directory)
{
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
