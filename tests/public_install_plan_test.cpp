#include <athena/core/api.hpp>

#include <algorithm>
#include <cassert>

using namespace athena::core;

static RepositorySnapshot snapshot(std::string repo) {
    PackageMetadata metadata{PackageId("app"), PackageVersion("1.0"), SchemaVersion(1)};
    metadata.features = {"offered"};
    metadata.architectures = {"x86_64"};
    metadata.conflicts = {"NOT pkg-a", "NOT (pkg-a AND pkg-b)", "NOT (pkg-a OR pkg-b)"};
    metadata.feature_dependencies["gui"] = {{DependencyExpression{"libgui"}, DependencyRole::Runtime, DependencyTargetContext::Target, true}};
    const RepositoryId id(repo);
    return RepositorySnapshot::create(id, RepositoryRevision("rev1"), {{metadata, RepositoryProvenance{id, RepositoryRevision("rev1")}}});
}

static PackageSelection selection(const RepositorySnapshot& snap) {
    auto candidate = snap.candidates_with_snapshot_reference().front();
    InstallReason reason;
    reason.kind = InstallReasonKind::ExplicitRequest;
    reason.explicit_target = InstallTarget{DependencySelector{SelectorKind::Package, "app", {}}};
    return {candidate, snap.reference(), {reason}};
}

int main() {
    auto a = snapshot("repo-a"), b = snapshot("repo-b");
    assert(selection(a).identity() != selection(b).identity());
    const auto restored = deserialize_repository_snapshot(serialize(a));
    assert(restored.candidates().front().package.feature_dependencies.at("gui").front().expression.text == "libgui");

    InstallRequest request;
    request.targets.push_back({DependencySelector{SelectorKind::Package, "app", {}}});
    request.target = {"linux", "x86_64", "glibc", "x86_64-linux-gnu"};
    request.profile = "desktop";
    request.snapshots = {a};
    request.repositories.emplace(RepositoryId("repo-a"), RepositorySelectionRule{});
    request.feature_states["gui"] = true;
    request.system_policies.push_back({SystemPolicyId("base"), {{DependencySelector{SelectorKind::Capability, "tls", {}}}}});

    ResolutionResult result;
    result.target = request.target;
    result.profile = request.profile;
    result.selections = {selection(a)};
    const auto id = result.selections.front().identity();
    InstallReason dependency_reason; dependency_reason.kind = InstallReasonKind::DependencyOf; dependency_reason.package = id;
    InstallReason feature_reason; feature_reason.kind = InstallReasonKind::EnabledByFeature; feature_reason.profile = "desktop"; feature_reason.feature = "gui";
    InstallReason policy_reason; policy_reason.kind = InstallReasonKind::RequiredBySystemPolicy; policy_reason.policy = SystemPolicyId("base");
    result.selections.front().reasons.insert(result.selections.front().reasons.end(), {dependency_reason, feature_reason, policy_reason});
    result.capability_bindings.push_back({DependencySelector{SelectorKind::Capability, "tls", {VersionConstraintKind::Comparison, VersionComparisonOperator::GreaterEqual, "1.2", {}}}, id});
    result.configured_features.push_back({"gui", "desktop", true});
    result.offered_features.push_back({"offered", id});
    result.activated_features.push_back({"gui", "desktop", id});
    result.edges.push_back({id, DependencySelector{SelectorKind::Package, "libgui", {}}, id, DependencyRole::Runtime, DependencyTargetContext::Target, feature_reason});
    result.negative_constraints = {{{"NOT pkg-a"}, id}, {{"NOT (pkg-a AND pkg-b)"}, id}, {{"NOT (pkg-a OR pkg-b)"}, id}};

    // Sort the input to the builder so equality verifies complete retention.
    std::sort(result.selections.front().reasons.begin(), result.selections.front().reasons.end(), [](const auto& x, const auto& y) { return x.kind < y.kind; });
    std::sort(result.negative_constraints.begin(), result.negative_constraints.end(), [](const auto& x, const auto& y) { return x.expression.text < y.expression.text; });
    const auto plan = InstallPlanBuilder(result).build();
    assert(plan.result() == result);
    assert(plan.selections().front().candidate.provenance.repository_id == RepositoryId("repo-a"));
    assert(plan.capability_bindings().front().provider == id);
    assert(plan.configured_features().front().enabled && plan.offered_features().size() == 1 && plan.activated_features().size() == 1);
    assert(plan.edges().front().reason->kind == InstallReasonKind::EnabledByFeature);
    assert(plan.negative_constraints().size() == 3);

    auto invalid = result;
    invalid.capability_bindings.front().provider.repository = RepositoryId("other");
    bool rejected = false;
    try { (void)InstallPlanBuilder(std::move(invalid)).build(); }
    catch (const InstallPlanError& e) { rejected = e.code() == InstallPlanErrorCode::InvalidBinding; }
    assert(rejected);

    invalid = result;
    invalid.target.architecture = "aarch64";
    rejected = false;
    try { (void)InstallPlanBuilder(std::move(invalid)).build(); }
    catch (const InstallPlanError& e) { rejected = e.code() == InstallPlanErrorCode::TargetMismatch; }
    assert(rejected);
}
