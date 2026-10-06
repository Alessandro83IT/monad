// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/repository/resolver_snapshot_adapter.hpp"
#include "../src/dependency/parser.hpp"
#include "../src/dependency/serializer.hpp"

#include <cassert>
#include <filesystem>
#include <set>

using namespace athena;
using namespace athena::core;

namespace {

RepositorySnapshot make_snapshot(
    const std::string& repository,
    const std::string& package,
    const std::string& version,
    std::vector<std::string> dependencies = {})
{
    const RepositoryId repository_id(repository);
    const RepositoryRevision revision("revision-" + version + "-" + repository);
    PackageMetadata metadata{PackageId(package), PackageVersion(version), SchemaVersion(1)};
    metadata.display_name = "Display " + package;
    metadata.description = "Description " + package;
    metadata.dependencies = std::move(dependencies);
    metadata.feature_dependencies["gui"] = {{
        DependencyExpression{"when(platform(linux), runtime(build):(capability(graphics) >= 2 AND (feature(theme) OR NOT helper)))"},
        DependencyRole::Development,
        DependencyTargetContext::Build,
        true
    }};
    metadata.capabilities = {"api", "graphics"};
    metadata.features = {"gui", "theme"};
    metadata.conflicts = {"blocked OR NOT obsolete"};
    metadata.platforms = {"linux"};
    metadata.architectures = {"x86_64"};
    metadata.abis = {"glibc"};
    RepositoryProvenance provenance{
        repository_id, revision, MetadataReference("catalog/" + package)};
    return RepositorySnapshot::create(repository_id, revision,
        {{std::move(metadata), std::move(provenance)}});
}

} // namespace

int main()
{
    const auto entries = [] {
        std::set<std::string> result;
        for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::current_path()))
            result.insert(entry.path().filename().string());
        return result;
    };
    const auto before = entries();

    const auto repo_a_v1 = make_snapshot("repo-a", "foo", "1.0",
        {"when(platform(linux), runtime(host):(capability(api) >= 1 AND (feature(gui) OR NOT blocked)))"});
    const auto repo_b_v1 = make_snapshot("repo-b", "foo", "1.0");
    const auto repo_a_v2 = make_snapshot("repo-a", "foo", "2.0");

    const auto index = repository::adapt_snapshots({repo_b_v1, repo_a_v2, repo_a_v1});
    const auto candidates = index.find_candidates("foo");
    assert(candidates.size() == 3);
    assert(candidates[0].identity->repository == RepositoryId("repo-a"));
    assert(candidates[0].identity->version == PackageVersion("1.0"));
    assert(candidates[1].identity->repository == RepositoryId("repo-b"));
    assert(candidates[1].identity->version == PackageVersion("1.0"));
    assert(candidates[2].identity->version == PackageVersion("2.0"));

    const auto find_candidate = [&](const RepositoryId& id) -> const repository::ResolverCandidate& {
        for (const auto& candidate : candidates)
            if (candidate.identity->repository == id && candidate.identity->version == PackageVersion("1.0"))
                return candidate;
        assert(false);
        return candidates.front();
    };
    const auto& adapted = find_candidate(RepositoryId("repo-a"));
    assert(adapted.snapshot == repo_a_v1.reference());
    assert(adapted.public_candidate == repo_a_v1.candidates().front());
    assert(adapted.identity->snapshot == repo_a_v1.reference());
    assert(adapted.identity->provenance == repo_a_v1.candidates().front().provenance);
    assert(adapted.name == "foo" && adapted.version == "1.0");
    assert(adapted.description == "Description foo");
    assert(adapted.dependencies.size() == 1);
    assert(adapted.dependencies.front().expression->kind == dependency::Expression::Kind::Conditional);
    assert(dependency::serialize_dependency(adapted.dependencies.front()).find("capability(api) >= 1") != std::string::npos);
    assert(dependency::serialize_dependency(adapted.dependencies.front()).find("NOT blocked") != std::string::npos);
    assert(adapted.feature_dependencies.at("gui").size() == 1);
    const auto& feature_dependency = adapted.feature_dependencies.at("gui").front();
    assert(feature_dependency.kind == dependency::DependencyKind::Development);
    assert(feature_dependency.context == dependency::DependencyContext::Build);
    assert(feature_dependency.enabled);
    assert(feature_dependency.expression->kind == dependency::Expression::Kind::Conditional);
    const auto feature_expression = dependency::serialize_expression(feature_dependency.expression);
    assert(feature_expression.find("runtime(build)") != std::string::npos);
    assert(feature_expression.find("feature(theme)") != std::string::npos);
    assert(feature_expression.find("NOT helper") != std::string::npos);
    assert(adapted.provides.capabilities == std::vector<std::string>({"api", "graphics"}));
    assert(adapted.provides.features == std::vector<std::string>({"gui", "theme"}));
    assert(adapted.conflicts.size() == 1);
    assert(adapted.platforms == std::vector<std::string>{"linux"});
    assert(adapted.architectures == std::vector<std::string>{"x86_64"});
    assert(adapted.abis == std::vector<std::string>{"glibc"});

    const auto reversed = repository::adapt_snapshots({repo_a_v1, repo_a_v2, repo_b_v1})
                              .find_candidates("foo");
    assert(reversed.size() == candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i)
        assert(reversed[i].identity == candidates[i].identity);

    auto wrong_revision = repo_a_v1.reference();
    wrong_revision.revision = RepositoryRevision("unrelated-revision");
    bool revision_mismatch_rejected = false;
    try {
        (void)repository::ResolverCandidate::from_public(
            repo_a_v1.candidates().front(), wrong_revision);
    } catch (const repository::ResolverCandidateError& error) {
        revision_mismatch_rejected =
            error.code() == repository::ResolverCandidateErrorCode::SnapshotMismatch;
    }
    assert(revision_mismatch_rejected);

    const auto environments = repository::adapt_environments(
        TargetEnvironment{"target-platform", "target-arch", "target-abi", "target-triple"},
        ResolutionEnvironments{
            TargetEnvironment{"host-platform", "host-arch", "host-abi", "host-triple"},
            TargetEnvironment{"build-platform", "build-arch", "build-abi", "build-triple"}});
    assert(environments.host.platform == "host-platform");
    assert(environments.host.architecture == "host-arch");
    assert(environments.host.abi == "host-abi");
    assert(environments.build.platform == "build-platform");
    assert(environments.build.architecture == "build-arch");
    assert(environments.build.abi == "build-abi");
    assert(environments.target.platform == "target-platform");
    assert(environments.target.architecture == "target-arch");
    assert(environments.target.abi == "target-abi");
    assert(entries() == before);
}
