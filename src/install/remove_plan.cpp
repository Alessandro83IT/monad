#include "remove_plan.hpp"

#include <algorithm>

namespace athena::install {

void RemovePlan::add(const std::string& package_name)
{
    /*
     * A removal request is a set of package names semantically.
     * Ignore duplicates so that planning the same package twice does
     * not produce duplicate removal operations.
     */
    if (!contains(package_name)) {
        packages_.push_back(package_name);
    }
}

const std::vector<std::string>& RemovePlan::packages() const
{
    return packages_;
}

bool RemovePlan::contains(const std::string& package_name) const
{
    return std::find(
        packages_.begin(),
        packages_.end(),
        package_name
    ) != packages_.end();
}

bool RemovePlan::empty() const
{
    return packages_.empty();
}

std::size_t RemovePlan::size() const
{
    return packages_.size();
}

} // namespace athena::install
