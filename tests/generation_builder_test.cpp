// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/install/generation_builder.hpp"

#include <cassert>
#include <filesystem>
#include <vector>

int main()
{
    const athena::generation::Generation current{
        7,
        {
            {"package-a", "/athena/store/package-a-1.0"},
            {"package-b", "/athena/store/package-b-1.0"},
            {"package-c", "/athena/store/package-c-1.0"}
        }
    };

    athena::package::Package upgraded;
    upgraded.name = "package-b";
    upgraded.version = "2.0";

    athena::package::Package added;
    added.name = "package-d";
    added.version = "1.0";

    const auto result =
        athena::install::build_target_entries(
            current,
            {
                {
                    upgraded,
                    "/athena/store/package-b-2.0"
                },
                {
                    added,
                    "/athena/store/package-d-1.0"
                }
            }
        );

    /*
     * The current generation represents the complete environment.
     * Applying a plan must therefore preserve A and C, replace B,
     * and append the newly introduced D.
     */
    assert(result.size() == 4);

    const athena::generation::GenerationEntry expected_a{
        "package-a",
        "/athena/store/package-a-1.0"
    };

    const athena::generation::GenerationEntry expected_b{
        "package-b",
        "/athena/store/package-b-2.0"
    };

    /*
     * Removal is applied to the current generation, before any upgrade
     * represented by the separate installation plan above.
     */
    const athena::generation::GenerationEntry expected_current_b{
        "package-b",
        "/athena/store/package-b-1.0"
    };

    const athena::generation::GenerationEntry expected_c{
        "package-c",
        "/athena/store/package-c-1.0"
    };

    const athena::generation::GenerationEntry expected_d{
        "package-d",
        "/athena/store/package-d-1.0"
    };

    assert(result[0] == expected_a);
    assert(result[1] == expected_b);
    assert(result[2] == expected_c);
    assert(result[3] == expected_d);



    /*
     * Removal builds another complete candidate generation from the
     * current environment. Only the requested package entries disappear;
     * all unrelated store references remain unchanged.
     */
    athena::install::RemovePlan remove_b;
    remove_b.add("package-b");

    const auto after_remove_b =
        athena::install::remove_target_entries(
            current,
            remove_b
        );

    assert(after_remove_b.size() == 2);
    assert(
        after_remove_b[0] ==
        expected_a
    );
    assert(
        after_remove_b[1] ==
        expected_c
    );

    /*
     * Multiple removals are evaluated as one candidate generation.
     */
    athena::install::RemovePlan remove_a_and_c;
    remove_a_and_c.add("package-a");
    remove_a_and_c.add("package-c");

    const auto after_remove_a_and_c =
        athena::install::remove_target_entries(
            current,
            remove_a_and_c
        );

    assert(after_remove_a_and_c.size() == 1);
    assert(
        after_remove_a_and_c[0] ==
        expected_current_b
    );

    /*
     * Removing every current entry produces an empty generation.
     */
    athena::install::RemovePlan remove_all;
    remove_all.add("package-a");
    remove_all.add("package-b");
    remove_all.add("package-c");

    const auto after_remove_all =
        athena::install::remove_target_entries(
            current,
            remove_all
        );

    assert(after_remove_all.empty());

    /*
     * A removal request for an entry not present in the generation is a
     * no-op at this layer. RemovalPlanner is responsible for rejecting
     * unknown installed packages before this builder is called.
     */
    athena::install::RemovePlan remove_unknown;
    remove_unknown.add("package-x");

    const auto after_remove_unknown =
        athena::install::remove_target_entries(
            current,
            remove_unknown
        );

    assert(after_remove_unknown == current.entries);

    return 0;
}
