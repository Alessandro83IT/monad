#include <athena/core/install_plan.hpp>

#include <algorithm>
#include <set>
#include <tuple>

namespace athena::core {
namespace {
using Key = std::tuple<std::string, std::string, std::string, std::string>;
Key key(const CandidateIdentity& id) {
    return {id.package.value(), id.version.value(), id.repository.value(),
        id.snapshot.snapshot_id ? id.snapshot.snapshot_id->value() : std::string{}};
}
bool matches(const std::vector<std::string>& allowed, const std::string& value) {
    return allowed.empty() || std::find(allowed.begin(), allowed.end(), value) != allowed.end();
}
bool valid_constraint(const VersionConstraint& constraint) {
    switch (constraint.kind) {
    case VersionConstraintKind::Any: return constraint.version.empty() && constraint.operands.empty();
    case VersionConstraintKind::Comparison: return !constraint.version.empty() && constraint.operands.empty();
    case VersionConstraintKind::And:
    case VersionConstraintKind::Or:
        return !constraint.operands.empty() && std::all_of(constraint.operands.begin(), constraint.operands.end(), valid_constraint);
    case VersionConstraintKind::Not:
        return constraint.operands.size() == 1 && valid_constraint(constraint.operands.front());
    }
    return false;
}
bool valid_selector(const DependencySelector& selector) {
    return !selector.name.empty() && valid_constraint(selector.constraint);
}
}

InstallPlan InstallPlanBuilder::build() const {
    auto result = result_;
    for (auto& selection : result.selections) {
        std::sort(selection.reasons.begin(), selection.reasons.end(), [](const auto& a, const auto& b) {
            return std::tuple{a.kind, a.profile, a.feature, a.policy ? a.policy->value() : std::string{}} <
                   std::tuple{b.kind, b.profile, b.feature, b.policy ? b.policy->value() : std::string{}};
        });
        selection.reasons.erase(std::unique(selection.reasons.begin(), selection.reasons.end()), selection.reasons.end());
    }
    std::sort(result.selections.begin(), result.selections.end(), [](const auto& a, const auto& b) { return key(a.identity()) < key(b.identity()); });
    std::sort(result.capability_bindings.begin(), result.capability_bindings.end(), [](const auto& a, const auto& b) {
        return std::tuple{a.selector.name, a.selector.constraint.version, key(a.provider)} <
               std::tuple{b.selector.name, b.selector.constraint.version, key(b.provider)};
    });
    std::sort(result.configured_features.begin(), result.configured_features.end(), [](const auto& a, const auto& b) { return std::tie(a.profile, a.feature, a.enabled) < std::tie(b.profile, b.feature, b.enabled); });
    std::sort(result.offered_features.begin(), result.offered_features.end(), [](const auto& a, const auto& b) { return std::tuple{a.feature, key(a.provider)} < std::tuple{b.feature, key(b.provider)}; });
    std::sort(result.activated_features.begin(), result.activated_features.end(), [](const auto& a, const auto& b) { return std::tuple{a.profile, a.feature, key(a.owner)} < std::tuple{b.profile, b.feature, key(b.owner)}; });
    std::sort(result.edges.begin(), result.edges.end(), [](const auto& a, const auto& b) { return std::tuple{key(a.source), a.target.kind, a.target.name, a.target.constraint.version, a.role, a.context} < std::tuple{key(b.source), b.target.kind, b.target.name, b.target.constraint.version, b.role, b.context}; });
    result.edges.erase(std::unique(result.edges.begin(), result.edges.end()), result.edges.end());
    std::sort(result.negative_constraints.begin(), result.negative_constraints.end(), [](const auto& a, const auto& b) { return std::tuple{key(a.declared_by), a.expression.text, a.role, a.context} < std::tuple{key(b.declared_by), b.expression.text, b.role, b.context}; });
    result.negative_constraints.erase(std::unique(result.negative_constraints.begin(), result.negative_constraints.end()), result.negative_constraints.end());
    InstallPlan plan(std::move(result));
    plan.validate();
    return plan;
}

void InstallPlan::validate() const {
    std::set<Key> identities;
    std::set<std::string> package_ids;
    for (const auto& selection : result_.selections) {
        const auto id = selection.identity();
        try { selection.candidate.validate(); selection.snapshot.validate(); }
        catch (const std::exception& e) { throw InstallPlanError(InstallPlanErrorCode::RepositoryMismatch, e.what()); }
        if (selection.candidate.package.package_id != id.package || selection.candidate.package.version != id.version ||
            selection.candidate.provenance.repository_id != id.repository || selection.snapshot.repository_id != id.repository ||
            !selection.candidate.provenance.snapshot || *selection.candidate.provenance.snapshot != selection.snapshot ||
            id.provenance != selection.candidate.provenance)
            throw InstallPlanError(InstallPlanErrorCode::RepositoryMismatch, "Candidate identity/provenance mismatch");
        if (!matches(selection.candidate.package.platforms, result_.target.platform) ||
            !matches(selection.candidate.package.architectures, result_.target.architecture) ||
            !matches(selection.candidate.package.abis, result_.target.abi))
            throw InstallPlanError(InstallPlanErrorCode::TargetMismatch, "Candidate does not match target environment");
        if (!identities.insert(key(id)).second || !package_ids.insert(id.package.value()).second)
            throw InstallPlanError(InstallPlanErrorCode::DuplicatePackage, "PackageId selected more than once: " + id.package.value());
        for (const auto& reason : selection.reasons) {
            const bool valid = (reason.kind == InstallReasonKind::ExplicitRequest && reason.explicit_target && valid_selector(reason.explicit_target->selector)) ||
                (reason.kind == InstallReasonKind::DependencyOf && reason.package) ||
                (reason.kind == InstallReasonKind::EnabledByFeature && !reason.profile.empty() && !reason.feature.empty()) ||
                (reason.kind == InstallReasonKind::RequiredBySystemPolicy && reason.policy && !reason.policy->empty());
            if (!valid) throw InstallPlanError(InstallPlanErrorCode::InvalidReason, "Incomplete install reason");
        }
    }
    const auto contains = [&](const CandidateIdentity& id) { return std::any_of(result_.selections.begin(), result_.selections.end(), [&](const auto& s) { return s.identity() == id; }); };
    for (const auto& selection : result_.selections) for (const auto& reason : selection.reasons) {
        if (reason.package && !contains(*reason.package))
            throw InstallPlanError(InstallPlanErrorCode::InvalidReason, "Dependency reason refers to an unselected package identity");
        if (reason.kind == InstallReasonKind::EnabledByFeature &&
            std::none_of(result_.configured_features.begin(), result_.configured_features.end(), [&](const auto& f) { return f.profile == reason.profile && f.feature == reason.feature && f.enabled; }))
            throw InstallPlanError(InstallPlanErrorCode::InvalidReason, "Feature reason has no enabled configuration");
    }
    for (const auto& binding : result_.capability_bindings)
        if (binding.selector.kind != SelectorKind::Capability || !valid_selector(binding.selector) || !contains(binding.provider)) throw InstallPlanError(InstallPlanErrorCode::InvalidBinding, "Invalid capability provider binding");
    for (const auto& edge : result_.edges) {
        if (!contains(edge.source) || (edge.provider && !contains(*edge.provider)) || !valid_selector(edge.target))
            throw InstallPlanError(InstallPlanErrorCode::InvalidEdge, "Dependency edge references an unselected candidate or invalid selector");
        if (edge.reason && edge.reason->package && !contains(*edge.reason->package))
            throw InstallPlanError(InstallPlanErrorCode::InvalidReason, "Dependency edge reason references an unselected package");
    }
    for (const auto& feature : result_.offered_features) {
        const auto owner = std::find_if(result_.selections.begin(), result_.selections.end(), [&](const auto& s) { return s.identity() == feature.provider; });
        if (owner == result_.selections.end() || std::find(owner->candidate.package.features.begin(), owner->candidate.package.features.end(), feature.feature) == owner->candidate.package.features.end())
            throw InstallPlanError(InstallPlanErrorCode::InvalidFeature, "Offered feature/provider mismatch");
    }
    for (const auto& feature : result_.activated_features) {
        const auto owner = std::find_if(result_.selections.begin(), result_.selections.end(), [&](const auto& s) { return s.identity() == feature.owner; });
        const bool configured = std::any_of(result_.configured_features.begin(), result_.configured_features.end(), [&](const auto& f) { return f.profile == feature.profile && f.feature == feature.feature && f.enabled; });
        if (owner == result_.selections.end() || !configured || !owner->candidate.package.feature_dependencies.contains(feature.feature))
            throw InstallPlanError(InstallPlanErrorCode::InvalidFeature, "Feature activation is inconsistent");
    }
    for (const auto& negative : result_.negative_constraints) {
        const auto owner = std::find_if(result_.selections.begin(), result_.selections.end(), [&](const auto& selection) { return selection.identity() == negative.declared_by; });
        if (owner == result_.selections.end() || negative.expression.text.empty() ||
            std::find(owner->candidate.package.conflicts.begin(), owner->candidate.package.conflicts.end(), negative.expression.text) == owner->candidate.package.conflicts.end())
            throw InstallPlanError(InstallPlanErrorCode::InvalidExpression, "Negative expression is not declared by its selected owner");
    }
}
} // namespace athena::core
