// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "install_plan.hpp"

#include <algorithm>

namespace athena::install {

void InstallPlan::add(
    const athena::package::Package& package
)
{
    /*
     * Legacy callers have no explicit context. Target is the historical
     * default because packages previously represented only final-system
     * installation requirements.
     */
    add(
        package,
        athena::dependency::DependencyContext::Target
    );
}

void InstallPlan::add(
    const athena::package::Package& package,
    athena::dependency::DependencyContext context
)
{
    /*
     * A package can be selected by multiple semantic contexts. Merge the
     * context instead of duplicating the package in the installation plan.
     */
    for (std::size_t i = 0; i < packages_.size(); ++i) {

        if (packages_[i].name != package.name ||
            packages_[i].version != package.version) {
            continue;
        }

        auto& package_contexts = contexts_[i];

        if (std::find(
                package_contexts.begin(),
                package_contexts.end(),
                context
            ) == package_contexts.end()) {

            package_contexts.push_back(context);
        }

        return;
    }

    packages_.push_back(package);
    contexts_.push_back({context});
}

const std::vector<athena::package::Package>&
InstallPlan::packages() const
{
    return packages_;
}

const std::vector<
    std::vector<athena::dependency::DependencyContext>
>&
InstallPlan::contexts() const
{
    return contexts_;
}

bool InstallPlan::empty() const
{
    return packages_.empty();
}

std::size_t InstallPlan::size() const
{
    return packages_.size();
}

}
