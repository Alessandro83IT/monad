#include "repository_index.hpp"

#include "../version/version.hpp"

#include <algorithm>

namespace athena::repository {

void RepositoryIndex::add(
    const athena::package::Package& package
)
{
    packages_.push_back(package);

    /*
     * Keep the index deterministic after every insertion. This makes
     * repository queries predictable and gives later resolver logic a
     * stable candidate ordering.
     */
    std::sort(
        packages_.begin(),
        packages_.end(),
        [](const auto& left, const auto& right) {

            if (left.name != right.name) {
                return left.name < right.name;
            }

            return athena::version::less(
                left.version,
                right.version
            );
        }
    );
}

std::vector<athena::package::Package>
RepositoryIndex::find(
    const std::string& name
) const
{
    std::vector<athena::package::Package> result;

    for (const auto& package : packages_) {

        if (package.name == name) {
            result.push_back(package);
        }
    }

    return result;
}

std::vector<athena::package::Package>
RepositoryIndex::find_capability(
    const std::string& capability
) const
{
    std::vector<athena::package::Package> result;

    for (const auto& package : packages_) {

        const auto& capabilities =
            package.provides.capabilities;

        if (std::find(
                capabilities.begin(),
                capabilities.end(),
                capability
            ) != capabilities.end()) {

            result.push_back(package);
        }
    }

    return result;
}

std::vector<athena::package::Package>
RepositoryIndex::find_feature(
    const std::string& feature
) const
{
    std::vector<athena::package::Package> result;

    for (const auto& package : packages_) {

        const auto& features =
            package.provides.features;

        if (std::find(
                features.begin(),
                features.end(),
                feature
            ) != features.end()) {

            result.push_back(package);
        }
    }

    return result;
}

bool RepositoryIndex::contains(
    const std::string& name
) const
{
    return std::any_of(
        packages_.begin(),
        packages_.end(),
        [&name](const auto& package) {
            return package.name == name;
        }
    );
}

}
