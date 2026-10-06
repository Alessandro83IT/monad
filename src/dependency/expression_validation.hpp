#pragma once
#include "expression.hpp"
namespace athena::dependency {
// Structural support validation is separate from evaluating truth. All branches
// are checked so unsupported semantics cannot be hidden by NOT/short-circuiting.
enum class ExpressionSupport { Complete, Legacy, PackageOnly };
void validate_expression(const ExpressionPtr&, ExpressionSupport = ExpressionSupport::Complete, bool condition = false);
void validate_scope(DependencyKind, DependencyContext);
}
