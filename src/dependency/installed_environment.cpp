#include "installed_environment.hpp"

namespace athena::dependency {

InstalledEnvironment::InstalledEnvironment(
    const std::vector<athena::package::Package>& packages
)
    : packages_(packages)
{
}

const athena::package::Package* InstalledEnvironment::find(
    const std::string& name
) const
{
    for (const auto& package : packages_) {

        if (package.name == name) {
            return &package;
        }
    }

    return nullptr;
}

bool InstalledEnvironment::contains(
    const std::string& name
) const
{
    return find(name) != nullptr;
}

const std::vector<athena::package::Package>&
InstalledEnvironment::packages() const
{
    return packages_;
}

} // namespace athena::dependency
