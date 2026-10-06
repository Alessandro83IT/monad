// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "expression.hpp"

#include <string>

namespace athena::dependency {

/*
 * Parse a dependency expression from its textual representation.
 *
 * Supported forms:
 *
 *   package
 *   package = version
 *   package != version
 *   package < version
 *   package <= version
 *   package > version
 *   package >= version
 *
 * Logical expressions:
 *
 *   expression AND expression
 *   expression OR expression
 *   NOT expression
 *   (expression)
 *
 * Logical operator precedence:
 *
 *   NOT
 *   AND
 *   OR
 */
Dependency parse_dependency(const std::string& text);

} // namespace athena::dependency
