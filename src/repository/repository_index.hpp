#pragma once

#include "../package/package.hpp"

#include <string>
#include <vector>

namespace athena::repository {

/*
 * In-memory index of packages available from one or more repository
 * sources.
 *
 * The index stores package definitions, not installed packages.
 * Installation state remains the responsibility of the package store
 * and generation layers.
 */
class RepositoryIndex {

public:

    /*
     * Add a package definition to the index.
     *
     * Multiple versions of the same package are intentionally allowed.
     */
    void add(
        const athena::package::Package& package
    );

    /*
     * Return every available version of a package.
     *
     * Results are ordered from oldest to newest according to
     * Athena's version comparison rules.
     */
    std::vector<athena::package::Package> find(
        const std::string& name
    ) const;

    /*
     * Return every package exposing the requested semantic capability.
     *
     * Multiple package versions and multiple providers are allowed.
     * The result follows the same deterministic package ordering used
     * by the repository index.
     */
    std::vector<athena::package::Package> find_capability(
        const std::string& capability
    ) const;

    /*
     * Return every package exposing the requested semantic feature.
     *
     * Features are package-provided semantic interfaces and are distinct
     * from the package name itself.
     */
    std::vector<athena::package::Package> find_feature(
        const std::string& feature
    ) const;

    /*
     * Check whether at least one version of a package is available.
     */
    bool contains(
        const std::string& name
    ) const;

    const std::vector<athena::package::Package>& packages() const noexcept { return packages_; }

private:

    std::vector<athena::package::Package> packages_;
};

}
