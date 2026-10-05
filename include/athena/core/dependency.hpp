#pragma once

#include <string>
#include <vector>

namespace athena::core {

// Public values corresponding to the private dependency AST semantics.
// Resolver adapters map DependencyKind/DependencyContext explicitly to these.
enum class DependencyRole { Runtime, Build, Test, Optional, Development, Propagated };
enum class DependencyTargetContext { Host, Build, Target };

enum class VersionConstraintKind { Any, Comparison, And, Or, Not };
enum class VersionComparisonOperator { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };
struct VersionConstraint {
    VersionConstraintKind kind = VersionConstraintKind::Any;
    VersionComparisonOperator comparison = VersionComparisonOperator::Equal;
    std::string version;
    std::vector<VersionConstraint> operands;
    bool operator==(const VersionConstraint&) const = default;
};

enum class SelectorKind { Package, Capability, Feature };
struct DependencySelector {
    SelectorKind kind = SelectorKind::Package;
    std::string name;
    VersionConstraint constraint;
    bool operator==(const DependencySelector&) const = default;
};

// Canonical dependency-language value. `text` is validated and canonicalized
// by the repository metadata codec; this keeps the public contract serializable
// without exposing the resolver's polymorphic AST.
struct DependencyExpression {
    std::string text;
    bool operator==(const DependencyExpression&) const = default;
};

struct DependencyMetadata {
    DependencyExpression expression;
    DependencyRole role = DependencyRole::Runtime;
    DependencyTargetContext context = DependencyTargetContext::Target;
    bool enabled = true;
    bool operator==(const DependencyMetadata&) const = default;
};

} // namespace athena::core
