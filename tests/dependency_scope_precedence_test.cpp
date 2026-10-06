// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dependency/parser.hpp"
#include "dependency/resolver.hpp"
#include "repository/repository_index.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace athena;
using Role = dependency::DependencyKind;
using Context = dependency::DependencyContext;
using Plan = install::InstallPlan;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

package::Package pkg(const std::string& name, const std::string& version = "1") {
    package::Package result;
    result.name = name;
    result.version = version;
    return result;
}

dependency::Dependency edge(const std::string& text, Role role = Role::Build,
                            Context context = Context::Build, bool enabled = true) {
    // Parentheses preserve an actual nested ScopedExpression instead of the
    // parser's top-level metadata header syntax.
    return {dependency::parse_dependency("(" + text + ")").expression, role, context, enabled};
}

bool has(const Plan& plan, const std::string& name) {
    return std::any_of(plan.packages().begin(), plan.packages().end(),
        [&](const auto& p) { return p.name == name; });
}

bool in_context(const Plan& plan, const std::string& name, Context context) {
    for (std::size_t i = 0; i < plan.packages().size(); ++i)
        if (plan.packages()[i].name == name)
            return plan.contexts()[i] == std::vector<Context>{context};
    return false;
}

void rejects(const package::Package& root, const repository::RepositoryIndex& repo,
             const dependency::Environments& env, const dependency::DependencyResolutionContext& policy) {
    try { (void)dependency::resolve_plan(root, repo, env, policy); }
    catch (const dependency::ResolutionError&) { return; }
    throw std::runtime_error("Expected resolution failure");
}
}

int main() {
    const dependency::Environments env{
        {"host-os", "host-arch", "host-abi"},
        {"build-os", "build-arch", "build-abi"},
        {"target-os", "target-arch", "target-abi"}};
    repository::RepositoryIndex repo;
    for (const auto* name : {"tool", "A", "B", "side", "blocked", "wrong"}) repo.add(pkg(name));
    dependency::DependencyResolutionContext runtime;
    runtime.active_kinds = {Role::Runtime};
    const auto solve = [&](const dependency::Dependency& d, const auto& policy) {
        auto root = pkg("root"); root.dependencies = {d};
        return dependency::resolve_plan(root, repo, env, policy);
    };

    // A/B: the explicit role wins in either direction.
    require(has(solve(edge("runtime(target):(tool)"), runtime), "tool"), "Runtime must override inactive Build");
    require(!has(solve(edge("build(build):(tool)", Role::Runtime, Context::Target), runtime), "tool"),
            "Inactive explicit Build must override active Runtime");

    // C: role and environment are independent; all selector dimensions use Host.
    auto host = solve(edge("runtime(host):(tool AND platform(host-os) AND architecture(host-arch) AND abi(host-abi))"), runtime);
    require(in_context(host, "tool", Context::Host), "Runtime/Host context not retained");
    require(in_context(solve(edge("runtime(build):(tool AND platform(build-os))"), runtime), "tool", Context::Build),
            "Runtime role must not imply Target context");

    // D: a deeper active Test scope is reachable through inactive Runtime/Build.
    auto test = runtime; test.active_kinds = {Role::Test};
    auto nested = solve(edge("runtime(host):(test(target):(tool AND platform(target-os) AND architecture(target-arch) AND abi(target-abi)))"), test);
    require(in_context(nested, "tool", Context::Target), "Deepest Test/Target scope must win");

    // E: sibling scopes neither rewrite metadata nor leak role/context.
    const auto sibling_edge = edge("runtime(host):(A) AND B AND platform(build-os)");
    require(has(solve(sibling_edge, runtime), "A") && !has(solve(sibling_edge, runtime), "B"),
            "B must inherit inactive Build");
    auto both = runtime; both.active_kinds.push_back(Role::Build);
    const auto siblings = solve(sibling_edge, both);
    require(in_context(siblings, "A", Context::Host) && in_context(siblings, "B", Context::Build),
            "Sibling context was not restored");
    auto build_only = runtime; build_only.active_kinds = {Role::Build};
    const auto build_siblings = solve(sibling_edge, build_only);
    require(!has(build_siblings, "A") && in_context(build_siblings, "B", Context::Build),
            "Inactive sibling scope rewrote enclosing Build role");
    require(sibling_edge.kind == Role::Build && sibling_edge.context == Context::Build,
            "Metadata was mutated");

    // F: a failed OR branch rolls back selected packages, contexts and negatives.
    const auto fallback = solve(edge(
        "(runtime(host):(side AND NOT blocked AND NOT (blocked AND wrong) AND missing) OR B) AND blocked"), both);
    require(!has(fallback, "side") && has(fallback, "blocked"), "Failed OR state leaked");
    require(in_context(fallback, "B", Context::Build) && in_context(fallback, "blocked", Context::Build),
            "OR fallback inherited a failed scope");
    const auto active_alternative = solve(edge("build(host):(missing) OR runtime(target):(tool)"), runtime);
    require(in_context(active_alternative, "tool", Context::Target), "Inactive OR branch became a winner");
    require(has(solve(edge("(runtime(host):(tool) AND missing) OR runtime(target):(tool)"), runtime), "tool"),
            "Nested active scope in an OR/AND branch was hidden");
    require(in_context(solve(edge("(runtime(host):(tool) AND build(build):(missing)) OR runtime(target):(A)"), runtime),
                       "tool", Context::Host), "Inactive AND child changed OR semantics");

    auto inactive_or = pkg("inactive-or");
    inactive_or.dependencies = {edge("build(host):(missing) OR optional(target):(missing)", Role::Runtime, Context::Target)};
    rejects(inactive_or, repo, env, runtime);

    // Conditional guard truth and configured-feature precedence are unchanged.
    require(in_context(solve(edge("when(platform(build-os), runtime(host):(tool))"), runtime), "tool", Context::Host),
            "Conditional body scope was hidden by enclosing Build");
    require(!has(solve(edge("when(false, runtime(host):(tool))"), runtime), "tool"), "False guard was ignored");
    require(has(solve(edge("when(build(host):(runtime(target):(platform(target-os))), runtime(host):(tool))"), runtime), "tool"),
            "Nested condition scope was hidden");
    require(!has(solve(edge("when(optional-disabled(host):(runtime(target):(true)), runtime(host):(tool))"), runtime), "tool"),
            "Disabled condition scope was re-enabled");
    require(has(solve(edge("when(runtime(host):(platform(host-os)) AND platform(build-os), runtime(target):(tool))"), runtime), "tool"),
            "Condition sibling context leaked");
    require(!has(solve(edge("when(build(host):(platform(host-os) AND runtime(target):(true)), runtime(target):(tool))"), runtime), "tool"),
            "Inactive condition leaf must retain inherited Build role");
    require(has(solve(edge("when(build(host):(platform(host-os) AND runtime(target):(true)), runtime(target):(tool))"), both), "tool"),
            "Active condition leaf lost inherited Build role");
    auto feature_root = pkg("feature-root"); feature_root.provides.features = {"extras"};
    feature_root.feature_dependencies["extras"] = {};
    feature_root.dependencies = {edge("when(feature(extras), runtime(host):(tool))")};
    auto feature_off = runtime; feature_off.feature_states["extras"] = false;
    require(!has(dependency::resolve_plan(feature_root, repo, env, feature_off), "tool"), "Configured false lost precedence");
    auto feature_on = runtime; feature_on.feature_states["extras"] = true;
    require(has(dependency::resolve_plan(feature_root, repo, env, feature_on), "tool"), "Configured feature did not reach scoped body");

    // Optional/Propagated still use their existing switches, not a rewritten role.
    require(!has(solve(edge("optional(host):(tool)"), runtime), "tool"), "Optional default changed");
    auto optional = runtime; optional.optional_enabled = true;
    require(in_context(solve(edge("optional(host):(tool)"), optional), "tool", Context::Host), "Optional switch/context lost");
    auto propagated = runtime; propagated.active_kinds.push_back(Role::Propagated);
    require(has(solve(edge("propagated(target):(tool)"), propagated), "tool"), "Propagated was not enabled");
    propagated.propagated_enabled = false;
    require(!has(solve(edge("propagated(target):(tool)"), propagated), "tool"), "Propagated switch was ignored");
    require(has(solve(edge("optional(host):(runtime(target):(tool))"), runtime), "tool"), "Inactive Optional hid Runtime override");
    require(!has(solve(edge("optional-disabled(host):(runtime(target):(tool))"), optional), "tool"), "Disabled scope was re-enabled");
    require(!has(solve(edge("runtime(host):(tool)", Role::Build, Context::Build, false), runtime), "tool"),
            "Disabled metadata was re-enabled");

    // G: NOT traverses effective scopes and retains the enclosing role for siblings.
    auto forbidden = pkg("forbidden");
    forbidden.dependencies = {edge("NOT build(host):(runtime(target):(tool))"), edge("runtime(target):(tool)")};
    rejects(forbidden, repo, env, runtime);
    forbidden.dependencies = {edge("NOT runtime(host):(test(target):(tool))"), edge("test(target):(tool)")};
    rejects(forbidden, repo, env, test);
    forbidden.dependencies = {edge("NOT build(host):(tool)", Role::Runtime, Context::Target), edge("runtime(target):(tool)")};
    require(has(dependency::resolve_plan(forbidden, repo, env, runtime), "tool"), "Inactive NOT scope forbade tool");
    forbidden.dependencies = {edge("NOT (runtime(target):(wrong) OR B)"), edge("runtime(target):(B)")};
    require(has(dependency::resolve_plan(forbidden, repo, env, runtime), "B"), "NOT sibling did not inherit inactive Build");
    forbidden.dependencies = {edge("NOT (runtime(target):(tool) AND B)"), edge("runtime(target):(tool)")};
    rejects(forbidden, repo, env, runtime);
    require(!has(solve(edge("NOT build(host):(runtime(target):(platform(wrong-os)))"), runtime), "tool"),
            "Negative environment scope failed");
    auto negative_env = pkg("negative-env");
    negative_env.dependencies = {edge("NOT build(host):(runtime(target):(platform(target-os)))")};
    rejects(negative_env, repo, env, runtime);

    require(!has(solve(edge("NOT build(host):(when(platform(host-os), runtime(target):(tool)))"), runtime), "tool"),
            "Negative conditional guard was incorrectly role-gated");
    forbidden.dependencies = {edge("NOT build(host):(when(platform(host-os), runtime(target):(tool)))"),
                              edge("runtime(target):(tool)")};
    rejects(forbidden, repo, env, runtime);

    auto conflict_owner = pkg("conflict-owner");
    conflict_owner.conflicts = {edge("build(host):(runtime(target):(tool AND platform(target-os)))")};
    repo.add(conflict_owner);
    auto conflicts = pkg("conflicts");
    conflicts.dependencies = {edge("runtime(target):(conflict-owner AND tool)")};
    rejects(conflicts, repo, env, runtime);
    auto inactive_conflict = pkg("inactive-conflict");
    inactive_conflict.conflicts = {edge("build(host):(tool)", Role::Runtime, Context::Target)};
    repo.add(inactive_conflict);
    require(has(solve(edge("runtime(target):(inactive-conflict AND tool)"), runtime), "tool"),
            "Entirely inactive conflict became vacuously true");
    auto conditional_conflict = pkg("conditional-conflict");
    conditional_conflict.conflicts = {edge("when(build(host):(runtime(target):(platform(target-os))), runtime(host):(tool))")};
    repo.add(conditional_conflict);
    conflicts.dependencies = {edge("runtime(target):(conditional-conflict AND tool)")};
    rejects(conflicts, repo, env, runtime);

    auto unscoped_conflict = pkg("unscoped-conflict");
    unscoped_conflict.conflicts = {edge("tool AND platform(build-os)")};
    repo.add(unscoped_conflict);
    conflicts.dependencies = {edge("test(target):(unscoped-conflict AND tool)")};
    rejects(conflicts, repo, env, test);
    // Global backtracking retains no contexts from the rejected scoped candidate.
    auto high = pkg("choice", "2");
    high.dependencies = {edge("runtime(host):(tool)")};
    high.conflicts = {edge("runtime(target):(blocked)")};
    auto low = pkg("choice", "1"); low.dependencies = {edge("runtime(target):(tool)")};
    repo.add(high); repo.add(low);
    const auto backtracked = solve(edge("runtime(build):(choice) AND runtime(target):(blocked)"), runtime);
    require(in_context(backtracked, "tool", Context::Target), "Rejected candidate's Host context leaked");
    const auto choice = std::find_if(backtracked.packages().begin(), backtracked.packages().end(), [](const auto& p) { return p.name == "choice"; });
    require(choice != backtracked.packages().end() && choice->version == "1", "Global candidate backtracking failed");

    // Configured feature dependencies enter the same scope evaluator.
    auto configured = pkg("configured");
    configured.feature_dependencies["extras"] = {edge("runtime(host):(tool)")};
    require(in_context(dependency::resolve_plan(configured, repo, env, feature_on), "tool", Context::Host),
            "Feature dependency scope did not override metadata");
    std::cout << "dependency_scope_precedence_test: passed\n";
}
