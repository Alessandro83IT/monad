#pragma once
#include <athena/core/install_plan.hpp>

namespace athena::installed {
// Resolution-time provider association. Retained for audit; validation checks
// current membership and may accept a different provider satisfying the selector.
struct ProviderBinding {
    core::DependencyExpression selector;
    core::CandidateIdentity provider;
    core::DependencyRole role = core::DependencyRole::Runtime;
    core::DependencyTargetContext context = core::DependencyTargetContext::Target;
    bool operator==(const ProviderBinding&) const = default;
};
struct SemanticState {
    core::PackageMetadata package;
    core::VersionScheme version_scheme = core::VersionScheme::Athena;
    core::TargetEnvironment target;
    core::ResolutionEnvironments environments;
    std::string profile;
    std::vector<core::DependencyRole> active_roles{core::DependencyRole::Runtime};
    bool optional_enabled = false;
    bool propagated_enabled = true;
    std::map<std::string, bool> feature_states;
    std::vector<ProviderBinding> bindings;
    // Old store metadata does not establish configuration/environment/conflicts.
    // Its explicitly persisted requirements are accepted only in a restricted
    // package/capability + version + boolean subset, never guessed richer state.
    bool legacy_restricted = false;
    void validate() const;
    bool operator==(const SemanticState&) const = default;
};
struct PolicyRequirement {
    core::SystemPolicyId policy;
    core::DependencyExpression selector;
    core::TargetEnvironment target;
    bool operator==(const PolicyRequirement&) const = default;
};
SemanticState from_selection(const core::InstallPlan&, const core::PackageSelection&);
std::vector<PolicyRequirement> policies_from_plan(const core::InstallPlan&);
core::DependencyExpression selector_expression(const core::DependencySelector&);
std::string serialize(const SemanticState&);
SemanticState deserialize_semantics(const std::string&);
std::string serialize(const PolicyRequirement&);
PolicyRequirement deserialize_policy(const std::string&);
}
