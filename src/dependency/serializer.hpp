#pragma once

#include "expression.hpp"

#include <string>

namespace athena::dependency {

/*
 * Serialize a dependency expression back into the textual syntax
 * understood by the dependency parser.
 *
 * Parentheses are emitted whenever they are required to preserve
 * the expression tree and therefore its semantics.
 */
std::string serialize_expression(
    const ExpressionPtr& expression
);

/*
 * Serialize a complete dependency.
 *
 * The current parser represents all parsed textual dependencies as
 * runtime dependencies targeting the host package environment, so
 * only the expression itself is serialized here.
 */
std::string serialize_dependency(
    const Dependency& dependency
);

}
