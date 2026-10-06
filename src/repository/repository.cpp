// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "repository.hpp"

#include "../package/package_loader.hpp"

#include <algorithm>
#include <stdexcept>

namespace athena::repository {

void Repository::add(
    const athena::package::Package& package
)
{
    /*
     * Repository is the mutation boundary for package definitions.
     * The underlying RepositoryIndex remains encapsulated.
     */
    index_.add(package);
}

void Repository::load_package(
    const std::filesystem::path& path
)
{
    /*
     * Keep TOML parsing outside Repository. The package loader converts
     * the external representation into Athena's internal Package model.
     */
    const auto package =
        athena::package::load_from_file(path);

    index_.add(package);
}

void Repository::load_directory(
    const std::filesystem::path& path
)
{
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error(
            "Repository directory does not exist: " +
            path.string()
        );
    }

    if (!std::filesystem::is_directory(path)) {
        throw std::runtime_error(
            "Repository path is not a directory: " +
            path.string()
        );
    }

    /*
     * Only package.toml files directly inside the directory are loaded.
     * Sorting the paths first makes repository construction deterministic
     * regardless of filesystem directory iteration order.
     */
    std::vector<std::filesystem::path> package_files;

    for (const auto& entry :
         std::filesystem::directory_iterator(path)) {

        if (!entry.is_regular_file()) {
            continue;
        }

        if (entry.path().filename() == "package.toml") {
            package_files.push_back(entry.path());
        }
    }

    std::sort(
        package_files.begin(),
        package_files.end()
    );

    for (const auto& package_file : package_files) {
        load_package(package_file);
    }
}

std::vector<athena::package::Package>
Repository::find(
    const std::string& name
) const
{
    return index_.find(name);
}

bool Repository::contains(
    const std::string& name
) const
{
    return index_.contains(name);
}

const RepositoryIndex& Repository::index() const
{
    return index_;
}

}
