// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "removal_planner.hpp"

#include "../dependency/installed_environment.hpp"
#include "../dependency/validator.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace athena::install {

RemovalPlanner::RemovalPlanner(
    const std::vector<athena::package::Package>& installed
)
    : installed_(installed)
{
}

RemovePlan RemovalPlanner::plan(
    const std::vector<std::string>& package_names
) const
{
    /*
     * Treat the requested removals as one transaction. A package is
     * therefore removed from the candidate environment before dependency
     * validation, even when another requested package depends on it.
     */
    std::unordered_set<std::string> requested;

    for (const auto& package_name : package_names) {

        if (package_name.empty()) {
            throw std::runtime_error(
                "Cannot remove a package with an empty name"
            );
        }

        requested.insert(package_name);
    }

    /*
     * Validate that every requested package actually exists in the
     * installed environment before constructing the candidate state.
     */
    for (const auto& package_name : requested) {

        const auto package = std::find_if(
            installed_.begin(),
            installed_.end(),
            [&package_name](
                const athena::package::Package& candidate
            ) {
                return candidate.name == package_name;
            }
        );

        if (package == installed_.end()) {
            throw std::runtime_error(
                "Cannot remove package '" +
                package_name +
                "': package is not installed"
            );
        }
    }

    /*
     * Build the candidate environment without any package belonging to
     * the complete removal set. No persistent state is touched here.
     */
    std::vector<athena::package::Package> candidate;

    candidate.reserve(installed_.size());

    for (const auto& package : installed_) {

        if (requested.find(package.name) == requested.end()) {
            candidate.push_back(package);
        }
    }

    /*
     * Reuse the same validator used by the rest of the dependency
     * subsystem. This keeps removal semantics consistent with the
     * installed-environment dependency model.
     */
    const athena::dependency::InstalledEnvironment environment(
        candidate
    );

    const athena::dependency::DependencyValidator validator(
        environment
    );

    validator.validate();

    /*
     * Validation succeeded, so expose the requested removal set as a
     * RemovePlan. The plan itself contains no filesystem operations.
     *
     * Preserve the caller's insertion order while RemovePlan prevents
     * duplicate package names.
     */
    RemovePlan result;

    for (const auto& package_name : package_names) {
        result.add(package_name);
    }

    return result;
}

} // namespace athena::install
