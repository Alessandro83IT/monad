// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "temporary_directory.hpp"

#include "../src/dependency/parser.hpp"
#include "../src/dependency/serializer.hpp"
#include "../src/package/package.hpp"
#include "../src/paths/paths.hpp"
#include "../src/store/metadata.hpp"
#include "../src/store/store.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

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
