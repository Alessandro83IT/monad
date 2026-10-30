// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "package.hpp"

#include "../version/version.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include <iostream>

namespace athena::package {

void validate_identity(std::string_view name, std::string_view version)
{
    if (name.empty() ||
        !std::isalnum(static_cast<unsigned char>(name.front())) ||
        !std::all_of(name.begin(), name.end(), [](unsigned char character) {
            return std::isalnum(character) || character == '.' || character == '_' ||
                   character == '+' || character == '-';
        })) {
        throw PackageIdentityError("Invalid package identity: unsafe package name");
    }
    try {
        (void)athena::version::less(std::string(version), std::string(version));
    } catch (const std::exception&) {
        throw PackageIdentityError("Invalid package identity: invalid package version");
    }
}

/*
 * Print the information stored in a Package object.
 *
 * This function does not perform any package-management operation.
 * It only presents package metadata to the user.
 */
void print_info(const Package& package)
{
    std::cout
        // Package identity.
        << "Name: " << package.name << '\n'
        << "Version: " << package.version << '\n'

        // Human-readable description.
        << "Description: " << package.description << '\n'

        // Source archive information.
        << "Source: " << package.source << '\n'
        << "SHA-256: " << package.sha256 << '\n';
}

}
