// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace athena::install {

/*
 * Describes which installed packages should be removed.
 *
 * RemovePlan is deliberately independent from Store and Generation:
 * it represents only the result of the removal decision. Applying the
 * plan will be handled by the generation/removal layer later, which
 * keeps planning separate from filesystem mutation.
 */
class RemovePlan {
public:
    void add(const std::string& package_name);

    const std::vector<std::string>& packages() const;

    bool contains(const std::string& package_name) const;

    bool empty() const;

    std::size_t size() const;

private:
    std::vector<std::string> packages_;
};

} // namespace athena::install
