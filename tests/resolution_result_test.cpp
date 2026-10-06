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

using namespace athena::core;
namespace
{
PackageMetadata pkg(std::string name, std::string version = "1",
                    std::vector<std::string> dependencies = {})
{
    PackageMetadata p{PackageId(name), PackageVersion(version), SchemaVersion(1)};
    p.dependencies = std::move(dependencies);
    return p;
}
RepositorySnapshot snapshot(std::string repo, std::vector<PackageMetadata> packages,
                            std::string rev = "rev")
{
    std::vector<RepositoryCandidate> candidates;
    for (auto& p : packages)
        candidates.push_back({p,
                              {RepositoryId(repo), RepositoryRevision(rev),
                               MetadataReference("meta/" + p.package_id.value())}});
    return RepositorySnapshot::create(RepositoryId(repo), RepositoryRevision(rev),
                                      std::move(candidates));
}
InstallTarget target(std::string name, SelectorKind kind = SelectorKind::Package)
{
    return {{kind, name, {}}};
}
InstallRequest request(std::vector<RepositorySnapshot> snapshots,
                       std::vector<InstallTarget> targets)
{
    InstallRequest r;
    r.snapshots = std::move(snapshots);
    r.targets = std::move(targets);
    for (const auto& s : r.snapshots)
        r.repositories[s.repository_id()] = {};
    r.target = {"target", "ta", "tabi", "target-triple"};
    r.environments = {{"host", "ha", "habi", "host-triple"},
                      {"build", "ba", "babi", "build-triple"}};
    return r;
}
const PackageSelection& selected(const ResolutionResult& r, std::string name)
{
    const auto it = std::find_if(r.selections.begin(), r.selections.end(), [&](const auto& s)
                                 { return s.candidate.package.package_id == PackageId(name); });
    assert(it != r.selections.end());
    return *it;
}
const InstallReason& reason(const PackageSelection& s, InstallReasonKind kind)
{
    const auto it = std::find_if(s.reasons.begin(), s.reasons.end(),
                                 [&](const auto& r) { return r.kind == kind; });
    assert(it != s.reasons.end());
    return *it;
}
ResolutionResult solve(const InstallRequest& r)
{
    auto result = resolve_request(r);
    assert(result.target == r.target && result.environments == r.environments &&
           result.profile == r.profile);
    assert(InstallPlanBuilder(result).build().result() == result);
    return result;
}
template <class F> void invalid(F f, InstallPlanErrorCode code)
{
    bool caught = false;
    try
    {
        f();
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
    const auto before = entries();
    auto owner = pkg("root", "1", {"dep", "when(feature(gui), guarded)"});
    owner.features = {"gui", "offered"};
    owner.feature_dependencies["gui"] = {
        {{"extra"}, DependencyRole::Runtime, DependencyTargetContext::Host, true}};
    auto owner2 = pkg("second", "1", {"dep"});
    owner2.feature_dependencies["gui"] = {
        {{"extra"}, DependencyRole::Runtime, DependencyTargetContext::Target, true}};
    auto r = request({snapshot("r", {owner, owner2, pkg("dep"), pkg("extra"), pkg("guarded")})},
                     {target("root"), target("second")});
    r.feature_states = {{"gui", true}, {"disabled", false}};
    r.system_policies = {{SystemPolicyId("base"), {target("root")}}};
    for (const auto& profile : std::vector<std::string>{"", "desktop"})
    {
        r.profile = profile;
        const auto result = solve(r);
        const auto& root = selected(result, "root");
        const auto& second = selected(result, "second");
        assert(reason(root, InstallReasonKind::ExplicitRequest).explicit_target ==
               r.targets.front());
        assert(reason(root, InstallReasonKind::RequiredBySystemPolicy).policy ==
               SystemPolicyId("base"));
        assert(root.reasons.size() == 2 && result.system_policy_selections.size() == 1);
        assert(result.system_policy_selections.front() ==
               (SystemPolicySelection{SystemPolicyId("base"), r.targets.front().selector,
                                      root.identity()}));
        const auto& dep = selected(result, "dep");
        assert(dep.reasons.size() == 2);
        assert(reason(dep, InstallReasonKind::DependencyOf).package);
        const auto& extra = selected(result, "extra");
        assert(extra.reasons.size() == 2);
        for (const auto& why : extra.reasons)
        {
            assert(why.kind == InstallReasonKind::EnabledByFeature && why.feature == "gui" &&
                   why.profile == profile);
            assert(why.package == root.identity() || why.package == second.identity());
        }
        assert(result.configured_features.size() == 2 && result.offered_features.size() == 2 &&
               result.activated_features.size() == 2);
        assert(result.edges.size() == 5 && result.selections.size() == 5);
        const auto edge =
            std::find_if(result.edges.begin(), result.edges.end(), [&](const auto& e)
                         { return e.source == root.identity() && e.target.name == "extra"; });
        assert(edge != result.edges.end() && edge->provider == extra.identity() &&
               edge->context == DependencyTargetContext::Host &&
               edge->reason == reason(extra, InstallReasonKind::EnabledByFeature));
        assert(root.candidate ==
               r.snapshots.front().candidates().at(
                   3)); // canonical package order: dep, extra, guarded, root, second
        assert(root.snapshot == r.snapshots.front().reference() &&
               root.identity().provenance == root.candidate.provenance);
        assert(!root.candidate.artifact && !root.candidate.source_recipe);
        auto broken = result;
        broken.system_policy_selections.clear();
        invalid([&] { InstallPlanBuilder(broken).build(); }, InstallPlanErrorCode::InvalidReason);
        broken = result;
        broken.system_policy_selections.front().selector.name = "dep";
        invalid([&] { InstallPlanBuilder(broken).build(); }, InstallPlanErrorCode::InvalidReason);
        broken = result;
        broken.activated_features.clear();
        invalid([&] { InstallPlanBuilder(broken).build(); }, InstallPlanErrorCode::InvalidReason);
    }
    r.feature_states["gui"] = false;
    auto disabled = solve(r);
    assert(disabled.selections.size() == 3 && disabled.activated_features.empty());
    // Configured false overrides the owner's offered gui condition.
    assert(disabled.offered_features.size() == 2);

    auto scoped = pkg("scoped", "1",
                      {"build(host):(runtime(build):(platform(build) AND architecture(ba) AND "
                       "abi(babi) AND tool))",
                       "runtime(host):(build(target):(missing))",
                       "optional-disabled(host):(runtime(target):(missing))"});
    auto scoped_request = request({snapshot("r", {scoped, pkg("tool")})}, {target("scoped")});
    auto scope = solve(scoped_request);
    assert(scope.edges.size() == 1);
    assert(scope.edges[0].role == DependencyRole::Runtime &&
           scope.edges[0].context == DependencyTargetContext::Build);
    assert(scope.edges[0].source == selected(scope, "scoped").identity() &&
           scope.edges[0].provider == selected(scope, "tool").identity());

    auto negative = pkg("negative", "1",
                        {"NOT A", "NOT (A AND B)", "NOT (A OR B)", "NOT runtime(host):(I OR J)",
                         "build(host):(runtime(build):(when(platform(build), NOT C)))"});
    negative.conflicts = {"D AND E", "F"};
    negative.feature_dependencies["gui"] = {{{"when(true, runtime(host):(NOT (G OR H)))"},
                                             DependencyRole::Build,
                                             DependencyTargetContext::Target,
                                             true}};
    auto negative_request = request({snapshot("r", {negative})}, {target("negative")});
    negative_request.feature_states["gui"] = true;
    auto negatives = solve(negative_request);
    assert(negatives.negative_constraints.size() == 8);
    std::set<std::string> expressions;
    for (const auto& n : negatives.negative_constraints)
    {
        expressions.insert(n.expression.text);
        assert(n.declared_by == selected(negatives, "negative").identity());
        if (n.expression.text == "C")
            assert(n.role == DependencyRole::Runtime &&
                   n.context == DependencyTargetContext::Build);
        if (n.source.kind == NegativeConstraintSourceKind::FeatureDependency)
            assert(n.source.feature == "gui" && n.context == DependencyTargetContext::Host &&
                   n.role == DependencyRole::Runtime);
    }
    assert(expressions == std::set<std::string>({"A", "A AND B", "A OR B", "C", "D AND E", "F",
                                                 "G OR H", "runtime(host): (I OR J)"}));
    auto broken = negatives;
    broken.negative_constraints.front().expression.text = "invented";
    invalid([&] { InstallPlanBuilder(broken).build(); }, InstallPlanErrorCode::InvalidExpression);
    broken = negatives;
    auto nf = std::find_if(
        broken.negative_constraints.begin(), broken.negative_constraints.end(), [](const auto& n)
        { return n.source.kind == NegativeConstraintSourceKind::FeatureDependency; });
    nf->source.feature = "wrong";
    invalid([&] { InstallPlanBuilder(broken).build(); }, InstallPlanErrorCode::InvalidExpression);

    // Failed candidate and OR/AND attempts must erase every public fact.
    auto bad = pkg("provider", "2", {"old-dep", "NOT stale"});
    bad.capabilities = {"api"};
    bad.features = {"bad-offer"};
    bad.feature_dependencies["gui"] = {
        {{"old-extra"}, DependencyRole::Runtime, DependencyTargetContext::Target, true}};
    bad.conflicts = {"blocked"};
    auto good = pkg("provider");
    good.capabilities = {"api"};
    auto root = pkg("root", "1", {"capability(api) >= 1", "blocked"});
    auto rollback =
        request({snapshot("a", {bad}),
                 snapshot("b", {good, root, pkg("old-dep"), pkg("old-extra"), pkg("blocked")})},
                {target("root")});
    rollback.feature_states["gui"] = true;
    auto rolled = solve(rollback);
    assert(rolled.selections.size() == 3 && rolled.edges.size() == 2 &&
           rolled.capability_bindings.size() == 1);
    assert(selected(rolled, "provider").identity().repository == RepositoryId("b"));
    assert(rolled.capability_bindings[0].provider == selected(rolled, "provider").identity());
    assert(rolled.capability_bindings[0].selector.constraint.version == "1");
    assert(rolled.activated_features.empty() && rolled.offered_features.empty() &&
           rolled.negative_constraints.empty());
    std::reverse(rollback.snapshots.begin(), rollback.snapshots.end());
    assert(solve(rollback) == rolled);
    auto orroot = pkg("orroot", "1", {"(failed AND absent) OR good"});
    auto failed = pkg("failed", "1", {"capability(api)", "NOT stale"});
    failed.feature_dependencies["gui"] = {
        {{"old-extra"}, DependencyRole::Runtime, DependencyTargetContext::Target, true}};
    auto or_request = request(
        {snapshot("r", {orroot, failed, good, pkg("good"), pkg("old-extra")})}, {target("orroot")});
    or_request.feature_states["gui"] = true;
    auto alternative = solve(or_request);
    assert(alternative.selections.size() == 2 && alternative.edges.size() == 1);
    assert(alternative.activated_features.empty() && alternative.negative_constraints.empty() &&
           alternative.capability_bindings.empty());

    // Equal package/version alternatives preserve the accepted repository/snapshot identity.
    auto samebad = pkg("foo", "1", {"side", "NOT stale"});
    samebad.conflicts = {"blocked"};
    auto same = request(
        {snapshot("a", {samebad}), snapshot("b", {pkg("foo"), pkg("side"), pkg("blocked")})},
        {target("foo"), target("blocked")});
    same.system_policies = {{SystemPolicyId("base"), {target("foo")}}};
    const auto accepted = solve(same);
    assert(selected(accepted, "foo").identity().repository == RepositoryId("b"));
    assert(accepted.selections.size() == 2 && accepted.edges.empty() &&
           accepted.negative_constraints.empty());
    assert(accepted.system_policy_selections.size() == 1 &&
           accepted.system_policy_selections[0].candidate == selected(accepted, "foo").identity());
    std::reverse(same.snapshots.begin(), same.snapshots.end());
    assert(solve(same) == accepted);
    // Policy association from a failed prefix must roll back too.
    auto policy =
        request({snapshot("r", {pkg("root"), pkg("policy", "2", {"old-dep", "NOT stale"}),
                                pkg("policy"), pkg("old-dep"), pkg("last", "1", {"policy = 1"})})},
                {target("root")});
    policy.system_policies = {{SystemPolicyId("base"), {target("policy"), target("last")}}};
    auto policies = solve(policy);
    assert(policies.system_policy_selections.size() == 2 && policies.negative_constraints.empty() &&
           policies.edges.size() == 1);
    for (const auto& p : policies.system_policy_selections)
        if (p.selector.name == "policy")
            assert(p.candidate.version == PackageVersion("1"));
    // Feature provider selector keeps its own provider identity; no capability binding is invented.
    auto provider = pkg("feature-provider");
    provider.features = {"theme"};
    auto feature_request =
        request({snapshot("r", {pkg("feature-root", "1", {"feature(theme)"}), provider})},
                {target("feature-root")});
    auto feature = solve(feature_request);
    assert(feature.capability_bindings.empty() &&
           feature.edges[0].target.kind == SelectorKind::Feature);
    assert(feature.edges[0].provider == selected(feature, "feature-provider").identity());
    // Root semantic selectors and policies reuse the selected candidate.
    auto api = pkg("api-provider");
    api.capabilities = {"api"};
    auto semantic = request({snapshot("r", {api})},
                            {target("api-provider"), target("api", SelectorKind::Capability)});
    semantic.system_policies = {
        {SystemPolicyId("api-policy"), {target("api", SelectorKind::Capability)}}};
    auto semantic_result = solve(semantic);
    assert(semantic_result.selections.size() == 1 &&
           semantic_result.capability_bindings.size() == 1);
    assert(semantic_result.selections[0].reasons.size() == 3 &&
           semantic_result.system_policy_selections[0].selector == semantic.targets[1].selector);
    // Feature provenance belongs to the declaring owner; ordinary dependencies
    // of a feature-enabled selection retain DependencyOf.
    auto feature_owner = pkg("owner");
    feature_owner.feature_dependencies["gui"] = {
        {{"child"}, DependencyRole::Runtime, DependencyTargetContext::Target, true}};
    auto feature_chain = request(
        {snapshot("r", {feature_owner, pkg("child", "1", {"grandchild"}), pkg("grandchild")})},
        {target("owner")});
    feature_chain.feature_states["gui"] = true;
    auto chain = solve(feature_chain);
    assert(reason(selected(chain, "child"), InstallReasonKind::EnabledByFeature).package ==
           selected(chain, "owner").identity());
    const auto& grandchild = selected(chain, "grandchild");
    assert(grandchild.reasons.size() == 1 &&
           grandchild.reasons[0].kind == InstallReasonKind::DependencyOf);
    assert(grandchild.reasons[0].package == selected(chain, "child").identity());
    // Deferred composed negatives also roll back selections, reasons and records.
    auto deferred_root = pkg("deferred", "2", {"side", "NOT (A AND B)"});
    auto deferred_request =
        request({snapshot("r", {deferred_root, pkg("deferred"), pkg("side"), pkg("A"), pkg("B")})},
                {target("deferred"), target("A"), target("B")});
    auto deferred = solve(deferred_request);
    assert(deferred.selections.size() == 3 && deferred.edges.empty() &&
           deferred.negative_constraints.empty());
    assert(selected(deferred, "deferred").candidate.package.version == PackageVersion("1"));
    // Feature-provider backtracking retains only the final owner's offered feature.
    auto feature_bad = pkg("theme-provider", "2", {"old"});
    feature_bad.features = {"theme", "stale-offer"};
    feature_bad.conflicts = {"blocked"};
    auto feature_good = pkg("theme-provider");
    feature_good.features = {"theme"};
    auto feature_retry =
        request({snapshot("r", {pkg("theme-root", "1", {"feature(theme)", "blocked"}), feature_bad,
                                feature_good, pkg("old"), pkg("blocked")})},
                {target("theme-root")});
    auto feature_final = solve(feature_retry);
    assert(feature_final.selections.size() == 3 && feature_final.offered_features.size() == 1);
    assert(feature_final.edges.size() == 2 && feature_final.negative_constraints.empty());
    assert(selected(feature_final, "theme-provider").candidate.package.version ==
           PackageVersion("1"));
    // Recursive version constraints participate in binding/policy ordering.
    auto constrained = semantic;
    auto constraint = VersionConstraint{
        VersionConstraintKind::And,
        VersionComparisonOperator::Equal,
        "",
        {{VersionConstraintKind::Comparison, VersionComparisonOperator::GreaterEqual, "1", {}},
         {VersionConstraintKind::Not,
          VersionComparisonOperator::Equal,
          "",
          {{VersionConstraintKind::Comparison, VersionComparisonOperator::Equal, "2", {}}}}}};
    constraint.comparison = VersionComparisonOperator::Greater; // Retain the exact public value.
    constrained.targets[1].selector.constraint = constraint;
    constrained.system_policies[0].required.push_back(constrained.targets[1]);
    auto constrained_result = solve(constrained);
    assert(constrained_result.capability_bindings.size() == 2 &&
           constrained_result.system_policy_selections.size() == 2);
    assert(constrained_result.selections[0].reasons.size() == 3);
    assert(std::any_of(constrained_result.capability_bindings.begin(), constrained_result.capability_bindings.end(),
                       [&](const auto& b) { return b.selector == constrained.targets[1].selector; }));
    std::reverse(constrained.system_policies[0].required.begin(),
                 constrained.system_policies[0].required.end());
    assert(solve(constrained) == constrained_result);
    // A global retry resumes an assigned owner, then an OR attempt restores
    // the assignment map. Its declarations and recorded source must stay valid.
    auto replay_failed = pkg("failed", "1", {"NOT stale"});
    replay_failed.feature_dependencies["gui"] = {{{"old"}, DependencyRole::Runtime, DependencyTargetContext::Target, true}};
    auto replay_request = request({snapshot("r", {pkg("replay", "1", {"choice", "(failed AND absent) OR good"}),
        pkg("choice", "2"), pkg("choice"), replay_failed, pkg("old"), pkg("good"), pkg("last", "1", {"choice = 1"})})},
        {target("replay"), target("last")});
    replay_request.feature_states["gui"] = true;
    auto replay = solve(replay_request);
    assert(replay.selections.size() == 4 && replay.edges.size() == 3);
    assert(replay.activated_features.empty() && replay.negative_constraints.empty());
    assert(selected(replay, "choice").candidate.package.version == PackageVersion("1"));
    assert(selected(replay, "choice").reasons.size() == 2);
    // Genuine artifact/build-input payloads survive exactly; absent ones stay absent.
    auto payload = snapshot("payload", {pkg("binary")}).candidates().front();
    payload.artifact = ArtifactMetadata{SchemaVersion(1),
                                        ArtifactId("artifact:real"),
                                        ContentDigest("sha256:" + std::string(64, 'a')),
                                        ManifestDigest("sha256:" + std::string(64, 'b')),
                                        Provenance{ArtifactOrigin::BuiltFromAthenaIR},
                                        BuildInputId("input:real")};
    const auto payload_snapshot =
        RepositorySnapshot::create(RepositoryId("payload"), RepositoryRevision("rev"), {payload});
    auto payload_result = solve(request({payload_snapshot}, {target("binary")}));
    assert(payload_result.selections[0].candidate == payload_snapshot.candidates()[0]);
    assert(payload_result.selections[0].candidate.artifact->artifact_id ==
           ArtifactId("artifact:real"));
    assert(payload_result.selections[0].candidate.artifact->build_input_id ==
           BuildInputId("input:real"));
    // Same package/version/repository, distinct revisions and snapshots.
    auto revisions = request(
        {snapshot("r", {pkg("foo")}, "one"), snapshot("r", {pkg("foo")}, "two")}, {target("foo")});
    auto revision_result = solve(revisions);
    std::reverse(revisions.snapshots.begin(), revisions.snapshots.end());
    assert(solve(revisions) == revision_result);
    const auto resolution_error = [](const InstallRequest& input, ResolutionErrorCode code)
    {
        bool caught = false;
        try
        {
            (void)resolve_request(input);
        }
        catch (const ResolutionError& error)
        {
            caught = error.code() == code;
        }
        assert(caught);
    };
    resolution_error(request({snapshot("r", {pkg("foo")})}, {target("missing")}),
                     ResolutionErrorCode::UnknownPackageRoot);
    resolution_error(
        request({snapshot("r", {pkg("foo")})}, {target("missing", SelectorKind::Capability)}),
        ResolutionErrorCode::NoCapabilityProvider);
    resolution_error(
        request({snapshot("r", {pkg("foo")})}, {target("missing", SelectorKind::Feature)}),
        ResolutionErrorCode::NoFeatureProvider);
    auto conflicting = pkg("foo");
    conflicting.conflicts = {"blocked"};
    resolution_error(
        request({snapshot("r", {conflicting, pkg("blocked")})}, {target("foo"), target("blocked")}),
        ResolutionErrorCode::Conflict);
    auto failing_provider = pkg("provider");
    failing_provider.capabilities = {"api"};
    failing_provider.conflicts = {"blocked"};
    resolution_error(request({snapshot("r", {failing_provider, pkg("blocked")})},
                             {target("api", SelectorKind::Capability), target("blocked")}),
                     ResolutionErrorCode::ProviderConflict);
    auto malformed = request({snapshot("r", {pkg("foo")})}, {target("foo")});
    const_cast<RepositoryCandidate&>(malformed.snapshots[0].candidates()[0])
        .package.dependencies = {"NOT ("};
    resolution_error(malformed, ResolutionErrorCode::InvalidDependencyMetadata);
    auto mismatched = request({snapshot("r", {pkg("foo")})}, {target("foo")});
    const_cast<RepositoryCandidate&>(mismatched.snapshots[0].candidates()[0])
        .provenance.repository_id = RepositoryId("other");
    resolution_error(mismatched, ResolutionErrorCode::CandidateSnapshotMismatch);
    assert(entries() == before);
}
