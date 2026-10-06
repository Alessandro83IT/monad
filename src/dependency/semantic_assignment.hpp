#pragma once
#include <string>
#include <string_view>

namespace athena::dependency {
enum class SemanticSelectorKind { Capability, Feature };
// Resolver and installed validation share one assignment per semantic name,
// independent of the consumer, version constraint, role and context.
inline std::string semantic_assignment_key(SemanticSelectorKind kind, std::string_view name) {
    return std::string(kind == SemanticSelectorKind::Capability ? "capability:" : "feature:") + std::string(name);
}
}
