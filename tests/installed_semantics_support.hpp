#pragma once
#include "installed/environment.hpp"
#include "install/generation_builder.hpp"
#include "install/transaction.hpp"
#include "install/remover.hpp"
#include "store/metadata.hpp"
#include "store/gc.hpp"
#include "object_store_test_support.hpp"
#include <athena/core/resolver.hpp>

namespace semantic_test {
using namespace athena;
using namespace athena::core;
inline PackageMetadata pkg(std::string name, std::string version = "1", std::vector<std::string> deps = {}) {
    PackageMetadata p{PackageId(std::move(name)), PackageVersion(std::move(version))};
    p.dependencies = std::move(deps); return p;
}
inline InstallPlan plan(std::vector<PackageMetadata> packages,
                        std::map<std::string, bool> features = {},
                        std::vector<SystemPolicyRequest> policies = {},
                        std::vector<DependencyRole> roles = {},
                        std::map<std::string, fs::path> sources = {}) {
    RepositoryId repo("semantic-test");
    std::vector<RepositoryCandidate> candidates;
    InstallRequest request;
    for (const auto& p : packages) {
        RepositoryCandidate c{p, RepositoryProvenance{repo}};
        if (auto it = sources.find(p.package_id.value()); it != sources.end()) {
            SourceRecipe recipe{SchemaVersion{}, p.package_id, p.version};
            recipe.sources.push_back({MetadataReference("file://" + it->second.native())});
            recipe.build_backend = "cmake"; c.source_recipe = recipe;
        }
        candidates.push_back(c);
        request.targets.push_back({{SelectorKind::Package, p.package_id.value(), {}}});
    }
    request.snapshots.push_back(RepositorySnapshot::create(repo, RepositoryRevision("fixture"), candidates));
    request.repositories[repo] = {};
    request.target = {"linux", "x86_64", "glibc", "target-triple"};
    request.environments = {{"linux", "x86_64", "glibc", "host-triple"}, {"linux", "x86_64", "glibc", "build-triple"}};
    request.feature_states = std::move(features); request.system_policies = std::move(policies);
    request.active_roles = std::move(roles);
    request.optional_enabled = true;
    return plan_install(request);
}
inline generation::Generation state(const InstallPlan& p, std::uint64_t id = 1) {
    generation::Generation g{id, {}};
    for (const auto& s : p.selections()) g.realizations.push_back({s.identity(),
        ArtifactId("sha256:" + std::string(64, 'a')), {}, ArtifactOrigin::AmbientSourceBuild,
        installed::from_selection(p, s)});
    g.policies = installed::policies_from_plan(p); return g;
}
inline generation::Generation remove(const generation::Generation& g, std::vector<std::string> names) {
    install::RemovePlan p;
    for (auto& n : names) p.add(std::move(n));
    auto result = install::build_removal_generation(g, p);
    installed::capture_legacy_semantics(result);
    installed::validate_environment(result); return result;
}
template<class F> inline void rejects(F action, const std::string& diagnostic = {}) {
    try { action(); } catch (const std::exception& error) {
        require(diagnostic.empty() || std::string(error.what()).find(diagnostic) != std::string::npos, error.what()); return;
    }
    throw std::runtime_error("Expected installed validation rejection: " + diagnostic);
}
inline auto& member(generation::Generation& g, const std::string& name) {
    auto it = std::find_if(g.realizations.begin(), g.realizations.end(), [&](const auto& r) { return r.candidate.package.value() == name; });
    require(it != g.realizations.end(), "missing semantic fixture member"); return *it;
}
}
