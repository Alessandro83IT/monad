// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "remove_plan.hpp"

#include "../package/package.hpp"

#include <string>
#include <vector>

namespace athena::install {

/*
 * Builds a removal plan against the currently installed environment.
 *
 * Removal planning is deliberately separate from Generation, Store and
 * filesystem mutation. The planner only answers one question:
 *
 *     "Can these packages be removed while keeping the remaining
 *      environment dependency-consistent?"
 *
 * The complete removal set is evaluated as one candidate transaction.
 * This is important when two packages depend on each other or when a
 * package being removed is itself the only dependency of another package
 * that is also being removed.
 */
class RemovalPlanner {
public:
    explicit RemovalPlanner(
        const std::vector<athena::package::Package>& installed
    );

    /*
     * Validate the requested removals and return the corresponding plan.
     *
     * Throws std::runtime_error when:
     *   - a requested package is not installed; or
     *   - the resulting candidate environment has an unsatisfied
     *     dependency.
     */
    RemovePlan plan(
        const std::vector<std::string>& package_names
    ) const;

private:
    const std::vector<athena::package::Package>& installed_;
};

} // namespace athena::install
