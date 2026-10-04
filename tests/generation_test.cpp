#include "../src/generation/generation.hpp"
#include "../src/store/manifest.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

int main()
{
    /*
     * Use an isolated temporary state directory so the generation
     * tests never modify Athena's real system state.
     */
    const auto test_root =
        std::filesystem::temp_directory_path() /
        "athena-generation-test";

    std::filesystem::remove_all(test_root);

    /*
     * The generation manager owns only generation metadata.
     * Package files themselves remain in the immutable store.
     */
    athena::generation::GenerationManager manager(
        test_root
    );

    /*
     * The first initialization must create Generation 0.
     */
    const auto initial =
        manager.initialize();

    assert(initial.id == 0);
    assert(initial.entries.empty());

    /*
     * Generation 0 must become the current generation.
     */
    const auto current =
        manager.current();

    assert(current.id == 0);
    assert(current.entries.empty());

    /*
     * Create a new generation referring to two immutable store
     * entries. The generation stores references, not file copies.
     */
    const std::vector<athena::generation::GenerationEntry> entries{
        {
            "pkg-a",
            "/athena/store/pkg-a-1.0"
        },
        {
            "pkg-b",
            "/athena/store/pkg-b-2.0"
        }
    };

    const auto generation_one =
        manager.create(entries);

    assert(generation_one.id == 1);
    assert(generation_one.entries == entries);

    /*
     * Creating a generation must not automatically change the
     * current generation.
     */
    assert(manager.current().id == 0);

    /*
     * A generation is immutable. Reading it must return exactly the
     * entries originally persisted for that generation.
     */
    const auto loaded =
        manager.get(1);

    assert(loaded.id == 1);
    assert(loaded.entries == entries);

    /*
     * Generation 0 must remain intact and independently readable.
     */
    const auto restored_initial =
        manager.get(0);

    assert(restored_initial.id == 0);
    assert(restored_initial.entries.empty());

    /*
     * Creating a generation must never change the current generation.
     */
    assert(manager.current().id == 0);

    /*
     * Verify that switching generations also materializes the
     * selected store entries into the target filesystem.
     *
     * Generation switching is deliberately tested with real
     * temporary store directories so the test exercises the
     * integration between generations and the activation layer.
     */
    const auto switch_root =
        test_root / "switch";

    const auto store_a =
        switch_root / "store-a";

    const auto store_b =
        switch_root / "store-b";

    const auto switch_target =
        switch_root / "target";

    std::filesystem::create_directories(
        store_a / "usr/bin"
    );

    std::filesystem::create_directories(
        store_b / "usr/bin"
    );

    {
        std::ofstream file(
            store_a / "usr/bin/package-a"
        );

        assert(file);
        file << "package A\n";
    }

    {
        std::ofstream file(
            store_b / "usr/bin/package-b"
        );

        assert(file);
        file << "package B\n";
    }

    athena::store::write_manifest(
        store_a,
        {
            {"usr/bin/package-a"}
        }
    );

    athena::store::write_manifest(
        store_b,
        {
            {"usr/bin/package-b"}
        }
    );

    const std::vector<athena::generation::GenerationEntry>
        switch_entries_a{
            {
                "package-a",
                store_a.string()
            }
        };

    const std::vector<athena::generation::GenerationEntry>
        switch_entries_b{
            {
                "package-b",
                store_b.string()
            }
        };

    const auto generation_two =
        manager.create(switch_entries_a);

    const auto generation_three =
        manager.create(switch_entries_b);

    manager.switch_to(
        generation_two.id,
        switch_target
    );

    assert(manager.current().id == generation_two.id);

    assert(
        std::filesystem::is_symlink(
            switch_target / "usr/bin/package-a"
        )
    );

    manager.switch_to(
        generation_three.id,
        switch_target
    );

    assert(manager.current().id == generation_three.id);

    /*
     * Package A must have been removed while package B must now
     * represent the active generation.
     */
    assert(
        !std::filesystem::exists(
            switch_target / "usr/bin/package-a"
        )
    );

    assert(
        std::filesystem::is_symlink(
            switch_target / "usr/bin/package-b"
        )
    );

    /*
     * Verify transactional rollback of a generation switch.
     *
     * Generation B deliberately conflicts with a pre-existing
     * regular file. The old generation must be restored completely
     * and the current-generation pointer must remain unchanged.
     */
    const auto rollback_root =
        switch_root / "rollback";

    const auto rollback_store_a =
        rollback_root / "store-a";

    const auto rollback_store_b =
        rollback_root / "store-b";

    const auto rollback_target =
        rollback_root / "target";

    std::filesystem::create_directories(
        rollback_store_a / "usr/bin"
    );

    std::filesystem::create_directories(
        rollback_store_b / "usr/bin"
    );

    {
        std::ofstream file(
            rollback_store_a / "usr/bin/rollback-a"
        );

        assert(file);
        file << "rollback A\n";
    }

    {
        std::ofstream file(
            rollback_store_b / "usr/bin/rollback-b"
        );

        assert(file);
        file << "rollback B\n";
    }

    athena::store::write_manifest(
        rollback_store_a,
        {
            {"usr/bin/rollback-a"}
        }
    );

    athena::store::write_manifest(
        rollback_store_b,
        {
            {"usr/bin/rollback-b"}
        }
    );

    /*
     * This regular file intentionally occupies the target path that
     * Generation B wants to activate. Activation must therefore fail
     * before modifying that object.
     */
    const auto conflicting_file =
        rollback_target / "usr/bin/rollback-b";

    std::filesystem::create_directories(
        conflicting_file.parent_path()
    );

    {
        std::ofstream file(conflicting_file);

        assert(file);
        file << "pre-existing file\n";
    }

    const auto rollback_generation_a =
        manager.create(
            {
                {
                    "rollback-a",
                    rollback_store_a.string()
                }
            }
        );

    const auto rollback_generation_b =
        manager.create(
            {
                {
                    "rollback-b",
                    rollback_store_b.string()
                }
            }
        );

    /*
     * Establish Generation A as the active state.
     */
    manager.switch_to(
        rollback_generation_a.id,
        rollback_target
    );

    assert(
        manager.current().id ==
        rollback_generation_a.id
    );

    assert(
        std::filesystem::is_symlink(
            rollback_target / "usr/bin/rollback-a"
        )
    );

    /*
     * Switching to B must fail because rollback-b already exists as
     * a regular file. The old generation must be restored.
     */
    bool switch_failed = false;

    try {
        manager.switch_to(
            rollback_generation_b.id,
            rollback_target
        );
    }
    catch (const std::runtime_error&) {
        switch_failed = true;
    }

    assert(switch_failed);

    /*
     * The persistent current generation must still be A.
     */
    assert(
        manager.current().id ==
        rollback_generation_a.id
    );

    /*
     * The old package must have been restored.
     */
    assert(
        std::filesystem::is_symlink(
            rollback_target / "usr/bin/rollback-a"
        )
    );

    /*
     * The conflicting pre-existing object must never have been
     * removed or replaced by the failed switch.
     */
    assert(
        std::filesystem::is_regular_file(
            conflicting_file
        )
    );

    /*
     * The failed target generation must not have left its package
     * activated.
     */
    assert(
        !std::filesystem::is_symlink(
            rollback_target / "usr/bin/rollback-b"
        )
    );

    /*
     * Verify that switch_to() preserves entries shared by both
     * generations and replaces only entries whose store reference
     * actually changes.
     *
     * This models a package upgrade:
     *
     *     package-a -> store-a-v1
     *     package-a -> store-a-v2
     *
     * while package-b remains untouched and package-c is newly added.
     */
    const auto transition_root =
        test_root / "transition";

    const auto transition_a_v1 =
        transition_root / "store-a-v1";

    const auto transition_a_v2 =
        transition_root / "store-a-v2";

    const auto transition_b =
        transition_root / "store-b";

    const auto transition_c =
        transition_root / "store-c";

    const auto transition_target =
        transition_root / "target";

    for (const auto& store : {
        transition_a_v1,
        transition_a_v2,
        transition_b,
        transition_c
    }) {
        std::filesystem::create_directories(
            store / "usr/bin"
        );
    }

    {
        std::ofstream file(
            transition_a_v1 / "usr/bin/package-a"
        );

        assert(file);
        file << "package A version 1\n";
    }

    {
        std::ofstream file(
            transition_a_v2 / "usr/bin/package-a"
        );

        assert(file);
        file << "package A version 2\n";
    }

    {
        std::ofstream file(
            transition_b / "usr/bin/package-b"
        );

        assert(file);
        file << "package B\n";
    }

    {
        std::ofstream file(
            transition_c / "usr/bin/package-c"
        );

        assert(file);
        file << "package C\n";
    }

    athena::store::write_manifest(
        transition_a_v1,
        {
            {"usr/bin/package-a"}
        }
    );

    athena::store::write_manifest(
        transition_a_v2,
        {
            {"usr/bin/package-a"}
        }
    );

    athena::store::write_manifest(
        transition_b,
        {
            {"usr/bin/package-b"}
        }
    );

    athena::store::write_manifest(
        transition_c,
        {
            {"usr/bin/package-c"}
        }
    );

    const auto transition_generation_a =
        manager.create(
            {
                {
                    "package-a",
                    transition_a_v1.string()
                },
                {
                    "package-b",
                    transition_b.string()
                }
            }
        );

    const auto transition_generation_b =
        manager.create(
            {
                {
                    "package-a",
                    transition_a_v2.string()
                },
                {
                    "package-b",
                    transition_b.string()
                },
                {
                    "package-c",
                    transition_c.string()
                }
            }
        );

    manager.switch_to(
        transition_generation_a.id,
        transition_target
    );

    assert(
        manager.current().id ==
        transition_generation_a.id
    );

    assert(
        std::filesystem::is_symlink(
            transition_target / "usr/bin/package-a"
        )
    );

    assert(
        std::filesystem::is_symlink(
            transition_target / "usr/bin/package-b"
        )
    );

    manager.switch_to(
        transition_generation_b.id,
        transition_target
    );

    assert(
        manager.current().id ==
        transition_generation_b.id
    );

    /*
     * package-a must now resolve to version 2.
     */
    const auto package_a_link =
        std::filesystem::read_symlink(
            transition_target / "usr/bin/package-a"
        );

    assert(
        std::filesystem::absolute(
            transition_target /
            "usr/bin" /
            package_a_link
        ) ==
        std::filesystem::absolute(
            transition_a_v2 /
            "usr/bin/package-a"
        )
    );

    /*
     * package-b belongs to both generations and therefore remains
     * active throughout the transition.
     */
    assert(
        std::filesystem::is_symlink(
            transition_target / "usr/bin/package-b"
        )
    );

    /*
     * package-c is newly introduced by the target generation.
     */
    assert(
        std::filesystem::is_symlink(
            transition_target / "usr/bin/package-c"
        )
    );

    /*
     * The old version of package-a must no longer be referenced by
     * the target filesystem.
     */
    const auto package_a_old_target =
        transition_target / "usr/bin/package-a";

    const auto package_a_old_link =
        std::filesystem::read_symlink(
            package_a_old_target
        );

    assert(
        std::filesystem::absolute(
            package_a_old_target.parent_path() /
            package_a_old_link
        ) !=
        std::filesystem::absolute(
            transition_a_v1 /
            "usr/bin/package-a"
        )
    );


    /*
     * Verify explicit disposal of a generation that belongs to a
     * failed or otherwise abandoned transaction.
     *
     * Discarding a non-current generation removes only its metadata.
     * The immutable store is deliberately left untouched and remains
     * under the control of the garbage collector.
     */
    const auto disposable_generation =
        manager.create(
            {
                {
                    "disposable-package",
                    transition_b.string()
                }
            }
        );

    assert(
        manager.get(disposable_generation.id).id ==
        disposable_generation.id
    );

    const auto disposable_directory =
        test_root /
        "generations" /
        std::to_string(disposable_generation.id);

    assert(
        std::filesystem::exists(
            disposable_directory
        )
    );

    manager.discard(
        disposable_generation.id
    );

    assert(
        !std::filesystem::exists(
            disposable_directory
        )
    );

    /*
     * Discarding generation metadata must never remove immutable
     * store objects. Store lifetime is managed independently by GC.
     */
    assert(
        std::filesystem::is_directory(
            transition_b
        )
    );

    /*
     * Reading a discarded generation must fail because its immutable
     * metadata no longer exists.
     */
    bool discarded_generation_missing = false;

    try {
        manager.get(
            disposable_generation.id
        );
    }
    catch (const std::runtime_error&) {
        discarded_generation_missing = true;
    }

    assert(discarded_generation_missing);

    /*
     * The current generation must never be discardable. This protects
     * the persistent current pointer from referring to missing
     * generation metadata.
     */
    bool current_discard_failed = false;

    try {
        manager.discard(
            manager.current().id
        );
    }
    catch (const std::runtime_error&) {
        current_discard_failed = true;
    }

    assert(current_discard_failed);

    /*
     * The current generation must remain valid after the rejected
     * discard operation.
     */
    const auto preserved_current =
        manager.current();

    assert(
        preserved_current.id ==
        transition_generation_b.id
    );

    /*
     * Historical generations remain untouched by discard().
     */
    assert(
        manager.get(
            transition_generation_a.id
        ).id ==
        transition_generation_a.id
    );

    assert(
        manager.get(
            transition_generation_b.id
        ).id ==
        transition_generation_b.id
    );

    std::filesystem::remove_all(test_root);

    std::cout
        << "Generation tests passed.\n";

    return 0;
}
