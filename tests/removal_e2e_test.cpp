// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "temporary_directory.hpp"

#include "../src/generation/generation.hpp"
#include "../src/install/remover.hpp"
#include "../src/paths/paths.hpp"
#include "../src/store/manifest.hpp"
#include "../src/store/metadata.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void create_package_store(
    const std::filesystem::path& store_path,
    const std::string& name,
    const std::string& version,
    const std::vector<std::string>& dependencies,
    const std::string& file_name
)
{
    std::filesystem::create_directories(store_path / "bin");

    {
        std::ofstream file(store_path / "bin" / file_name);
        if (!file) {
            throw std::runtime_error(
                "Cannot create test package file"
            );
        }

        file << "athena removal e2e test\n";
    }

    athena::store::write_metadata(
        store_path,
        athena::store::Metadata{
            name,
            version,
            "",
            "",
            "",
            dependencies
        }
    );

    athena::store::write_manifest(
        store_path,
        {
            { "bin/" + file_name }
        }
    );
}

} // namespace

int main()
{
    athena::test::TemporaryDirectory fixture("athena-removal-e2e");
    const auto root = fixture.path();

    const auto target_root = root / "target";

    try {
        athena::paths::set_root(root / "state");
        athena::paths::initialize();

        const auto store_root = athena::paths::store();

        const auto store_a = store_root / "package-a-1.0";
        const auto store_b = store_root / "package-b-1.0";

        /*
         * A depends on B.
         *
         * Therefore:
         *   - removing A is valid;
         *   - removing B while A remains installed is invalid.
         */
        create_package_store(
            store_b,
            "package-b",
            "1.0",
            {},
            "b"
        );

        create_package_store(
            store_a,
            "package-a",
            "1.0",
            { "package-b" },
            "a"
        );

        athena::generation::GenerationManager generations(
            athena::paths::root()
        );

        generations.initialize();

        const auto generation = generations.create(
            {
                { "package-b", store_b.string() },
                { "package-a", store_a.string() }
            }
        );

        generations.switch_to(
            generation.id,
            target_root
        );

        expect(
            generations.current().id == generation.id,
            "Initial generation was not activated"
        );

        expect(
            std::filesystem::is_symlink(target_root / "bin/b"),
            "Package B was not activated"
        );

        expect(
            std::filesystem::is_symlink(target_root / "bin/a"),
            "Package A was not activated"
        );

        /*
         * Remove A.
         *
         * A new generation must contain only B. The immutable Store
         * object for A must remain available for historical generations.
         */
        athena::install::remove_packages(
            { "package-a" },
            target_root
        );

        const auto after_a = generations.current();

        expect(
            after_a.id != generation.id,
            "Removal did not create a new generation"
        );

        expect(
            after_a.entries.size() == 1 &&
            after_a.entries.front().package == "package-b",
            "Unexpected generation after removing A"
        );

        expect(
            !std::filesystem::exists(target_root / "bin/a"),
            "Package A remained active"
        );

        expect(
            std::filesystem::is_symlink(target_root / "bin/b"),
            "Package B was unexpectedly removed"
        );

        expect(
            std::filesystem::exists(store_a),
            "Removed Store object was deleted prematurely"
        );

        /*
         * Roll back to the historical generation.
         *
         * This proves that removal created a new generation instead of
         * destructively deleting the Store object belonging to A.
         */
        generations.switch_to(
            generation.id,
            target_root
        );

        expect(
            std::filesystem::is_symlink(target_root / "bin/a"),
            "Historical generation could not restore A"
        );

        /*
         * B cannot be removed while A depends on it.
         *
         * The failed transaction must leave both the generation pointer
         * and the active filesystem unchanged.
         */
        bool rejected = false;

        try {
            athena::install::remove_packages(
                { "package-b" },
                target_root
            );
        }
        catch (const std::exception&) {
            rejected = true;
        }

        expect(
            rejected,
            "Removal of a required dependency was not rejected"
        );

        expect(
            generations.current().id == generation.id,
            "Failed removal changed the current generation"
        );

        expect(
            std::filesystem::is_symlink(target_root / "bin/a") &&
            std::filesystem::is_symlink(target_root / "bin/b"),
            "Failed removal modified the active filesystem"
        );

        athena::paths::reset_root();

        std::cout << "removal_e2e_test passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        athena::paths::reset_root();

        std::cerr
            << "removal_e2e_test failed: "
            << error.what()
            << '\n';

        return 1;
    }
}
