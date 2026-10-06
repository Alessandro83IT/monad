// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../package/package.hpp"
#include "repository_index.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace athena::repository {

/*
 * High-level local repository abstraction.
 *
 * Repository owns the process of loading package definitions and
 * exposes the resulting package index to higher-level components.
 *
 * Package parsing remains delegated to package::load_from_file().
 * RepositoryIndex remains responsible only for indexing and querying
 * already-loaded Package objects.
 */
class Repository {

public:

    /*
     * Load one package definition from a TOML file and add it to
     * the repository index.
     */
    void load_package(
        const std::filesystem::path& path
    );

    /*
     * Add an already-loaded Package to the repository.
     *
     * This keeps mutation of the internal RepositoryIndex behind the
     * Repository abstraction and avoids exposing a mutable index.
     */
    void add(
        const athena::package::Package& package
    );

    /*
     * Load every package.toml file directly contained in a directory.
     * The initial implementation intentionally does not recurse.
     */
    void load_directory(
        const std::filesystem::path& path
    );

    /*
     * Find every available version of a package.
     *
     * RepositoryIndex owns the actual query and version ordering logic.
     */
    std::vector<athena::package::Package> find(
        const std::string& name
    ) const;

    /*
     * Check whether at least one version of a package is available.
     */
    bool contains(
        const std::string& name
    ) const;

    /*
     * Expose the underlying index to components that explicitly need
     * repository-index semantics, such as the dependency resolver.
     */
    const RepositoryIndex& index() const;

private:

    RepositoryIndex index_;
};

}
