// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../package/package.hpp"

#include <string>
#include <vector>

namespace athena::dependency {

/*
 * Read-only view of the packages currently installed in a
 * candidate environment.
 *
 * The dependency validator deliberately works on the installed
 * environment rather than on RepositoryIndex: removing a package
 * must validate what remains installed, not select replacement
 * packages from a repository.
 */
class InstalledEnvironment {
public:
    explicit InstalledEnvironment(
        const std::vector<athena::package::Package>& packages
    );

    const athena::package::Package* find(
        const std::string& name
    ) const;

    bool contains(
        const std::string& name
    ) const;

    /*
     * Expose the complete candidate environment as a read-only view.
     *
     * The validator must inspect every installed package and verify
     * its dependencies against the same candidate environment.
     */
    const std::vector<athena::package::Package>& packages() const;

private:
    const std::vector<athena::package::Package>& packages_;
};

} // namespace athena::dependency
