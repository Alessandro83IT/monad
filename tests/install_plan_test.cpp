// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/install/install_plan.hpp"

#include <cassert>
#include <iostream>

using athena::install::InstallPlan;
using athena::package::Package;

int main()
{
    /*
     * Create a small dependency-order example.
     *
     * The plan should preserve exactly the order supplied by the
     * dependency resolver: dependency packages first, root package last.
     */
    Package zlib{
        "zlib",
        "2.0",
        "Zlib",
        "",
        "",
        "autotools",
        {}
    };

    Package libfoo{
        "libfoo",
        "1.0",
        "Libfoo",
        "",
        "",
        "autotools",
        {}
    };

    Package app{
        "app",
        "1.0",
        "Application",
        "",
        "",
        "autotools",
        {}
    };

    InstallPlan plan;

    assert(plan.empty());
    assert(plan.size() == 0);

    plan.add(zlib);
    plan.add(libfoo);
    plan.add(app);

    assert(!plan.empty());
    assert(plan.size() == 3);

    const auto& packages = plan.packages();

    assert(packages[0].name == "zlib");
    assert(packages[1].name == "libfoo");
    assert(packages[2].name == "app");

    /*
     * Verify that the plan preserves package metadata rather than
     * storing only package names.
     */
    assert(packages[0].version == "2.0");
    assert(packages[1].version == "1.0");
    assert(packages[2].version == "1.0");

    std::cout
        << "Install plan tests passed.\n";

    return 0;
}
