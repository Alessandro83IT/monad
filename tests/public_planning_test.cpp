// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <athena/core/api.hpp>
#include <cassert>
#include <filesystem>
#include <set>
#include <type_traits>

using namespace athena::core;
namespace
{
PackageMetadata pkg(std::string name, std::vector<std::string> deps = {})
{
    PackageMetadata p{PackageId(name), PackageVersion("1"), SchemaVersion(1)};
    p.dependencies = std::move(deps);
    return p;
}
RepositorySnapshot snapshot(std::string repo, std::vector<PackageMetadata> packages)
{
    std::vector<RepositoryCandidate> candidates;
    for (auto& p : packages)
        candidates.push_back({p,
                              {RepositoryId(repo), RepositoryRevision("rev"),
                               MetadataReference("metadata/" + p.package_id.value())}});
    return RepositorySnapshot::create(RepositoryId(repo), RepositoryRevision("rev"), candidates);
}
const PackageSelection& selected(const ResolutionResult& r, std::string name)
{
    auto it = std::find_if(r.selections.begin(), r.selections.end(), [&](const auto& s)
                           { return s.candidate.package.package_id == PackageId(name); });
    assert(it != r.selections.end());
    return *it;
}
template <class F> void reject(const ResolutionResult& r, InstallPlanErrorCode code, F mutate)
{
    auto broken = r;
    mutate(broken);
    bool caught = false;
    try
    {
        (void)InstallPlanBuilder(std::move(broken)).build();
    }
    catch (const InstallPlanError& e)
    {
        caught = e.code() == code;
    }
    assert(caught);
}
std::set<std::filesystem::path> entries()
{
    std::set<std::filesystem::path> result;
    for (const auto& e : std::filesystem::recursive_directory_iterator("."))
        result.insert(e.path());
    return result;
}
} // namespace
int main()
{
    static_assert(std::is_same_v<decltype(std::declval<InstallPlan&>().resolution()),
                                 const ResolutionResult&>);
    static_assert(std::is_same_v<decltype(std::declval<InstallPlan&>().selections()),
                                 const std::vector<PackageSelection>&>);
    static_assert(!std::is_constructible_v<InstallPlan, ResolutionResult>);
    const auto before = entries();
    auto root =
        pkg("root", {"shared", "capability(tls) >= 1", "NOT A", "NOT (A AND B)", "NOT (A OR B)",
                     "build(build):tool", "test(build):tests", "development(host):dev",
                     "optional(target):opt", "propagated(target):prop"});
    root.features = {"offered"};
    root.conflicts = {"conflicting"};
    root.feature_dependencies["gui"] = {
        {{"extra"}, DependencyRole::Runtime, DependencyTargetContext::Host, true},
        {{"NOT (C OR D)"}, DependencyRole::Runtime, DependencyTargetContext::Target, true}};
    auto provider = pkg("provider");
    provider.capabilities = {"tls"};
    InstallRequest request;
    request.snapshots = {
        snapshot("main", {root, pkg("second", {"shared"}), pkg("shared"), pkg("extra"), pkg("tool"),
                          pkg("tests"), pkg("dev"), pkg("opt"), pkg("prop")}),
        snapshot("providers", {provider, pkg("policy")})};
    for (const auto& s : request.snapshots)
        request.repositories[s.repository_id()] = {};
    request.targets = {{{SelectorKind::Package, "root", {}}},
                       {{SelectorKind::Package, "second", {}}}};
    request.target = {"linux", "x86_64", "glibc", "target-triple"};
    request.environments = {{"linux", "host", "habi", "host-triple"},
                            {"linux", "build", "babi", "build-triple"}};
    request.feature_states = {{"gui", true}, {"disabled", false}};
    request.active_roles = {DependencyRole::Runtime,  DependencyRole::Build,
                            DependencyRole::Test,     DependencyRole::Development,
                            DependencyRole::Optional, DependencyRole::Propagated};
    request.optional_enabled = true;
    request.system_policies = {{SystemPolicyId("base"), {{{SelectorKind::Package, "policy", {}}}}}};
    for (const std::string profile : {"", "desktop"})
    {
        request.profile = profile;
        // M2.1 acceptance: only the public planning operation constructs this plan.
        const auto plan = plan_install(request);
        const auto& r = plan.resolution();
        assert(r.selections.size() == 11);
        assert(r.target == request.target && r.environments == request.environments &&
               r.profile == profile);
        assert(selected(r, "shared").reasons.size() == 2);
        assert(std::count_if(r.edges.begin(), r.edges.end(),
                             [](const auto& e) { return e.target.name == "shared"; }) == 2);
        assert(r.capability_bindings.size() == 1);
        const auto& p = selected(r, "provider");
        assert(r.capability_bindings[0].provider == p.identity());
        assert(p.identity().repository == RepositoryId("providers"));
        assert(p.snapshot == request.snapshots[1].reference());
        assert(p.candidate.provenance.metadata_reference == MetadataReference("metadata/provider"));
        assert(!p.candidate.artifact && !p.candidate.source_recipe);
        assert(r.configured_features.size() == 2 && r.offered_features.size() == 1 &&
               r.activated_features.size() == 1);
        assert(r.activated_features[0].profile == profile &&
               r.activated_features[0].owner == selected(r, "root").identity());
        std::set<DependencyRole> roles;
        std::set<DependencyTargetContext> contexts;
        for (const auto& e : r.edges)
        {
            roles.insert(e.role);
            contexts.insert(e.context);
        }
        assert(roles.size() == 6 && contexts.size() == 3);
        assert(r.system_policy_selections.size() == 1);
        assert(r.system_policy_selections[0].selector ==
               request.system_policies[0].required[0].selector);
        assert(selected(r, "policy").reasons[0].kind == InstallReasonKind::RequiredBySystemPolicy);
        assert(r.negative_constraints.size() == 5);
        std::set<std::string> negatives;
        std::set<NegativeConstraintSourceKind> sources;
        for (const auto& n : r.negative_constraints)
        {
            negatives.insert(n.expression.text);
            sources.insert(n.source.kind);
        }
        assert(negatives ==
               std::set<std::string>({"A", "A AND B", "A OR B", "C OR D", "conflicting"}));
        assert(sources.size() == 3);
        assert(resolve_request(request) == r);
        auto shuffled = r;
        std::reverse(shuffled.selections.begin(), shuffled.selections.end());
        for (auto& s : shuffled.selections)
            std::reverse(s.reasons.begin(), s.reasons.end());
        std::reverse(shuffled.capability_bindings.begin(), shuffled.capability_bindings.end());
        std::reverse(shuffled.configured_features.begin(), shuffled.configured_features.end());
        std::reverse(shuffled.offered_features.begin(), shuffled.offered_features.end());
        std::reverse(shuffled.activated_features.begin(), shuffled.activated_features.end());
        std::reverse(shuffled.edges.begin(), shuffled.edges.end());
        std::reverse(shuffled.negative_constraints.begin(), shuffled.negative_constraints.end());
        std::reverse(shuffled.system_policy_selections.begin(),
                     shuffled.system_policy_selections.end());
        assert(InstallPlanBuilder(shuffled).build().resolution() == r);
        auto reordered = request;
        std::reverse(reordered.snapshots.begin(), reordered.snapshots.end());
        std::reverse(reordered.targets.begin(), reordered.targets.end());
        assert(plan_install(reordered).resolution() == r);
        reject(r, InstallPlanErrorCode::InvalidBinding, [](auto& b)
               { b.capability_bindings[0].provider.repository = RepositoryId("absent"); });
        reject(r, InstallPlanErrorCode::InvalidBinding,
               [](auto& b) { b.capability_bindings[0].selector.name = "absent"; });
        reject(r, InstallPlanErrorCode::InvalidEdge,
               [](auto& b) { b.edges[0].source.repository = RepositoryId("absent"); });
        reject(r, InstallPlanErrorCode::InvalidEdge, [](auto& b) { b.edges[0].provider.reset(); });
        reject(r, InstallPlanErrorCode::InvalidEdge,
               [](auto& b) { b.edges[0].role = static_cast<DependencyRole>(99); });
        reject(r, InstallPlanErrorCode::InvalidEdge,
               [](auto& b) { b.edges[0].target.name = "wrong"; });
        reject(r, InstallPlanErrorCode::InvalidEdge,
               [](auto& b) { b.edges[0].context = static_cast<DependencyTargetContext>(99); });
        reject(r, InstallPlanErrorCode::InvalidReason,
               [](auto& b) { b.edges[0].reason->package = selected(b, "shared").identity(); });
        reject(r, InstallPlanErrorCode::DuplicatePackage,
               [](auto& b) { b.selections.push_back(b.selections.front()); });
        reject(r, InstallPlanErrorCode::InvalidReason,
               [](auto& b)
               {
                   for (auto& s : b.selections)
                       for (auto& why : s.reasons)
                           if (why.package)
                               why.package->repository = RepositoryId("absent");
               });
        reject(r, InstallPlanErrorCode::InvalidReason,
               [](auto& b) { b.system_policy_selections[0].selector.name = "wrong"; });
        reject(r, InstallPlanErrorCode::InvalidReason,
               [](auto& b) { b.system_policy_selections.clear(); });
        reject(r, InstallPlanErrorCode::InvalidExpression,
               [](auto& b)
               {
                   b.negative_constraints[0].source.kind =
                       static_cast<NegativeConstraintSourceKind>(99);
               });
        reject(r, InstallPlanErrorCode::InvalidExpression,
               [](auto& b)
               {
                   for (auto& n : b.negative_constraints)
                       if (n.source.kind == NegativeConstraintSourceKind::FeatureDependency)
                           n.source.feature = "wrong";
               });
        reject(r, InstallPlanErrorCode::RepositoryMismatch,
               [](auto& b) { b.selections[0].snapshot.repository_id = RepositoryId("wrong"); });
        reject(r, InstallPlanErrorCode::InvalidFeature,
               [](auto& b) { b.offered_features[0].provider.repository = RepositoryId("absent"); });
        reject(r, InstallPlanErrorCode::InvalidFeature,
               [](auto& b) { b.configured_features.push_back({"gui", b.profile, false}); });
        reject(r, InstallPlanErrorCode::InvalidFeature,
               [](auto& b)
               {
                   b.activated_features.push_back(
                       {"gui", b.profile, selected(b, "shared").identity()});
               });
        auto missing = request;
        missing.targets = {{{SelectorKind::Package, "absent", {}}}};
        bool precise = false;
        try
        {
            (void)plan_install(missing);
        }
        catch (const ResolutionError& e)
        {
            precise = e.code() == ResolutionErrorCode::UnknownPackageRoot;
        }
        assert(precise);
    }
    assert(entries() == before);
}
