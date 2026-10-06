// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/install/remove_plan.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(
    bool condition,
    const std::string& message
)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main()
{
    athena::install::RemovePlan plan;

    /*
     * A newly created plan must contain no removal operations.
     */
    require(
        plan.empty(),
        "New RemovePlan must be empty"
    );

    require(
        plan.size() == 0,
        "New RemovePlan must have size zero"
    );

    /*
     * Adding a package records it in insertion order.
     */
    plan.add("A");

    require(
        !plan.empty(),
        "RemovePlan must not be empty after adding a package"
    );

    require(
        plan.size() == 1,
        "RemovePlan must contain one package"
    );

    require(
        plan.contains("A"),
        "RemovePlan must contain package A"
    );

    /*
     * Multiple distinct packages form one removal set.
     */
    plan.add("B");

    require(
        plan.size() == 2,
        "RemovePlan must contain two packages"
    );

    require(
        plan.contains("B"),
        "RemovePlan must contain package B"
    );

    /*
     * Duplicate requests must not create duplicate operations.
     */
    plan.add("A");

    require(
        plan.size() == 2,
        "Duplicate package must not be added twice"
    );

    const auto& packages = plan.packages();

    require(
        packages.size() == 2,
        "packages() must expose both requested packages"
    );

    require(
        packages[0] == "A" &&
        packages[1] == "B",
        "RemovePlan must preserve insertion order"
    );

    std::cout
        << "remove_plan_test: all checks passed\n";

    return 0;
}
