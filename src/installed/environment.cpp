// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "environment.hpp"
#include "../dependency/parser.hpp"
#include "../dependency/constraint.hpp"
#include "../dependency/expression_validation.hpp"
#include "../dependency/serializer.hpp"
#include "../dependency/role_activity.hpp"
#include "../dependency/semantic_assignment.hpp"
#include <functional>
#include <iterator>
#include "../store/metadata.hpp"
#include <algorithm>
#include <set>
#include <toml++/toml.hpp>

namespace athena::installed {
namespace {
namespace dep = athena::dependency;
using K = dep::Expression::Kind;
[[noreturn]] void unsupported() { throw ValidationError("Unsupported installed semantic expression"); }
template<class T> const T& node(const dep::ExpressionPtr& p) {
    const auto* n = dynamic_cast<const T*>(p.get());
    if (!n) unsupported();
    return *n;
}
void validate_dependency(const dep::Dependency& d, bool legacy, bool condition = false) {
    dep::validate_scope(d.kind, d.context);
    if (legacy && (d.kind != dep::DependencyKind::Runtime || d.context != dep::DependencyContext::Target || !d.enabled))
        throw ValidationError("Legacy dependency role/environment state is unavailable");
    validate_expression(d.expression, legacy, condition);
}
bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}
bool compatible(const core::PackageMetadata& p, const core::TargetEnvironment& e) {
    return (p.platforms.empty() || contains(p.platforms, e.platform)) &&
        (p.architectures.empty() || contains(p.architectures, e.architecture) || contains(p.architectures, "any")) &&
        (p.abis.empty() || contains(p.abis, e.abi));
}
bool same_environment(const core::TargetEnvironment& output, const core::TargetEnvironment& required) {
    auto same = [](const std::string& a, const std::string& b) {
        if (a.empty() != b.empty()) throw ValidationError("Installed provider environment state is unavailable");
        return a == b;
    };
    return same(output.platform, required.platform) && same(output.architecture, required.architecture) &&
        same(output.abi, required.abi);
}
bool satisfies(const dep::VersionConstraint& c, const std::string& v, core::VersionScheme scheme) {
    // Existing dependency constraints already delegate to Athena's version
    // module, including Arch epoch/pkgrel semantics. Do not invent an ordering.
    if (scheme != core::VersionScheme::Athena && scheme != core::VersionScheme::Arch) unsupported();
    return dep::satisfies(c, v);
}

using Assignments = std::map<std::string, const SemanticState*>;
std::string assignment_key(const dep::ExpressionPtr& e) {
    if (e->kind == K::Capability) return dep::semantic_assignment_key(dep::SemanticSelectorKind::Capability, node<dep::CapabilityRef>(e).name);
    return dep::semantic_assignment_key(dep::SemanticSelectorKind::Feature, node<dep::FeatureRef>(e).name);
}

struct Evaluator {
    const SemanticState& owner;
    const std::vector<const SemanticState*>& members;
    const Assignments& assignments;
    dep::DependencyResolutionContext policy;
    Evaluator(const SemanticState& s, const std::vector<const SemanticState*>& all, const Assignments& selected,
              const std::map<std::string, bool>& features) : owner(s), members(all), assignments(selected) {
        for (auto r : s.active_roles) policy.active_kinds.push_back(static_cast<dep::DependencyKind>(r));
        policy.optional_enabled = s.optional_enabled; policy.propagated_enabled = s.propagated_enabled;
        policy.feature_states = features;
    }
    const core::TargetEnvironment& environment(dep::DependencyContext c) const {
        switch(c) {
        case dep::DependencyContext::Host: return owner.environments.host;
        case dep::DependencyContext::Build: return owner.environments.build;
        case dep::DependencyContext::Target: return owner.target;
        }
        unsupported();
    }
    bool selector(const dep::ExpressionPtr& e, dep::DependencyContext context, const dep::VersionConstraint* c = nullptr, bool facts = false) const {
        auto candidates = members;
        // Positive requirements use the single resulting assignment. Negative
        // requirements/conflicts inspect all actual facts, as in the resolver:
        // an unselected but installed provider cannot escape NOT/conflicts.
        if (!facts && (e->kind == K::Capability || e->kind == K::Feature)) {
            const auto it = assignments.find(assignment_key(e));
            candidates = it != assignments.end() && it->second
                ? std::vector<const SemanticState*>{it->second} : std::vector<const SemanticState*>{};
        }
        // Unknown capability versions cannot turn into an absence under NOT.
        if (c && e->kind == K::Capability)
            for (const auto* s : members)
                if (s->legacy_restricted && compatible(s->package, environment(context)) &&
                    contains(s->package.capabilities, node<dep::CapabilityRef>(e).name))
                    throw ValidationError("Legacy capability version state is unavailable");
        for (const auto* s : candidates) {
            if (!compatible(s->package, environment(context))) continue;
            bool offers = false;
            switch(e->kind) {
            case K::Package: offers = s->package.package_id.value() == node<dep::PackageRef>(e).name; break;
            case K::Capability: offers = contains(s->package.capabilities, node<dep::CapabilityRef>(e).name); break;
            case K::Feature: offers = contains(s->package.features, node<dep::FeatureRef>(e).name); break;
            default: unsupported();
            }
            if (!offers) continue;
            if (s->legacy_restricted && context != dep::DependencyContext::Target)
                throw ValidationError("Legacy host/build environment state is unavailable");
            // Restricted legacy consumers have no persisted target placement.
            // Their historical package requirements are validated in the target
            // membership set without inventing host/build environment facts.
            if (!owner.legacy_restricted && !s->legacy_restricted &&
                !same_environment(s->target, environment(context))) continue;
            if (!c || satisfies(*c, s->package.version.value(), s->version_scheme)) return true;
        }
        return false;
    }
    bool evaluate(const dep::ExpressionPtr& e, dep::DependencyKind role, dep::DependencyContext context,
                  bool enabled = true, bool condition = false, bool scoped_condition = false,
                  bool conflict = false, bool explicit_scope = false, bool facts = false) const {
        if (condition) {
            if (scoped_condition && !dep::expression_role_active(e, role, enabled, policy)) return false;
        } else if (!dep::expression_role_active(e, role, enabled, policy, !conflict || explicit_scope)) return true;
        switch(e->kind) {
        case K::True: return true;
        case K::False: return false;
        case K::Package:
        case K::Capability: return selector(e, context, nullptr, facts || conflict);
        case K::Feature:
            if (condition) {
                const auto& name = node<dep::FeatureRef>(e).name;
                auto f = policy.feature_states.find(name);
                return f != policy.feature_states.end() ? f->second : contains(owner.package.features, name);
            }
            return selector(e, context, nullptr, facts || conflict);
        case K::Platform:
            if (environment(context).platform.empty()) throw ValidationError("Installed platform state is unavailable");
            return node<dep::PlatformRef>(e).name == environment(context).platform;
        case K::Architecture:
            if (environment(context).architecture.empty()) throw ValidationError("Installed architecture state is unavailable");
            return node<dep::ArchitectureRef>(e).name == environment(context).architecture;
        case K::ABI:
            if (environment(context).abi.empty()) throw ValidationError("Installed ABI state is unavailable");
            return node<dep::ABIRef>(e).name == environment(context).abi;
        case K::VersionComparison: {
            const auto& v = node<dep::VersionComparison>(e);
            return selector(v.target, context, v.constraint.get(), facts || conflict);
        }
        case K::Scoped: {
            const auto& s = node<dep::ScopedExpression>(e);
            if (condition && !dep::expression_role_active(s.body, s.kind_role, s.enabled, policy)) return false;
            return evaluate(s.body, s.kind_role, s.context, s.enabled, condition, condition, conflict, true, facts);
        }
        case K::And: {
            bool result = true;
            for (const auto& x : node<dep::AndExpression>(e).expressions)
                result = evaluate(x, role, context, enabled, condition, scoped_condition, conflict, explicit_scope, facts) && result;
            return result;
        }
        case K::Or: {
            bool result = false;
            for (const auto& x : node<dep::OrExpression>(e).expressions) {
                // Inactive disjuncts are absent alternatives, not true providers.
                if (!condition && !dep::expression_role_active(x, role, enabled, policy, !conflict || explicit_scope)) continue;
                result = evaluate(x, role, context, enabled, condition, scoped_condition, conflict, explicit_scope, facts) || result;
            }
            return result;
        }
        case K::Not: {
            const auto& child = node<dep::NotExpression>(e).expression;
            if (!condition && !dep::expression_role_active(child, role, enabled, policy, !conflict || explicit_scope)) return true;
            return !evaluate(child, role, context, enabled, condition, scoped_condition, conflict, explicit_scope, true);
        }
        case K::Conditional: {
            const auto& c = node<dep::ConditionalExpression>(e);
            if (!evaluate(c.condition, role, context, true, true)) return !conflict;
            return evaluate(c.body, role, context, enabled, false, false, conflict, explicit_scope, facts);
        }
        }
        unsupported();
    }
    bool dependency(const dep::Dependency& d) const { return evaluate(d.expression, d.kind, d.context, d.enabled); }
};
}
void validate_expression(const dep::ExpressionPtr& e, bool legacy, bool condition) {
    try { dep::validate_expression(e, legacy ? dep::ExpressionSupport::Legacy : dep::ExpressionSupport::Complete, condition); }
    catch (const std::runtime_error& error) { throw ValidationError(error.what()); }
}
void capture_legacy_semantics(generation::Generation& g) {
    for (auto& e : g.entries) {
        if (e.semantics) continue;
        const auto doc = toml::parse_file((std::filesystem::path(e.store_path) / ".athena/metadata.toml").string());
        // Older absent fields are unknown, not an assertion of no dependencies.
        if (!doc["dependencies"].is_array()) throw ValidationError("Legacy dependency state is unavailable: " + e.package);
        const auto m = store::read_metadata(e.store_path);
        if (m.name != e.package || m.version.empty()) throw ValidationError("Generation/store package mismatch: " + e.package);
        SemanticState s{core::PackageMetadata{core::PackageId(m.name), core::PackageVersion(m.version)}};
        s.package.dependencies = m.dependencies;
        s.package.capabilities = m.provides.capabilities;
        s.package.features = m.provides.features;
        s.legacy_restricted = true;
        // These fields were not historically persisted by the legacy writer.
        // If supplied externally, do not silently discard them.
        for (const auto* field : {"conflicts", "feature_dependencies", "platforms", "architectures", "abis"})
            if (doc.contains(field)) throw ValidationError("Unsupported legacy semantic metadata: " + std::string(field));
        if (const auto* p = doc["provides"].as_table()) {
            for (const auto& [key, value] : *p) {
                if (key != "capabilities" && key != "features") throw ValidationError("Unsupported legacy provider metadata");
                if (!value.is_array()) throw ValidationError("Invalid legacy provider metadata");
            }
        } else if (doc.contains("provides")) throw ValidationError("Invalid legacy provides table");
        s.validate(); e.semantics = std::move(s);
    }
}
void validate_environment(const generation::Generation& g, const std::vector<SemanticState>& planned_members) {
    g.validate();
    std::vector<const SemanticState*> members;
    std::set<std::string> names;
    for (const auto& e : g.entries) {
        if (!e.semantics) throw ValidationError("Legacy dependency state is unavailable: " + e.package);
        members.push_back(&*e.semantics);
    }
    for (const auto& r : g.realizations) {
        if (!r.semantics) throw ValidationError("Persisted realization dependency state is unavailable: " + r.candidate.package.value());
        members.push_back(&*r.semantics);
    }
    for (const auto& s : planned_members) members.push_back(&s);
    // Establish environment/configuration invariants before evaluating any
    // consumer. Omission inherits explicit values in the same profile; an
    // entirely unconfigured feature retains the resolver's offered-feature
    // guard fallback and does not activate feature dependency declarations.
    std::map<std::string, std::map<std::string, bool>> configured;
    const core::TargetEnvironment* target = nullptr;
    std::sort(members.begin(), members.end(), [](const auto* a, const auto* b) {
        return a->package.package_id < b->package.package_id;
    });
    for (const auto* s : members) {
        s->validate();
        if (!names.insert(s->package.package_id.value()).second) throw ValidationError("Duplicate installed package");
        if (!s->legacy_restricted) {
            if (!compatible(s->package, s->target)) throw ValidationError("Installed package target mismatch");
            if (target && !same_environment(*target, s->target)) throw ValidationError("Incompatible installed target environments");
            target = &s->target;
        }
        for (const auto& [feature, enabled] : s->feature_states) {
            auto [it, inserted] = configured[s->profile].emplace(feature, enabled);
            if (!inserted && it->second != enabled) throw ValidationError("Conflicting installed feature state: " + feature);
            if (enabled && !std::any_of(members.begin(), members.end(), [&](const auto* owner) {
                    return owner->profile == s->profile && (owner->package.feature_dependencies.contains(feature) ||
                        contains(owner->package.features, feature));
                })) throw ValidationError("Undeclared installed feature: " + feature);
        }
    }
    struct Requirement {
        const SemanticState* owner;
        dep::Dependency dependency;
        bool conflict;
        std::string diagnostic;
    };
    std::vector<Requirement> requirements;
    std::map<std::string, std::vector<const SemanticState*>> domains;
    std::function<void(const dep::ExpressionPtr&)> collect = [&](const dep::ExpressionPtr& e) {
        switch (e->kind) {
        case K::Capability:
        case K::Feature: {
            const auto key = assignment_key(e);
            if (domains.contains(key)) return;
            auto& candidates = domains[key];
            for (const auto* provider : members)
                if (e->kind == K::Capability ? contains(provider->package.capabilities, node<dep::CapabilityRef>(e).name)
                    : contains(provider->package.features, node<dep::FeatureRef>(e).name)) candidates.push_back(provider);
            // Absence is an alternative for unused OR branches. Positive
            // selectors never interpret it as a satisfied requirement.
            candidates.push_back(nullptr);
            return;
        }
        case K::VersionComparison: collect(node<dep::VersionComparison>(e).target); return;
        case K::Scoped: collect(node<dep::ScopedExpression>(e).body); return;
        case K::And:
            for (const auto& child : node<dep::AndExpression>(e).expressions) collect(child); return;
        case K::Or:
            for (const auto& child : node<dep::OrExpression>(e).expressions) collect(child); return;
        case K::Conditional: collect(node<dep::ConditionalExpression>(e).body); return;
        // NOT and conflicts evaluate installed facts, not positive assignments.
        default: return;
        }
    };
    auto add = [&](const SemanticState& owner, dep::Dependency d, bool conflict, std::string diagnostic) {
        validate_dependency(d, owner.legacy_restricted);
        if (!conflict) collect(d.expression);
        requirements.push_back({&owner, std::move(d), conflict, std::move(diagnostic)});
    };
    for (const auto* s : members) {
        for (const auto& text : s->package.dependencies)
            add(*s, dep::parse_dependency(text), false,
                "Unsatisfied dependency for package '" + s->package.package_id.value() + "': " + text);
        for (const auto& [feature, declarations] : s->package.feature_dependencies) {
            for (const auto& declaration : declarations) {
                auto d = dep::parse_dependency(declaration.expression.text);
                d.kind = static_cast<dep::DependencyKind>(declaration.role);
                d.context = static_cast<dep::DependencyContext>(declaration.context);
                d.enabled = declaration.enabled;
                // Structural validation includes disabled feature branches.
                validate_dependency(d, false);
                const auto& features = configured[s->profile];
                const auto f = features.find(feature);
                if (f != features.end() && f->second)
                    add(*s, std::move(d), false, "Unsatisfied feature dependency for package '" +
                        s->package.package_id.value() + "': " + declaration.expression.text);
            }
        }
        for (const auto& text : s->package.conflicts)
            add(*s, dep::parse_dependency(text), true,
                "Installed conflict declared by '" + s->package.package_id.value() + "': " + text);
    }
    std::vector<SemanticState> policy_owners;
    policy_owners.reserve(g.policies.size());
    for (const auto& p : g.policies) {
        SemanticState owner{core::PackageMetadata{core::PackageId("policy"), core::PackageVersion("1")}};
        owner.target = p.target; owner.environments = {p.target, p.target};
        policy_owners.push_back(std::move(owner));
        add(policy_owners.back(), dep::parse_dependency(p.selector.text), false, "Unsatisfied system policy: " + p.policy.value());
    }
    Assignments assignments;
    std::string failure;
    auto valid_assignment = [&] {
        for (const auto& r : requirements) {
            Evaluator evaluator(*r.owner, members, assignments, configured[r.owner->profile]);
            const auto& d = r.dependency;
            if (r.conflict) {
                if (dep::expression_role_active(d.expression, d.kind, d.enabled, evaluator.policy, false) &&
                    evaluator.evaluate(d.expression, d.kind, d.context, d.enabled, false, false, true)) {
                    failure = r.diagnostic; return false;
                }
            } else if (!evaluator.dependency(d)) { failure = r.diagnostic; return false; }
        }
        return true;
    };
    // Finite offline assignment search over installed facts only. Sorted keys
    // and PackageIds make the result independent of membership insertion order.
    // No greedy provider can prevent a valid alternative/OR assignment.
    std::function<bool(decltype(domains)::const_iterator)> search = [&](auto it) {
        if (it == domains.end()) return valid_assignment();
        const auto next = std::next(it);
        for (const auto* provider : it->second) {
            assignments[it->first] = provider;
            if (search(next)) return true;
        }
        return false;
    };
    if (!search(domains.cbegin())) throw ValidationError(failure + " (no coherent semantic provider assignment)");

}
void inherit_profile_features(generation::Generation& g, const generation::Generation* previous) {
    std::vector<SemanticState*> states;
    for (auto& e : g.entries) if (e.semantics && !e.semantics->legacy_restricted) states.push_back(&*e.semantics);
    for (auto& r : g.realizations) if (r.semantics) states.push_back(&*r.semantics);
    std::map<std::string, std::map<std::string, bool>> profiles;
    auto merge = [&](const SemanticState& s) {
        for (const auto& [feature, enabled] : s.feature_states) {
            auto [it, added] = profiles[s.profile].emplace(feature, enabled);
            if (!added && it->second != enabled) throw ValidationError("Conflicting installed feature state: " + feature);
        }
    };
    if (previous) {
        for (const auto& e : previous->entries) if (e.semantics && !e.semantics->legacy_restricted) merge(*e.semantics);
        for (const auto& r : previous->realizations) if (r.semantics) merge(*r.semantics);
    }
    for (const auto* s : states) merge(*s);
    for (auto* s : states) s->feature_states = profiles[s->profile];
}
generation::Generation prepare_install(const generation::Generation& current, const core::InstallPlan& plan) {
    plan.validate();
    auto target = current;
    inherit_profile_features(target);
    std::set<std::string> replaced;
    for (const auto& s : plan.selections()) replaced.insert(s.candidate.package.package_id.value());
    std::erase_if(target.entries, [&](const auto& e) { return replaced.contains(e.package); });
    std::erase_if(target.realizations, [&](const auto& r) { return replaced.contains(r.candidate.package.value()); });
    capture_legacy_semantics(target);
    std::vector<SemanticState> additions;
    for (const auto& s : plan.selections()) {
        auto semantics = from_selection(plan, s);
        // Replacing the last configuration carrier must not erase a profile's
        // effective configuration. Explicit contradictory values fail closed.
        for (const auto& r : current.realizations) if (r.semantics && r.semantics->profile == semantics.profile)
            for (const auto& [feature, enabled] : r.semantics->feature_states) {
                auto [it, inserted] = semantics.feature_states.emplace(feature, enabled);
                if (!inserted && it->second != enabled) throw ValidationError("Conflicting installed feature state: " + feature);
            }
        additions.push_back(std::move(semantics));
    }
    auto policies = policies_from_plan(plan);
    std::set<core::SystemPolicyId> changed;
    for (const auto& p : policies) changed.insert(p.policy);
    std::erase_if(target.policies, [&](const auto& p) { return changed.contains(p.policy); });
    target.policies.insert(target.policies.end(), policies.begin(), policies.end());
    validate_environment(target, additions); return target;
}
}
