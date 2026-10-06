// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/dependency/resolver.hpp"
#include "../src/repository/resolver_snapshot_adapter.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <set>
using namespace athena;
using namespace athena::core;
namespace {
PackageMetadata metadata(std::string name, std::string version = "1", std::vector<std::string> deps = {}) {
    PackageMetadata p{PackageId(name), PackageVersion(version), SchemaVersion(1)};
    p.dependencies = std::move(deps);
    return p;
}
RepositorySnapshot snapshot(std::string repo, std::vector<PackageMetadata> packages) {
    std::vector<RepositoryCandidate> candidates;
    for (auto& p : packages) candidates.push_back({p, {RepositoryId(repo), RepositoryRevision("rev"), MetadataReference(p.package_id.value())}});
    return RepositorySnapshot::create(RepositoryId(repo), RepositoryRevision("rev"), candidates);
}
InstallTarget root(std::string name, SelectorKind kind = SelectorKind::Package, std::string version = "") {
    VersionConstraint c;
    if (!version.empty()) { c.kind = VersionConstraintKind::Comparison; c.version = version; }
    return {{kind, name, c}};
}
InstallRequest request(std::vector<RepositorySnapshot> snapshots, std::vector<InstallTarget> roots) {
    InstallRequest r;
    r.snapshots = std::move(snapshots); r.targets = std::move(roots);
    for (const auto& s : r.snapshots) r.repositories[s.repository_id()] = {};
    r.target = {"target", "ta", "tabi", "triple"};
    r.environments = {{"host", "ha", "habi", ""}, {"build", "ba", "babi", ""}};
    return r;
}
const repository::ResolverCandidate& selected(const dependency::RequestResolution& r, std::string name) {
    auto it = std::find_if(r.candidates.begin(), r.candidates.end(), [&](const auto& c) { return c.name == name; });
    assert(it != r.candidates.end()); return *it;
}
void error(const InstallRequest& r, dependency::ResolutionErrorCode code) {
    bool caught = false;
    try { (void)dependency::resolve_request(r); }
    catch (const dependency::ResolutionError& e) { caught = e.code() == code; }
    assert(caught);
}
std::set<std::filesystem::path> entries() {
    std::set<std::filesystem::path> r;
    for (const auto& e : std::filesystem::directory_iterator(".")) r.insert(e.path());
    return r;
}
}
int main() {
    const auto before = entries();
    auto shared = request({snapshot("r", {metadata("a", "1", {"shared >= 1"}), metadata("b", "1", {"shared < 2"}), metadata("shared", "1"), metadata("shared", "2")})}, {root("a"), root("b")});
    auto solution = dependency::resolve_request(shared);
    assert(solution.candidates.size() == 3 && solution.origins.size() == 2);
    assert(selected(solution, "shared").version == "1");
    shared.targets.push_back(root("shared", SelectorKind::Package, "2"));
    error(shared, dependency::ResolutionErrorCode::IncompatibleRoots);
    auto versions = request({snapshot("r", {metadata("foo"), metadata("foo", "2")})}, {root("foo", SelectorKind::Package, "1")});
    assert(selected(dependency::resolve_request(versions), "foo").version == "1");
    versions.targets = {root("foo", SelectorKind::Package, "3")};
    error(versions, dependency::ResolutionErrorCode::IncompatibleRootVersion);
    versions.targets = {root("missing")}; error(versions, dependency::ResolutionErrorCode::UnknownPackageRoot);
    versions.targets = {root("missing", SelectorKind::Capability)}; error(versions, dependency::ResolutionErrorCode::NoCapabilityProvider);
    versions.targets = {root("missing", SelectorKind::Feature)}; error(versions, dependency::ResolutionErrorCode::NoFeatureProvider);
    versions.targets = {root("")}; error(versions, dependency::ResolutionErrorCode::InvalidSelector);
    versions.targets = {}; error(versions, dependency::ResolutionErrorCode::InvalidRequest);
    auto provider = metadata("provider", "2"); provider.capabilities = {"api"}; provider.features = {"gui"};
    auto semantic = request({snapshot("r", {provider})}, {root("provider"), root("api", SelectorKind::Capability, "2"), root("gui", SelectorKind::Feature, "2")});
    auto sem = dependency::resolve_request(semantic);
    assert(sem.candidates.size() == 1 && sem.origins.size() == 3);
    for (const auto& o : sem.origins) assert(o.candidate.identity == sem.candidates.front().identity);
    auto pinned = metadata("provider", "1");
    auto offered = metadata("provider", "2"); offered.capabilities = {"api"};
    auto other = metadata("other"); other.capabilities = {"api"};
    auto reuse = request({snapshot("r", {pinned, offered, other})}, {root("provider", SelectorKind::Package, "1"), root("api", SelectorKind::Capability)});
    auto reused = dependency::resolve_request(reuse);
    assert(selected(reused, "provider").version == "1" && reused.origins[1].candidate.name == "other");
    semantic.system_policies = {{SystemPolicyId("api-policy"), {root("api", SelectorKind::Capability, "2")}}};
    auto semantic_policy = dependency::resolve_request(semantic);
    assert(semantic_policy.candidates.size() == 1 && semantic_policy.origins.back().policy == SystemPolicyId("api-policy"));
    assert(semantic_policy.origins.back().candidate.identity == semantic_policy.origins.front().candidate.identity);
    versions.targets = {root("foo")};
    versions.targets[0].selector.constraint.kind = VersionConstraintKind::And;
    versions.targets[0].selector.constraint.operands = {
        {VersionConstraintKind::Comparison, VersionComparisonOperator::GreaterEqual, "1", {}},
        {VersionConstraintKind::Not, VersionComparisonOperator::Equal, "", {
            {VersionConstraintKind::Comparison, VersionComparisonOperator::Equal, "2", {}}}}};
    assert(selected(dependency::resolve_request(versions), "foo").version == "1");
    assert(dependency::resolve_request(versions).origins[0].target == versions.targets[0]);
    auto named = metadata("api");
    auto collision = request({snapshot("r", {named, other})}, {root("api"), root("api", SelectorKind::Capability)});
    auto colliding = dependency::resolve_request(collision);
    assert(colliding.candidates.size() == 2 && colliding.origins[1].candidate.name == "other");
    auto alternatives = request({snapshot("a", {metadata("foo", "1", {"bad"})}), snapshot("b", {metadata("foo")})}, {root("foo")});
    alternatives.repositories[RepositoryId("a")].priority = -1;
    auto alt = dependency::resolve_request(alternatives);
    assert(selected(alt, "foo").identity->repository == RepositoryId("b"));
    assert(alt.origins.size() == 1 && alt.origins[0].candidate.identity == alt.candidates[0].identity);
    std::reverse(alternatives.snapshots.begin(), alternatives.snapshots.end());
    assert(dependency::resolve_request(alternatives).candidates[0].identity == alt.candidates[0].identity);
    auto ranking = request({snapshot("a", {metadata("foo")}), snapshot("b", {metadata("foo")})}, {root("foo")});
    ranking.repositories[RepositoryId("b")].priority = -1;
    assert(selected(dependency::resolve_request(ranking), "foo").identity->repository == RepositoryId("b"));
    ranking.repositories[RepositoryId("b")].version_scheme = VersionScheme::Unsupported;
    error(ranking, dependency::ResolutionErrorCode::InvalidRepositoryRule);
    auto owner = metadata("owner", "1", {"when(feature(gui), conditional)"});
    owner.features = {"gui"};
    owner.feature_dependencies["gui"] = {{{"extra"}, DependencyRole::Runtime, DependencyTargetContext::Target, true}};
    auto owner2 = metadata("owner2");
    owner2.feature_dependencies["gui"] = {{{"when(true, runtime(build):(platform(build) AND extra2))"}, DependencyRole::Build, DependencyTargetContext::Host, true}};
    auto features = request({snapshot("r", {owner, owner2, metadata("extra"), metadata("extra2"), metadata("conditional")})}, {root("owner"), root("owner2")});
    features.profile = "development";
    features.feature_states["gui"] = true;
    auto feat = dependency::resolve_request(features);
    assert(feat.candidates.size() == 5 && feat.profile == "development");
    features.feature_states["gui"] = false;
    assert(dependency::resolve_request(features).candidates.size() == 2);
    auto scoped = metadata("scoped", "1", {
        "build(host):(runtime(build):(platform(build) AND architecture(ba) AND abi(babi) AND build-dep) AND platform(host) AND architecture(ha) AND abi(habi))",
        "runtime(target):(platform(target) AND architecture(ta) AND abi(tabi) AND runtime-dep)",
        "optional(target):optional-dep", "propagated(host):propagated-dep",
        "optional-disabled(host):(runtime(target):(missing))"});
    auto roles = request({snapshot("r", {scoped, metadata("build-dep"), metadata("runtime-dep"), metadata("optional-dep"), metadata("propagated-dep")})}, {root("scoped")});
    assert(dependency::resolve_request(roles).candidates.size() == 3);
    roles.active_roles = {DependencyRole::Runtime, DependencyRole::Build, DependencyRole::Optional, DependencyRole::Propagated};
    roles.optional_enabled = true;
    assert(dependency::resolve_request(roles).candidates.size() == 5);
    roles.propagated_enabled = false;
    assert(dependency::resolve_request(roles).candidates.size() == 4);
    roles.optional_enabled = false;
    assert(dependency::resolve_request(roles).candidates.size() == 3);
    roles.active_roles = {DependencyRole::Runtime};
    roles.optional_enabled = true;
    assert(dependency::resolve_request(roles).candidates.size() == 3);
    auto policies = request({snapshot("r", {metadata("a", "2"), metadata("a"), metadata("b", "1", {"a = 1"})})}, {root("a")});
    policies.system_policies = {{SystemPolicyId("base"), {root("b"), root("a")}}};
    auto pol = dependency::resolve_request(policies);
    assert(selected(pol, "a").version == "1" && pol.origins.size() == 3);
    assert(!pol.origins[0].policy && pol.origins[1].policy == SystemPolicyId("base"));
    assert(pol.origins[0].candidate.identity == pol.origins[2].candidate.identity);
    assert(pol.origins[0].candidate.version == "1");
    policies.system_policies[0].required.push_back(root("absent"));
    error(policies, dependency::ResolutionErrorCode::UnsatisfiedSystemPolicy);
    // Policy candidate itself backtracks; no stale origin from its failed branch.
    auto rollback = request({snapshot("r", {metadata("root"), metadata("policy", "2"), metadata("policy"), metadata("last", "1", {"policy = 1"})})}, {root("root")});
    rollback.system_policies = {{SystemPolicyId("base"), {root("policy"), root("last")}}};
    auto rolled = dependency::resolve_request(rollback);
    assert(rolled.origins.size() == 3 && rolled.origins[1].candidate.version == "1");
    // Eligibility uses the same target policy as public repository selection.
    auto rejected = metadata("foo"); rejected.platforms = {"other"};
    auto eligibility = request({snapshot("a", {rejected}), snapshot("b", {metadata("foo")})}, {root("foo")});
    assert(selected(dependency::resolve_request(eligibility), "foo").identity->repository == RepositoryId("b"));
    eligibility.repositories.erase(RepositoryId("a"));
    error(eligibility, dependency::ResolutionErrorCode::InvalidRepositoryRule);
    auto invalid = request({snapshot("r", {metadata("foo")})}, {root("foo")});
    const_cast<RepositoryCandidate&>(invalid.snapshots[0].candidates()[0]).package.description = "tampered";
    bool invalid_snapshot = false;
    try { (void)dependency::resolve_request(invalid); }
    catch (const repository::ResolverSnapshotAdapterError&) { invalid_snapshot = true; }
    assert(invalid_snapshot);
    assert(entries() == before);
}
