// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "temporary_directory.hpp"

#include "../src/activation/activation.hpp"
#include "../src/store/manifest.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

int main()
{
    /*
     * Use a dedicated temporary directory so the activation test
     * never touches the real filesystem outside its test area.
     */
    athena::test::TemporaryDirectory fixture("athena-activation-test");
    const auto test_root = fixture.path();

    const auto store_directory =
        test_root / "store";

    const auto target_root =
        test_root / "target";

    /*
     * The reserved fixture root is empty and remains present until
     * RAII cleanup, so no other invocation can reuse its name.
     */

    std::filesystem::create_directories(
        store_directory / "usr/bin"
    );

    /*
     * Create a representative file inside the immutable store.
     */
    const auto source =
        store_directory / "usr/bin/athena-test";

    {
        std::ofstream file(source);
        assert(file);
        file << "Athena activation test\n";
    }

    /*
     * The activation layer operates from the package manifest,
     * therefore the test must provide the same metadata it would
     * receive from a real store entry.
     */
    athena::store::ManifestEntry entry;
    entry.path = "usr/bin/athena-test";

    const std::vector<athena::store::ManifestEntry> manifest{
        entry
    };

    athena::store::write_manifest(
        store_directory,
        manifest
    );

    /*
     * Activate the package into the target root.
     */
    athena::activation::activate(
        store_directory,
        target_root
    );

    const auto target =
        target_root / "usr/bin/athena-test";

    /*
     * The target must now exist as a symbolic link.
     */
    assert(
        std::filesystem::is_symlink(target)
    );

    /*
     * Resolve the link and verify that it points to the exact
     * file belonging to the expected store entry.
     */
    const auto linked_target =
        std::filesystem::absolute(
            target.parent_path() /
            std::filesystem::read_symlink(target)
        );

    assert(
        linked_target ==
        std::filesystem::absolute(source)
    );

    /*
     * Deactivate the package again.
     */
    athena::activation::deactivate(
        store_directory,
        target_root
    );

    /*
     * The activation link must have been removed while the
     * immutable store file itself must remain untouched.
     */
    assert(
        !std::filesystem::exists(target)
    );

    assert(
        std::filesystem::exists(source)
    );

    /*
     * Verify transactional activation.
     *
     * The manifest deliberately contains the same target twice.
     * The first symlink is created successfully, while the second
     * creation fails because the target now exists.
     *
     * A failed activation must roll back every symlink created
     * before the filesystem error occurred.
     */
    const auto rollback_root =
        test_root / "rollback";

    const auto rollback_store =
        rollback_root / "store";

    const auto rollback_target =
        rollback_root / "target";

    std::filesystem::create_directories(
        rollback_store / "usr/bin"
    );

    const auto rollback_source =
        rollback_store / "usr/bin/first";

    {
        std::ofstream file(rollback_source);
        assert(file);
        file << "rollback test\n";
    }

    /*
     * Create an unrelated object that already exists in the target
     * root. A failed activation must never remove pre-existing
     * filesystem objects during its rollback.
     */
    const auto preexisting =
        rollback_target / "usr/preexisting";

    std::filesystem::create_directories(
        preexisting.parent_path()
    );

    {
        std::ofstream file(preexisting);
        assert(file);
        file << "pre-existing object\n";
    }

    /*
     * Both manifest entries resolve to the same target. This makes
     * the second symlink creation fail deterministically after the
     * first one has already been created.
     */
    const std::vector<athena::store::ManifestEntry>
        rollback_manifest{
            {"usr/bin/first"},
            {"usr/bin/first"}
        };

    athena::store::write_manifest(
        rollback_store,
        rollback_manifest
    );

    bool activation_failed = false;

    try {
        athena::activation::activate(
            rollback_store,
            rollback_target
        );
    }
    catch (const std::runtime_error&) {
        activation_failed = true;
    }

    /*
     * The activation must fail on the second attempt to create
     * the same target.
     */
    assert(activation_failed);

    /*
     * The first symlink must not survive the failed transaction.
     */
    assert(
        !std::filesystem::exists(
            rollback_target / "usr/bin/first"
        )
    );

    /*
     * The immutable store entry must remain untouched.
     */
    assert(
        std::filesystem::exists(rollback_source)
    );

    /*
     * The pre-existing object must also survive the rollback.
     */
    assert(
        std::filesystem::is_regular_file(preexisting)
    );

    std::cout
        << "Activation tests passed.\n";

    return 0;
}
