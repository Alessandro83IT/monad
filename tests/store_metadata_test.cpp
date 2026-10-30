// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "temporary_directory.hpp"

#include "../src/dependency/parser.hpp"
#include "../src/dependency/serializer.hpp"
#include "../src/package/package.hpp"
#include "../src/paths/paths.hpp"
#include "../src/store/metadata.hpp"
#include "../src/store/manifest.hpp"
#include "../src/store/store.hpp"

#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <csignal>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main()
{
    athena::test::TemporaryDirectory fixture("athena-store-metadata-test");
    const auto test_root = fixture.path();

    /*
     * Keep the test isolated from the real Athena installation.
     * store::install() resolves its destination through paths::store().
     */
    athena::paths::set_root(test_root);
    athena::paths::initialize();

    try {

        /*
         * Build a realistic Package with dependency expressions.
         *
         * The test deliberately uses both a version constraint and
         * a logical OR expression so serialization is exercised through
         * the same code path used by the real installer.
         */
        athena::package::Package package{
            "metadata-test",
            "1.0",
            "Metadata persistence test package",
            "https://example.org/metadata-test-1.0.tar.xz",
            "test-sha256",
            "cmake",
            {
                athena::dependency::parse_dependency("zlib >= 1.2"),
                athena::dependency::parse_dependency(
                    "openssl OR libressl"
                )
            },
            {
                {"cmake", "build-system"},
                {"modern-cmake"}
            }
        };

        /*
         * Create the DESTDIR-style staging tree expected by Store.
         */
        const auto staging =
            test_root / "staging";

        std::filesystem::create_directories(
            staging / "usr" / "bin"
        );

        std::ofstream(staging / "usr" / "bin" / "metadata-test")
            << "test";

        /*
         * Install the package into the immutable store.
         *
         * This is the same public API used by Installer.
         */
        const auto store_path =
            athena::store::install(
                staging,
                package
            );

        require(
            std::filesystem::exists(
                store_path / ".athena" / "metadata.toml"
            ),
            "Il file metadata.toml non è stato creato."
        );

        // An existing (even incomplete) destination is user state. A failed
        // publication must preserve it and leave no private staging tree.
        const auto occupied = store_path.parent_path() / "occupied-1.0";
        std::filesystem::create_directories(occupied);
        std::ofstream(occupied / "sentinel") << "preserve";
        auto occupied_package = package;
        occupied_package.name = "occupied";
        bool collision_rejected = false;
        try { (void)athena::store::install(staging, occupied_package); }
        catch (const std::runtime_error&) { collision_rejected = true; }
        require(collision_rejected, "Existing store destination was not rejected");
        require(std::filesystem::exists(occupied / "sentinel"), "Existing store data was removed");
        for (const auto& item : std::filesystem::directory_iterator(occupied.parent_path()))
            require(!item.path().filename().string().starts_with(".monad-store-stage-"),
                    "Failed store publication leaked private staging data");

        // Inject an actual write failure after private staging has begun.
        const auto failed_staging = test_root / "copy-failure-staging";
        std::filesystem::create_directories(failed_staging / "usr" / "share");
        std::ofstream payload(failed_staging / "usr" / "share" / "large.bin", std::ios::binary);
        payload << std::string(64 * 1024, 'x');
        payload.close();
        auto failed_package = package;
        failed_package.name = "copy-failure-test";
        struct rlimit old_limit{};
        require(::getrlimit(RLIMIT_FSIZE, &old_limit) == 0, "Cannot inspect file-size limit for failure injection");
        struct sigaction old_action{}, ignore_action{};
        ignore_action.sa_handler = SIG_IGN;
        sigemptyset(&ignore_action.sa_mask);
        require(::sigaction(SIGXFSZ, &ignore_action, &old_action) == 0, "Cannot configure file-size failure injection");
        struct rlimit limited = old_limit;
        limited.rlim_cur = old_limit.rlim_max == RLIM_INFINITY ? 1024 : std::min<rlim_t>(1024, old_limit.rlim_max);
        const bool limit_set = ::setrlimit(RLIMIT_FSIZE, &limited) == 0;
        bool copy_failed = false;
        if (limit_set) {
            try { (void)athena::store::install(failed_staging, failed_package); }
            catch (const std::exception&) { copy_failed = true; }
        }
        const bool limit_restored = ::setrlimit(RLIMIT_FSIZE, &old_limit) == 0;
        const bool action_restored = ::sigaction(SIGXFSZ, &old_action, nullptr) == 0;
        require(limit_set && limit_restored && action_restored, "Could not run/restore file-size failure injection");
        require(copy_failed, "Store copy did not fail under injected file-size limit");
        require(!std::filesystem::exists(store_path.parent_path() / "copy-failure-test-1.0"),
                "Failed store copy published a partial destination");
        for (const auto& item : std::filesystem::directory_iterator(store_path.parent_path()))
            require(!item.path().filename().string().starts_with(".monad-store-stage-"),
                    "Failed store copy did not clean its private staging tree");

        // Model an abrupt publisher death with a private stage left behind.
        // Recovery is inspection only: even this known synthetic orphan is
        // reported and preserved, never automatically adopted or deleted.
        const auto orphan = store_path.parent_path() / ".monad-store-stage-crash-fixture";
        const pid_t child = ::fork();
        require(child >= 0, "Could not fork crash-recovery fixture");
        if (child == 0) {
            if (::mkdir(orphan.c_str(), 0700) != 0) _exit(90);
            const auto marker = orphan / "partial-copy";
            const int fd = ::open(marker.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
            if (fd < 0) _exit(91);
            constexpr char bytes[] = "interrupted";
            if (::write(fd, bytes, sizeof(bytes)) != static_cast<ssize_t>(sizeof(bytes))) _exit(92);
            ::close(fd);
            ::kill(::getpid(), SIGKILL);
            _exit(93);
        }
        int child_status = 0;
        require(::waitpid(child, &child_status, 0) == child && WIFSIGNALED(child_status) &&
                    WTERMSIG(child_status) == SIGKILL,
                "Crash fixture publisher did not terminate with SIGKILL");
        const auto candidates = athena::store::inspect_recovery_candidates(store_path.parent_path());
        require(candidates.size() == 1 && candidates.front().path == orphan &&
                    candidates.front().type == std::filesystem::file_type::directory,
                "Recovery inspection did not report the interrupted stage");
        require(std::filesystem::exists(orphan / "partial-copy"),
                "Recovery inspection changed the interrupted stage");

        /*
         * Inspect the serialized TOML directly.
         *
         * This prevents a broken writer and reader from accidentally
         * passing the test together.
         */
        std::ifstream metadata_file(
            store_path / ".athena" / "metadata.toml"
        );

        require(
            static_cast<bool>(metadata_file),
            "Impossibile aprire metadata.toml."
        );

        const std::string metadata{
            std::istreambuf_iterator<char>(metadata_file),
            std::istreambuf_iterator<char>()
        };

        require(
            metadata.find("dependencies") != std::string::npos,
            "Il campo dependencies manca dai metadata."
        );

        require(
            metadata.find("zlib >= 1.2") != std::string::npos,
            "La dipendenza zlib non è stata serializzata."
        );

        require(
            metadata.find("openssl OR libressl") != std::string::npos,
            "La dipendenza OR non è stata serializzata."
        );

        /*
         * Verify that semantic package capabilities and features are
         * persisted in the optional [provides] TOML table.
         */
        require(
            metadata.find("[provides]") != std::string::npos,
            "La sezione provides manca dai metadata."
        );

        require(
            metadata.find("cmake") != std::string::npos,
            "La capability cmake non è stata serializzata."
        );

        require(
            metadata.find("build-system") != std::string::npos,
            "La capability build-system non è stata serializzata."
        );

        require(
            metadata.find("modern-cmake") != std::string::npos,
            "La feature modern-cmake non è stata serializzata."
        );

        /*
         * Read the metadata back through the public Store API.
         */
        const auto restored =
            athena::store::read_metadata(store_path);

        require(
            restored.name == package.name,
            "Nome del pacchetto non preservato."
        );

        require(
            restored.version == package.version,
            "Versione del pacchetto non preservata."
        );

        require(
            restored.source == package.source,
            "Source non preservata."
        );

        require(
            restored.sha256 == package.sha256,
            "SHA-256 non preservato."
        );

        require(
            restored.build_system == package.build_system,
            "Build system non preservato."
        );

        require(
            restored.dependencies.size() == 2,
            "Numero di dipendenze ripristinate errato."
        );

        require(
            restored.dependencies[0] == "zlib >= 1.2",
            "Prima dipendenza ripristinata in modo errato."
        );

        require(
            restored.dependencies[1] == "openssl OR libressl",
            "Seconda dipendenza ripristinata in modo errato."
        );

        require(
            restored.provides.capabilities.size() == 2,
            "Numero di capability ripristinate errato."
        );

        require(
            restored.provides.capabilities[0] == "cmake",
            "Prima capability ripristinata in modo errato."
        );

        require(
            restored.provides.capabilities[1] == "build-system",
            "Seconda capability ripristinata in modo errato."
        );

        require(
            restored.provides.features.size() == 1,
            "Numero di feature ripristinate errato."
        );

        require(
            restored.provides.features[0] == "modern-cmake",
            "Feature ripristinata in modo errato."
        );

        /*
         * Verify backward compatibility: metadata files generated
         * before dependency persistence did not contain the field.
         * read_metadata() must therefore return an empty list.
         */
        const auto legacy_root =
            test_root / "legacy";

        std::filesystem::create_directories(
            legacy_root / ".athena"
        );

        std::ofstream legacy_file(
            legacy_root / ".athena" / "metadata.toml"
        );

        legacy_file
            << "name = \"legacy\"\n"
            << "version = \"1.0\"\n"
            << "source = \"legacy-source\"\n"
            << "sha256 = \"legacy-sha256\"\n"
            << "build_system = \"cmake\"\n";

        legacy_file.close();

        const auto legacy =
            athena::store::read_metadata(legacy_root);

        require(
            legacy.dependencies.empty(),
            "I metadata legacy devono avere dipendenze vuote."
        );

        // Staging links must not be followed into files outside the package.
        const auto external_secret = test_root / "outside-secret";
        std::ofstream(external_secret) << "must not enter the package store";
        const auto linked_staging = test_root / "linked-staging";
        std::filesystem::create_directories(linked_staging / "usr" / "bin");
        std::filesystem::create_symlink(external_secret, linked_staging / "usr" / "bin" / "leak");
        auto linked_package = package;
        linked_package.name = "linked-stage-test";
        bool rejected_link = false;
        try {
            (void)athena::store::install(linked_staging, linked_package);
        } catch (const std::runtime_error& error) {
            rejected_link = std::string(error.what()).find("collegamenti simbolici") != std::string::npos;
        }
        require(rejected_link, "Staging symlink was not rejected");
        require(std::filesystem::exists(external_secret), "External staging target was changed");
        require(!std::filesystem::exists(test_root / "var" / "lib" / "athena" / "store" / "linked-stage-test-1.0"),
                "Rejected staging tree left a partial store entry");

        const auto root_link_staging = test_root / "root-link-staging";
        std::filesystem::create_directories(root_link_staging);
        std::filesystem::create_directory_symlink(test_root / "staging" / "usr", root_link_staging / "usr");
        auto root_link_package = package;
        root_link_package.name = "linked-root-stage-test";
        bool rejected_root_link = false;
        try {
            (void)athena::store::install(root_link_staging, root_link_package);
        } catch (const std::runtime_error&) {
            rejected_root_link = true;
        }
        require(rejected_root_link, "Staging root symlink was not rejected");

        const auto manifest_fixture = test_root / "manifest-roundtrip";
        const std::string unusual_path = "share/quote\" and\nnewline";
        athena::store::write_manifest(manifest_fixture, {{unusual_path}});
        const auto unusual_entries = athena::store::read_manifest(manifest_fixture);
        require(unusual_entries.size() == 1 && unusual_entries.front().path == unusual_path,
                "Manifest TOML did not preserve an escaped filename");
        bool rejected_traversal = false;
        try {
            athena::store::write_manifest(manifest_fixture, {{"../outside"}});
        } catch (const std::runtime_error&) {
            rejected_traversal = true;
        }
        require(rejected_traversal, "Manifest writer accepted a traversal path");

        athena::paths::reset_root();

        std::cout
            << "store_metadata_test: OK\n";

        return 0;
    }
    catch (...) {
        athena::paths::reset_root();

        throw;
    }
}
