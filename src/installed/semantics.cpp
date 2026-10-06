// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "semantics.hpp"
#include "../dependency/parser.hpp"
#include "../dependency/serializer.hpp"
#include "../dependency/constraint.hpp"
#include "../realization/identity_codec.hpp"
#include <algorithm>
#include <locale>
#include <set>
#include <sstream>

namespace athena::installed {
namespace {
using namespace core;
namespace dep = athena::dependency;
void keys(const toml::table& t, std::initializer_list<std::string_view> allowed) {
    for (const auto& [k, v] : t) {
        (void)v;
        if (std::find(allowed.begin(), allowed.end(), k.str()) == allowed.end())
            throw TypeError("Unknown installed semantic field: " + std::string(k.str()));
    }
}
std::string text(const toml::table& t, const char* k) {
    auto v = t[k].value<std::string>();
    if (!v) throw TypeError(std::string("Invalid installed semantic string: ") + k);
    return *v;
}
bool boolean(const toml::table& t, const char* k) {
    auto v = t[k].value<bool>();
    if (!v) throw TypeError(std::string("Invalid installed semantic boolean: ") + k);
    return *v;
}
int integer(const toml::table& t, const char* k, int maximum) {
    if (!t[k].is_integer()) throw TypeError(std::string("Invalid installed semantic integer: ") + k);
    auto v = t[k].value<int>();
    if (!v || *v < 0 || *v > maximum) throw TypeError(std::string("Invalid installed semantic enum: ") + k);
    return *v;
}
const toml::table& table(const toml::table& t, const char* k) {
    auto v = t[k].as_table();
    if (!v) throw TypeError(std::string("Invalid installed semantic table: ") + k);
    return *v;
}
const toml::array& array(const toml::table& t, const char* k) {
    auto v = t[k].as_array();
    if (!v) throw TypeError(std::string("Invalid installed semantic array: ") + k);
    return *v;
}
std::string format(const toml::table& t) {
    std::ostringstream out; out.imbue(std::locale::classic()); out << t; return out.str();
}
toml::table environment(const TargetEnvironment& e) {
    return toml::table{{"platform", e.platform}, {"architecture", e.architecture}, {"abi", e.abi}, {"triple", e.triple}};
}
TargetEnvironment environment(const toml::table& t) {
    keys(t, {"platform", "architecture", "abi", "triple"});
    return {text(t, "platform"), text(t, "architecture"), text(t, "abi"), text(t, "triple")};
}
void expression(const DependencyExpression& e) {
    try { (void)dep::parse_dependency(e.text); }
    catch (const std::exception& error) { throw TypeError("Invalid installed expression: " + std::string(error.what())); }
}
dep::VersionConstraint constraint(const core::VersionConstraint& c) {
    using V = dep::VersionConstraint;
    switch (c.kind) {
    case VersionConstraintKind::Any:
        if (!c.version.empty() || !c.operands.empty()) throw TypeError("Invalid any constraint");
        return V::any();
    case VersionConstraintKind::Comparison: {
        if (c.version.empty() || !c.operands.empty()) throw TypeError("Invalid comparison constraint");
        dep::ComparisonOperator op;
        switch(c.comparison) {
        case VersionComparisonOperator::Equal: op = dep::ComparisonOperator::Equal; break;
        case VersionComparisonOperator::NotEqual: op = dep::ComparisonOperator::NotEqual; break;
        case VersionComparisonOperator::Less: op = dep::ComparisonOperator::Less; break;
        case VersionComparisonOperator::LessEqual: op = dep::ComparisonOperator::LessEqual; break;
        case VersionComparisonOperator::Greater: op = dep::ComparisonOperator::Greater; break;
        case VersionComparisonOperator::GreaterEqual: op = dep::ComparisonOperator::GreaterEqual; break;
        default: throw TypeError("Unsupported comparison operator");
        }
        return V::comparison(op, c.version);
    }
    case VersionConstraintKind::Not:
        if (!c.version.empty() || c.operands.size() != 1) throw TypeError("Invalid NOT constraint");
        return V::not_(constraint(c.operands.front()));
    case VersionConstraintKind::And:
    case VersionConstraintKind::Or: {
        if (!c.version.empty() || c.operands.empty()) throw TypeError("Invalid composed constraint");
        std::vector<V> children;
        for (const auto& child : c.operands) children.push_back(constraint(child));
        return c.kind == VersionConstraintKind::And ? V::all(std::move(children)) : V::any_of(std::move(children));
    }
    }
    throw TypeError("Unsupported version constraint");
}
void atomic_selector(const DependencyExpression& value) {
    // Wrapping prevents parse_dependency from extracting/discarding an outer
    // scope. A scoped value either remains a wrapper or fails scoped grammar.
    dep::ExpressionPtr atom;
    try { atom = dep::parse_dependency("(" + value.text + ")").expression; }
    catch (const std::runtime_error& error) {
        throw TypeError("Expected unscoped installed selector expression: " + std::string(error.what()));
    }
    if (const auto* v = dynamic_cast<const dep::VersionComparison*>(atom.get())) atom = v->target;
    if (!atom || (atom->kind != dep::Expression::Kind::Package && atom->kind != dep::Expression::Kind::Capability &&
                  atom->kind != dep::Expression::Kind::Feature))
        throw TypeError("Expected unscoped installed selector expression");
}
}
core::DependencyExpression selector_expression(const core::DependencySelector& s) {
    dep::ExpressionPtr e;
    if (s.name.empty()) throw TypeError("Empty installed selector");
    switch (s.kind) {
    case core::SelectorKind::Package: e = std::make_shared<dep::PackageRef>(s.name); break;
    case core::SelectorKind::Capability: e = std::make_shared<dep::CapabilityRef>(s.name); break;
    case core::SelectorKind::Feature: e = std::make_shared<dep::FeatureRef>(s.name); break;
    default: throw TypeError("Unsupported installed selector kind");
    }
    const auto c = constraint(s.constraint);
    if (c.kind != dep::VersionConstraint::Kind::Any)
        e = std::make_shared<dep::VersionComparison>(e, std::make_shared<dep::VersionConstraint>(c));
    core::DependencyExpression result{dep::serialize_expression(e)};
    atomic_selector(result);
    return result;
}
void SemanticState::validate() const {
    package.validate();
    if (version_scheme != core::VersionScheme::Athena && version_scheme != core::VersionScheme::Arch)
        throw core::TypeError("Unsupported installed version scheme");
    if (active_roles.empty()) throw core::TypeError("Missing installed dependency roles");
    std::set<core::DependencyRole> roles;
    for (const auto r : active_roles)
        if (static_cast<unsigned>(r) > static_cast<unsigned>(core::DependencyRole::Propagated) || !roles.insert(r).second)
            throw core::TypeError("Invalid/duplicate installed dependency role");
    for (const auto& [name, enabled] : feature_states) {
        (void)enabled;
        if (name.empty()) throw core::TypeError("Empty installed feature name");
    }
    for (const auto& b : bindings) {
        expression(b.selector); atomic_selector(b.selector);
        if (static_cast<unsigned>(b.role) > static_cast<unsigned>(core::DependencyRole::Propagated) ||
            static_cast<unsigned>(b.context) > static_cast<unsigned>(core::DependencyTargetContext::Target))
            throw core::TypeError("Invalid installed provider scope");
        realization::validate_candidate(b.provider);
    }
    if (legacy_restricted && (!feature_states.empty() || !package.feature_dependencies.empty() ||
        !package.conflicts.empty() || !bindings.empty())) throw core::TypeError("Legacy metadata cannot claim complete semantics");
}
SemanticState from_selection(const core::InstallPlan& plan, const core::PackageSelection& selection) {
    SemanticState s{selection.candidate.package};
    const auto& r = plan.result();
    s.target = r.target; s.environments = r.environments; s.profile = r.profile;
    s.active_roles = r.active_roles; s.optional_enabled = r.optional_enabled; s.propagated_enabled = r.propagated_enabled;
    if (auto it = r.version_schemes.find(selection.candidate.provenance.repository_id); it != r.version_schemes.end()) s.version_scheme = it->second;
    for (const auto& f : r.configured_features) {
        if (f.profile != s.profile || !s.feature_states.emplace(f.feature, f.enabled).second)
            throw core::TypeError("Contradictory installed feature configuration");
    }
    for (const auto& e : r.edges)
        if (e.source == selection.identity() && e.provider)
            s.bindings.push_back({selector_expression(e.target), *e.provider, e.role, e.context});
    // Include explicit capability selectors when this package is their selected provider.
    for (const auto& b : r.capability_bindings)
        if (b.provider == selection.identity()) s.bindings.push_back({selector_expression(b.selector), b.provider});
    s.validate(); return s;
}
std::vector<PolicyRequirement> policies_from_plan(const core::InstallPlan& plan) {
    std::vector<PolicyRequirement> result;
    for (const auto& p : plan.system_policy_selections()) {
        PolicyRequirement value{p.policy, selector_expression(p.selector), plan.target()};
        if (std::find(result.begin(), result.end(), value) == result.end()) result.push_back(std::move(value));
    }
    return result;
}
std::string serialize(const SemanticState& s) {
    s.validate();
    toml::array roles, bindings;
    for (auto r : s.active_roles) roles.push_back(static_cast<int>(r));
    for (const auto& b : s.bindings) bindings.push_back(toml::table{
        {"selector", b.selector.text}, {"provider", realization::encode_candidate(b.provider)},
        {"role", static_cast<int>(b.role)}, {"context", static_cast<int>(b.context)}});
    toml::table features;
    for (const auto& [name, enabled] : s.feature_states) features.insert(name, enabled);
    return format(toml::table{{"schema_version", 1}, {"package", toml::parse(core::serialize(s.package))},
        {"version_scheme", static_cast<int>(s.version_scheme)}, {"target", environment(s.target)},
        {"host", environment(s.environments.host)}, {"build", environment(s.environments.build)},
        {"profile", s.profile}, {"active_roles", std::move(roles)}, {"optional_enabled", s.optional_enabled},
        {"propagated_enabled", s.propagated_enabled}, {"features", std::move(features)},
        {"bindings", std::move(bindings)}, {"legacy_restricted", s.legacy_restricted}});
}
SemanticState deserialize_semantics(const std::string& document) {
    try {
        const auto t = toml::parse(document);
        keys(t, {"schema_version", "package", "version_scheme", "target", "host", "build", "profile",
                 "active_roles", "optional_enabled", "propagated_enabled", "features", "bindings", "legacy_restricted"});
        if (integer(t, "schema_version", 1) != 1) throw core::TypeError("Unsupported installed semantic schema");
        SemanticState s{core::deserialize_package_metadata(format(table(t, "package")))};
        s.version_scheme = static_cast<core::VersionScheme>(integer(t, "version_scheme", 1));
        s.target = environment(table(t, "target"));
        s.environments = {environment(table(t, "host")), environment(table(t, "build"))};
        s.profile = text(t, "profile");
        s.optional_enabled = boolean(t, "optional_enabled"); s.propagated_enabled = boolean(t, "propagated_enabled");
        s.legacy_restricted = boolean(t, "legacy_restricted");
        s.active_roles.clear();
        for (const auto& r : array(t, "active_roles")) {
            auto v = r.value<int>();
            if (!r.is_integer() || !v || *v < 0 || *v > 5) throw core::TypeError("Invalid installed dependency role");
            s.active_roles.push_back(static_cast<core::DependencyRole>(*v));
        }
        for (const auto& [name, value] : table(t, "features")) {
            auto v = value.value<bool>();
            if (!v) throw core::TypeError("Invalid installed feature state");
            s.feature_states.emplace(std::string(name.str()), *v);
        }
        for (const auto& value : array(t, "bindings")) {
            auto b = value.as_table(); if (!b) throw core::TypeError("Invalid provider binding");
            keys(*b, {"selector", "provider", "role", "context"});
            s.bindings.push_back({{text(*b, "selector")}, realization::decode_candidate(table(*b, "provider")),
                static_cast<core::DependencyRole>(integer(*b, "role", 5)),
                static_cast<core::DependencyTargetContext>(integer(*b, "context", 2))});
        }
        s.validate(); return s;
    } catch (const toml::parse_error& e) { throw core::TypeError(e.what()); }
}
std::string serialize(const PolicyRequirement& p) {
    if (p.policy.empty()) throw core::TypeError("Empty installed policy");
    expression(p.selector); atomic_selector(p.selector);
    return format(toml::table{{"schema_version", 1}, {"policy", p.policy.value()},
        {"selector", p.selector.text}, {"target", environment(p.target)}});
}
PolicyRequirement deserialize_policy(const std::string& document) {
    try {
        const auto t = toml::parse(document);
        keys(t, {"schema_version", "policy", "selector", "target"});
        if (integer(t, "schema_version", 1) != 1) throw core::TypeError("Unsupported installed policy schema");
        PolicyRequirement p{core::SystemPolicyId(text(t, "policy")), {text(t, "selector")}, environment(table(t, "target"))};
        (void)serialize(p); return p;
    } catch (const toml::parse_error& e) { throw core::TypeError(e.what()); }
}
}
