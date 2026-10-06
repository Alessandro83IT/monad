// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "package.hpp"

#include <iostream>

namespace athena::package {

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
