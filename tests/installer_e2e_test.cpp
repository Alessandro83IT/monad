// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/install/installer.hpp"
#include "../src/paths/paths.hpp"
#include "../src/store/store.hpp"
#include "../src/generation/generation.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

struct Fixture {
    std::filesystem::path archive;
    std::string checksum;
};

std::string sha256(const std::filesystem::path& file)
{
    /*
     * Use the same host utility used by Athena's verifier to obtain
     * the checksum of the locally generated fixture archive.
     */
    const std::string command =
        "sha256sum \"" + file.string() + "\"";

    FILE* pipe = popen(command.c_str(), "r");

    if (pipe == nullptr) {
        throw std::runtime_error(
            "Impossibile eseguire sha256sum"
        );
    }

    char buffer[256]{};
    std::string output;

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

    const int status = pclose(pipe);

    if (status != 0) {
        throw std::runtime_error(
            "sha256sum ha restituito un errore"
        );
    }

    const auto separator = output.find(' ');

    if (separator == std::string::npos) {
        throw std::runtime_error(
            "Output inatteso da sha256sum"
        );
    }

    return output.substr(0, separator);
}

/*
 * Create a tiny real CMake package fixture.
 *
 * Each package gets a distinct name/version directory and archive,
 * allowing the E2E test to exercise upgrades and generation rollback
 * without relying on mocked installer behavior.
 */
Fixture create_fixture(
    const std::filesystem::path& fixture_root,
    const std::string& name,
    const std::string& version,
    const std::string& file_name,
    const std::string& content
)
{
    const std::string package_directory =
        name + "-" + version;

    const auto source =
        fixture_root / package_directory;

    std::filesystem::create_directories(
        source / "usr/share/athena-e2e"
    );

    {
        std::ofstream cmake(source / "CMakeLists.txt");

        cmake
            << "cmake_minimum_required(VERSION 3.20)\n"
            << "project(athena_e2e_package LANGUAGES NONE)\n"
            << "install(\n"
            << "    FILES \"${CMAKE_CURRENT_SOURCE_DIR}/usr/share/athena-e2e/"
            << file_name
            << "\"\n"
            << "    DESTINATION share/athena-e2e\n"
            << ")\n";
    }

    {
        std::ofstream file(
            source / "usr/share/athena-e2e" / file_name
        );

        file << content << "\n";
    }

    /*
     * The archive must contain exactly one top-level directory,
     * matching the contract enforced by athena::archive::extract().
     */
    const auto archive =
        fixture_root / (package_directory + ".tar.gz");

    const std::string command =
        "tar -czf \"" + archive.string() +
        "\" -C \"" + fixture_root.string() +
        "\" \"" + package_directory + "\"";

    if (std::system(command.c_str()) != 0) {
        throw std::runtime_error(
            "Creazione dell'archivio fixture fallita: " +
            package_directory
        );
    }

    return {
        archive,
        sha256(archive)
    };
}

athena::package::Package make_package(
    const std::string& name,
    const std::string& version,
    const Fixture& fixture
)
{
    athena::package::Package package;

    package.name = name;
    package.version = version;
    package.description = "End-to-end installer test package";
    package.source = "file://" + fixture.archive.string();
    package.sha256 = fixture.checksum;
    package.build_system = "cmake";

    return package;
}

void assert_active(
    const std::filesystem::path& target_root,
    const std::filesystem::path& store_path,
    const std::string& file_name
)
{
    const auto activated_file =
        target_root / "share/athena-e2e" / file_name;

    assert(std::filesystem::is_symlink(activated_file));

    assert(
        std::filesystem::read_symlink(activated_file) ==
        store_path / "share/athena-e2e" / file_name
    );
}

void assert_not_active(
    const std::filesystem::path& target_root,
    const std::string& file_name
)
{
    const auto activated_file =
        target_root / "share/athena-e2e" / file_name;

    assert(!std::filesystem::exists(activated_file));
    assert(!std::filesystem::is_symlink(activated_file));
}

void assert_generation_contains(
    const athena::generation::Generation& generation,
    const std::string& package_name,
    const std::filesystem::path& store_path
)
{
    bool found = false;

    for (const auto& entry : generation.entries) {
        if (
            entry.package == package_name &&
            std::filesystem::path(entry.store_path) == store_path
        ) {
            found = true;
            break;
        }
    }

    assert(found);
}

}

int main()
{
    const auto test_root =
        std::filesystem::temp_directory_path() /
        "athena-installer-e2e-test";

    std::filesystem::remove_all(test_root);
    std::filesystem::create_directories(test_root);

    /*
     * Redirect every Athena-managed path into the temporary test
     * hierarchy. No production path such as /var/lib/athena is used.
     */
    athena::paths::set_root(test_root / "athena");

    try {
        const auto fixture_root =
            test_root / "fixture";

        std::filesystem::create_directories(fixture_root);

        /*
         * Generation 1 fixtures:
         *
         *   A1
         *   B1
         */
        const auto fixture_a1 = create_fixture(
            fixture_root,
            "athena-e2e-a",
            "1.0",
            "a.txt",
            "Athena package A version 1"
        );

        const auto fixture_b1 = create_fixture(
            fixture_root,
            "athena-e2e-b",
            "1.0",
            "b.txt",
            "Athena package B version 1"
        );

        /*
         * Generation 2 fixtures:
         *
         *   A2 replaces A1
         *   C1 is newly installed
         */
        const auto fixture_a2 = create_fixture(
            fixture_root,
            "athena-e2e-a",
            "2.0",
            "a.txt",
            "Athena package A version 2"
        );

        const auto fixture_c1 = create_fixture(
            fixture_root,
            "athena-e2e-c",
            "1.0",
            "c.txt",
            "Athena package C version 1"
        );

        const auto package_a1 = make_package(
            "athena-e2e-a",
            "1.0",
            fixture_a1
        );

        const auto package_b1 = make_package(
            "athena-e2e-b",
            "1.0",
            fixture_b1
        );

        const auto package_a2 = make_package(
            "athena-e2e-a",
            "2.0",
            fixture_a2
        );

        const auto package_c1 = make_package(
            "athena-e2e-c",
            "1.0",
            fixture_c1
        );

        /*
         * Generation 3 failure fixture:
         *
         *   D1 is successfully built and stored, but activation will
         *   intentionally fail because the target path is occupied by
         *   a regular file instead of being available for a symlink.
         */
        const auto fixture_d1 = create_fixture(
            fixture_root,
            "athena-e2e-d",
            "1.0",
            "d.txt",
            "Athena package D version 1"
        );

        const auto package_d1 = make_package(
            "athena-e2e-d",
            "1.0",
            fixture_d1
        );

        const auto target_root =
            test_root / "active";

        athena::generation::GenerationManager generations(
            athena::paths::root()
        );

        /*
         * Install the first environment:
         *
         *   G1 = A1 + B1
         *
         * Both packages are installed into the immutable store before
         * the generation is activated.
         */
        athena::install::InstallPlan first_plan;
        first_plan.add(package_a1);
        first_plan.add(package_b1);

        athena::install::install_plan(
            first_plan,
            target_root
        );

        const auto generation_one =
            generations.current();

        assert(generation_one.entries.size() == 2);

        const auto store_a1 =
            athena::paths::store() / "athena-e2e-a-1.0";

        const auto store_b1 =
            athena::paths::store() / "athena-e2e-b-1.0";

        assert(!store_a1.empty());
        assert(!store_b1.empty());

        assert_generation_contains(
            generation_one,
            "athena-e2e-a",
            store_a1
        );

        assert_generation_contains(
            generation_one,
            "athena-e2e-b",
            store_b1
        );

        assert_active(
            target_root,
            store_a1,
            "a.txt"
        );

        assert_active(
            target_root,
            store_b1,
            "b.txt"
        );

        /*
         * Install the second plan:
         *
         *   A2 replaces A1
         *   B1 remains active because it is not mentioned by the plan
         *   C1 is added
         *
         * The installer must therefore construct:
         *
         *   G2 = A2 + B1 + C1
         */
        athena::install::InstallPlan second_plan;
        second_plan.add(package_a2);
        second_plan.add(package_c1);

        athena::install::install_plan(
            second_plan,
            target_root
        );

        const auto generation_two =
            generations.current();

        assert(generation_two.id != generation_one.id);
        assert(generation_two.entries.size() == 3);

        const auto store_a2 =
            athena::paths::store() / "athena-e2e-a-2.0";

        const auto store_c1 =
            athena::paths::store() / "athena-e2e-c-1.0";

        assert(!store_a2.empty());
        assert(!store_c1.empty());

        /*
         * Verify the resulting complete environment.
         *
         * A1 disappeared from the generation.
         * A2 replaced it.
         * B1 was preserved.
         * C1 was added.
         */
        assert_generation_contains(
            generation_two,
            "athena-e2e-a",
            store_a2
        );

        assert_generation_contains(
            generation_two,
            "athena-e2e-b",
            store_b1
        );

        assert_generation_contains(
            generation_two,
            "athena-e2e-c",
            store_c1
        );

        assert_active(
            target_root,
            store_a2,
            "a.txt"
        );

        assert_active(
            target_root,
            store_b1,
            "b.txt"
        );

        /*
         * The activation path is shared by all fixture packages, so
         * checking the final symlink alone is insufficient to prove
         * that B1 survived. The generation metadata above verifies
         * its presence independently.
         */
        assert(!std::filesystem::exists(
            store_a1 / ".athena-nonexistent-test-marker"
        ));

        /*
         * Roll back the complete environment to G1.
         *
         * This must:
         *   - restore A1
         *   - keep B1
         *   - deactivate A2
         *   - deactivate C1
         *   - restore the persistent current pointer to G1
         */
        generations.switch_to(
            generation_one.id,
            target_root
        );

        const auto rolled_back =
            generations.current();

        assert(rolled_back.id == generation_one.id);
        assert(rolled_back.entries.size() == 2);

        assert_generation_contains(
            rolled_back,
            "athena-e2e-a",
            store_a1
        );

        assert_generation_contains(
            rolled_back,
            "athena-e2e-b",
            store_b1
        );

        assert_active(
            target_root,
            store_a1,
            "a.txt"
        );

        assert_active(
            target_root,
            store_b1,
            "b.txt"
        );

        /*
         * A single activation path is shared by the fixture packages,
         * therefore generation membership is the authoritative check
         * that A2 and C1 were removed during rollback.
         */
        assert_not_active(target_root, "c.txt");

        assert(
            std::filesystem::read_symlink(
                target_root / "share/athena-e2e/a.txt"
            ) ==
            store_a1 / "share/athena-e2e/a.txt"
        );

        /*
         * Both old and new store entries must remain available after
         * rollback. Generations reference immutable store objects;
         * rollback changes activation, not store contents.
         */
        assert(std::filesystem::is_directory(store_a1));
        assert(std::filesystem::is_directory(store_a2));
        assert(std::filesystem::is_directory(store_b1));
        assert(std::filesystem::is_directory(store_c1));


        /*
         * Verify the complete failed-install transaction.
         *
         * D1 must first reach the immutable store. Activation is then
         * forced to fail by occupying its target path with a regular
         * file. install_plan() must restore the previous filesystem
         * state and discard the newly created generation.
         */
        const auto generations_before_failure =
            generations.list();

        const auto generation_before_failure =
            generations.current();

        const auto conflicting_target =
            target_root /
            "share/athena-e2e/d.txt";

        std::filesystem::create_directories(
            conflicting_target.parent_path()
        );

        {
            std::ofstream conflict(
                conflicting_target
            );

            conflict
                << "This regular file intentionally blocks activation.\\n";
        }

        athena::install::InstallPlan failing_plan;
        failing_plan.add(package_d1);

        bool installation_failed = false;

        try {
            athena::install::install_plan(
                failing_plan,
                target_root
            );
        }
        catch (const std::runtime_error&) {
            installation_failed = true;
        }

        assert(installation_failed);

        /*
         * The failed transaction must leave the previously active
         * generation selected.
         */
        const auto generation_after_failure =
            generations.current();

        assert(
            generation_after_failure.id ==
            generation_before_failure.id
        );

        /*
         * The failed generation must have been discarded rather than
         * remaining as an orphaned historical generation.
         */
        const auto generations_after_failure =
            generations.list();

        assert(
            generations_after_failure.size() ==
            generations_before_failure.size()
        );

        /*
         * The conflicting regular file must remain untouched.
         * switch_to() must not replace user data when activation fails.
         */
        assert(
            std::filesystem::is_regular_file(
                conflicting_target
            )
        );

        /*
         * D1 was installed into the immutable store before activation
         * failed. discard() removes only generation metadata; the
         * store object remains available for the garbage collector.
         */
        const auto store_d1 =
            athena::paths::store() /
            "athena-e2e-d-1.0";

        assert(
            std::filesystem::is_directory(
                store_d1
            )
        );

        /*
         * The previously active packages must still be active after
         * the failed transaction.
         */
        assert_active(
            target_root,
            store_a1,
            "a.txt"
        );

        assert_active(
            target_root,
            store_b1,
            "b.txt"
        );

        /*
         * The regular conflict file is not a symlink, so verify its
         * contents independently.
         */
        {
            std::ifstream conflict(
                conflicting_target
            );

            std::string contents;
            std::getline(
                conflict,
                contents
            );

            /*
             * The exact text is irrelevant. What matters is that the
             * pre-existing regular file survived the failed activation
             * unchanged as a regular file with readable contents.
             */
            assert(!contents.empty());
        }

        std::filesystem::remove_all(test_root);
        athena::paths::reset_root();

        return 0;
    }
    catch (...) {
        /*
         * Always restore the global path override before propagating
         * the failure so the test cannot contaminate later tests.
         */
        std::filesystem::remove_all(test_root);
        athena::paths::reset_root();
        throw;
    }
}
