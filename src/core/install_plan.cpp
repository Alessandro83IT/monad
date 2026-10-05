#include "../dependency/constraint.hpp"
#include "../dependency/parser.hpp"
#include "../dependency/serializer.hpp"
#include "resolution_order.hpp"
#include <athena/core/install_plan.hpp>

#include <algorithm>
#include <set>
#include <tuple>

namespace athena::core
{
namespace
{
using Key = std::tuple<std::string, std::string, std::string, std::string>;
Key key(const CandidateIdentity& id)
{
    return {id.package.value(), id.version.value(), id.repository.value(),
            id.snapshot.snapshot_id ? id.snapshot.snapshot_id->value() : std::string{}};
}
bool matches(const std::vector<std::string>& allowed, const std::string& value)
{
    return allowed.empty() || std::find(allowed.begin(), allowed.end(), value) != allowed.end();
}
bool valid_constraint(const VersionConstraint& constraint)
{
    switch (constraint.kind)
    {
    case VersionConstraintKind::Any:
        return constraint.version.empty() && constraint.operands.empty();
    case VersionConstraintKind::Comparison:
        switch (constraint.comparison)
        {
        case VersionComparisonOperator::Equal:
        case VersionComparisonOperator::NotEqual:
        case VersionComparisonOperator::Less:
        case VersionComparisonOperator::LessEqual:
        case VersionComparisonOperator::Greater:
        case VersionComparisonOperator::GreaterEqual:
            break;
        default:
            return false;
        }
        return !constraint.version.empty() && constraint.operands.empty();
    case VersionConstraintKind::And:
    case VersionConstraintKind::Or:
        return constraint.version.empty() && !constraint.operands.empty() &&
               std::all_of(constraint.operands.begin(), constraint.operands.end(),
                           valid_constraint);
    case VersionConstraintKind::Not:
        return constraint.version.empty() && constraint.operands.size() == 1 &&
               valid_constraint(constraint.operands.front());
    }
    return false;
}
bool valid_selector(const DependencySelector& selector)
{
    switch (selector.kind)
    {
    case SelectorKind::Package:
    case SelectorKind::Capability:
    case SelectorKind::Feature:
        break;
    default:
        return false;
    }
    return !selector.name.empty() && selector.name.find_first_of(" \t\r\n") == std::string::npos &&
           valid_constraint(selector.constraint);
}
athena::dependency::VersionConstraint private_constraint(const VersionConstraint& c)
{
    using V = athena::dependency::VersionConstraint;
    using O = athena::dependency::ComparisonOperator;
    switch (c.kind)
    {
    case VersionConstraintKind::Any:
        return V::any();
    case VersionConstraintKind::Comparison:
    {
        O op;
        switch (c.comparison)
        {
        case VersionComparisonOperator::Equal:
            op = O::Equal;
            break;
        case VersionComparisonOperator::NotEqual:
            op = O::NotEqual;
            break;
        case VersionComparisonOperator::Less:
            op = O::Less;
            break;
        case VersionComparisonOperator::LessEqual:
            op = O::LessEqual;
            break;
        case VersionComparisonOperator::Greater:
            op = O::Greater;
            break;
        case VersionComparisonOperator::GreaterEqual:
            op = O::GreaterEqual;
            break;
        default:
            throw std::invalid_argument("Unknown comparison");
        }
        return V::comparison(op, c.version);
    }
    case VersionConstraintKind::Not:
        return V::not_(private_constraint(c.operands.front()));
    case VersionConstraintKind::And:
    case VersionConstraintKind::Or:
    {
        std::vector<V> children;
        for (const auto& child : c.operands)
            children.push_back(private_constraint(child));
        return c.kind == VersionConstraintKind::And ? V::all(std::move(children))
                                                    : V::any_of(std::move(children));
    }
    }
    throw std::invalid_argument("Unknown constraint");
}
bool selector_matches(const DependencySelector& s, const PackageMetadata& p)
{
    bool matches = false;
    switch (s.kind)
    {
    case SelectorKind::Package:
        matches = s.name == p.package_id.value();
        break;
    case SelectorKind::Capability:
        matches =
            std::find(p.capabilities.begin(), p.capabilities.end(), s.name) != p.capabilities.end();
        break;
    case SelectorKind::Feature:
        matches = std::find(p.features.begin(), p.features.end(), s.name) != p.features.end();
        break;
    }
    return matches &&
           athena::dependency::satisfies(private_constraint(s.constraint), p.version.value());
}
DependencyRole role_value(athena::dependency::DependencyKind r)
{
    using R = athena::dependency::DependencyKind;
    switch (r)
    {
    case R::Runtime:
        return DependencyRole::Runtime;
    case R::Build:
        return DependencyRole::Build;
    case R::Test:
        return DependencyRole::Test;
    case R::Optional:
        return DependencyRole::Optional;
    case R::Development:
        return DependencyRole::Development;
    case R::Propagated:
        return DependencyRole::Propagated;
    }
    throw std::invalid_argument("Unknown role");
}
DependencyTargetContext context_value(athena::dependency::DependencyContext c)
{
    using C = athena::dependency::DependencyContext;
    switch (c)
    {
    case C::Host:
        return DependencyTargetContext::Host;
    case C::Build:
        return DependencyTargetContext::Build;
    case C::Target:
        return DependencyTargetContext::Target;
    }
    throw std::invalid_argument("Unknown context");
}
bool role_matches(athena::dependency::DependencyKind r, DependencyRole p)
{
    return role_value(r) == p;
}
bool context_matches(athena::dependency::DependencyContext c, DependencyTargetContext p)
{
    return context_value(c) == p;
}
bool has_negative_public(const athena::dependency::ExpressionPtr& e, const std::string& canonical,
                         DependencyRole role, DependencyTargetContext context,
                         const NegativeConstraint& n)
{
    using namespace athena::dependency;
    if (!e)
        return false;
    switch (e->kind)
    {
    case Expression::Kind::Not:
    {
        const auto& node = static_cast<const NotExpression&>(*e);
        return (role == n.role && context == n.context &&
                serialize_expression(node.expression) == canonical);
    }
    case Expression::Kind::And:
        for (const auto& child : static_cast<const AndExpression&>(*e).expressions)
            if (has_negative_public(child, canonical, role, context, n))
                return true;
        return false;
    case Expression::Kind::Or:
        for (const auto& child : static_cast<const OrExpression&>(*e).expressions)
            if (has_negative_public(child, canonical, role, context, n))
                return true;
        return false;
    case Expression::Kind::Conditional:
        return has_negative_public(static_cast<const ConditionalExpression&>(*e).body, canonical,
                                   role, context, n);
    case Expression::Kind::Scoped:
    {
        const auto& scope = static_cast<const ScopedExpression&>(*e);
        return has_negative_public(scope.body, canonical, role_value(scope.kind_role),
                                   context_value(scope.context), n);
    }
    default:
        return false;
    }
}
bool has_negative(const athena::dependency::ExpressionPtr& e, const std::string& canonical,
                  athena::dependency::DependencyKind role,
                  athena::dependency::DependencyContext context, const NegativeConstraint& n)
{
    return has_negative_public(e, canonical, role_value(role), context_value(context), n);
}

} // namespace

InstallPlan InstallPlanBuilder::build() const
{
    auto result = result_;
    detail::canonicalize(result);
    InstallPlan plan(std::move(result));
    plan.validate();
    return plan;
}

void InstallPlan::validate() const
{
    std::set<Key> identities;
    std::set<std::string> package_ids;
    for (const auto& selection : result_.selections)
    {
        const auto id = selection.identity();
        try
        {
            selection.candidate.validate();
            selection.snapshot.validate();
        }
        catch (const std::exception& e)
        {
            throw InstallPlanError(InstallPlanErrorCode::RepositoryMismatch, e.what());
        }
        if (selection.candidate.package.package_id != id.package ||
            selection.candidate.package.version != id.version ||
            selection.candidate.provenance.repository_id != id.repository ||
            selection.snapshot.repository_id != id.repository ||
            (selection.candidate.provenance.snapshot &&
             *selection.candidate.provenance.snapshot != selection.snapshot) ||
            (selection.candidate.provenance.revision &&
             *selection.candidate.provenance.revision != selection.snapshot.revision) ||
            id.provenance != selection.candidate.provenance)
            throw InstallPlanError(InstallPlanErrorCode::RepositoryMismatch,
                                   "Candidate identity/provenance mismatch");
        if (!matches(selection.candidate.package.platforms, result_.target.platform) ||
            !matches(selection.candidate.package.architectures, result_.target.architecture) ||
            !matches(selection.candidate.package.abis, result_.target.abi))
            throw InstallPlanError(InstallPlanErrorCode::TargetMismatch,
                                   "Candidate does not match target environment");
        if (!identities.insert(key(id)).second || !package_ids.insert(id.package.value()).second)
            throw InstallPlanError(InstallPlanErrorCode::DuplicatePackage,
                                   "PackageId selected more than once: " + id.package.value());
        for (const auto& reason : selection.reasons)
        {
            const bool valid =
                (reason.kind == InstallReasonKind::ExplicitRequest && reason.explicit_target &&
                 valid_selector(reason.explicit_target->selector)) ||
                (reason.kind == InstallReasonKind::DependencyOf && reason.package) ||
                (reason.kind == InstallReasonKind::EnabledByFeature && reason.package &&
                 !reason.feature.empty()) ||
                (reason.kind == InstallReasonKind::RequiredBySystemPolicy && reason.policy &&
                 !reason.policy->empty());
            if (!valid)
                throw InstallPlanError(InstallPlanErrorCode::InvalidReason,
                                       "Incomplete install reason");
        }
    }
    const auto contains = [&](const CandidateIdentity& id)
    {
        return std::any_of(result_.selections.begin(), result_.selections.end(),
                           [&](const auto& s) { return s.identity() == id; });
    };
    for (const auto& selection : result_.selections)
        for (const auto& reason : selection.reasons)
        {
            if (reason.package && !contains(*reason.package))
                throw InstallPlanError(
                    InstallPlanErrorCode::InvalidReason,
                    "Dependency reason refers to an unselected package identity");
            if (reason.kind == InstallReasonKind::EnabledByFeature &&
                std::none_of(result_.configured_features.begin(), result_.configured_features.end(),
                             [&](const auto& f)
                             {
                                 return f.profile == reason.profile &&
                                        f.feature == reason.feature && f.enabled;
                             }))
                throw InstallPlanError(InstallPlanErrorCode::InvalidReason,
                                       "Feature reason has no enabled configuration");
            if (reason.kind == InstallReasonKind::EnabledByFeature &&
                std::none_of(result_.activated_features.begin(), result_.activated_features.end(),
                             [&](const auto& f)
                             {
                                 return f.owner == *reason.package && f.profile == reason.profile &&
                                        f.feature == reason.feature;
                             }))
                throw InstallPlanError(InstallPlanErrorCode::InvalidReason,
                                       "Feature reason has no selected owner activation");
            if (reason.kind == InstallReasonKind::RequiredBySystemPolicy &&
                std::none_of(
                    result_.system_policy_selections.begin(),
                    result_.system_policy_selections.end(), [&](const auto& p)
                    { return p.policy == *reason.policy && p.candidate == selection.identity(); }))
                throw InstallPlanError(InstallPlanErrorCode::InvalidReason,
                                       "Policy reason has no policy selection");
        }
    for (const auto& binding : result_.capability_bindings)
        if (binding.selector.kind != SelectorKind::Capability ||
            !valid_selector(binding.selector) || !contains(binding.provider))
            throw InstallPlanError(InstallPlanErrorCode::InvalidBinding,
                                   "Invalid capability provider binding");
    for (const auto& edge : result_.edges)
    {
        if (!contains(edge.source) || (edge.provider && !contains(*edge.provider)) ||
            !valid_selector(edge.target))
            throw InstallPlanError(
                InstallPlanErrorCode::InvalidEdge,
                "Dependency edge references an unselected candidate or invalid selector");
        if (edge.reason && edge.reason->package && !contains(*edge.reason->package))
            throw InstallPlanError(InstallPlanErrorCode::InvalidReason,
                                   "Dependency edge reason references an unselected package");
    }
    for (const auto& feature : result_.offered_features)
    {
        const auto owner =
            std::find_if(result_.selections.begin(), result_.selections.end(),
                         [&](const auto& s) { return s.identity() == feature.provider; });
        if (owner == result_.selections.end() ||
            std::find(owner->candidate.package.features.begin(),
                      owner->candidate.package.features.end(),
                      feature.feature) == owner->candidate.package.features.end())
            throw InstallPlanError(InstallPlanErrorCode::InvalidFeature,
                                   "Offered feature/provider mismatch");
    }
    for (const auto& feature : result_.activated_features)
    {
        const auto owner =
            std::find_if(result_.selections.begin(), result_.selections.end(),
                         [&](const auto& s) { return s.identity() == feature.owner; });
        const bool configured = std::any_of(
            result_.configured_features.begin(), result_.configured_features.end(),
            [&](const auto& f)
            { return f.profile == feature.profile && f.feature == feature.feature && f.enabled; });
        if (owner == result_.selections.end() || !configured ||
            !owner->candidate.package.feature_dependencies.contains(feature.feature))
            throw InstallPlanError(InstallPlanErrorCode::InvalidFeature,
                                   "Feature activation is inconsistent");
    }
    for (const auto& policy : result_.system_policy_selections)
    {
        const auto selected =
            std::find_if(result_.selections.begin(), result_.selections.end(),
                         [&](const auto& s) { return s.identity() == policy.candidate; });
        if (policy.policy.empty() || !valid_selector(policy.selector) ||
            selected == result_.selections.end() ||
            !selector_matches(policy.selector, selected->candidate.package) ||
            std::none_of(selected->reasons.begin(), selected->reasons.end(),
                         [&](const auto& r)
                         {
                             return r.kind == InstallReasonKind::RequiredBySystemPolicy &&
                                    r.policy == policy.policy;
                         }))
            throw InstallPlanError(InstallPlanErrorCode::InvalidReason,
                                   "Invalid system policy selection");
    }
    for (const auto& negative : result_.negative_constraints)
    {
        const auto owner = std::find_if(result_.selections.begin(), result_.selections.end(),
                                        [&](const auto& selection)
                                        { return selection.identity() == negative.declared_by; });
        bool declared = false;
        try
        {
            if (owner != result_.selections.end() && !negative.expression.text.empty())
            {
                const auto canonical = athena::dependency::serialize_expression(
                    athena::dependency::parse_dependency("(" + negative.expression.text + ")")
                        .expression);
                if (canonical != negative.expression.text)
                    throw std::invalid_argument("Noncanonical prohibited expression");
                const auto& metadata = owner->candidate.package;
                switch (negative.source.kind)
                {
                case NegativeConstraintSourceKind::ConflictDeclaration:
                    if (negative.source.feature.empty())
                        for (const auto& text : metadata.conflicts)
                        {
                            const auto d = athena::dependency::parse_dependency(text);
                            if (athena::dependency::serialize_expression(d.expression) ==
                                    canonical &&
                                role_matches(d.kind, negative.role) &&
                                context_matches(d.context, negative.context))
                                declared = true;
                        }
                    break;
                case NegativeConstraintSourceKind::PackageDependency:
                    if (negative.source.feature.empty())
                        for (const auto& text : metadata.dependencies)
                        {
                            const auto d = athena::dependency::parse_dependency(text);
                            if (has_negative(d.expression, canonical, d.kind, d.context, negative))
                                declared = true;
                        }
                    break;
                case NegativeConstraintSourceKind::FeatureDependency:
                {
                    const auto feature =
                        metadata.feature_dependencies.find(negative.source.feature);
                    if (!negative.source.feature.empty() &&
                        feature != metadata.feature_dependencies.end())
                        for (const auto& d : feature->second)
                        {
                            const auto e =
                                athena::dependency::parse_dependency(d.expression.text).expression;
                            if (has_negative_public(e, canonical, d.role, d.context, negative))
                                declared = true;
                        }
                    break;
                }
                }
            }
        }
        catch (const std::exception&)
        {
            declared = false;
        }
        if (!declared)
            throw InstallPlanError(
                InstallPlanErrorCode::InvalidExpression,
                "Negative expression is not declared by its selected owner/source");
    }
}
} // namespace athena::core
