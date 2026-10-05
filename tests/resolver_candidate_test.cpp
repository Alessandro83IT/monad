#include "../src/dependency/resolver.hpp"
#include "../src/dependency/parser.hpp"
#include "../src/repository/repository_index.hpp"

#include <athena/core/repository.hpp>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <set>

using namespace athena;
using namespace athena::core;
using Package = athena::package::Package;

namespace {

core::RepositorySnapshot make_snapshot(
    const std::string& repository_name,
    const std::string& package_name,
    const std::string& version,
    std::vector<std::string> conflicts = {},
    std::vector<std::string> dependencies = {})
{
    const RepositoryId repository_id(repository_name);
    const RepositoryRevision revision("rev-" + version + "-" + repository_name);
    PackageMetadata metadata{PackageId(package_name), PackageVersion(version), SchemaVersion(1)};
    metadata.conflicts = std::move(conflicts);
    metadata.dependencies = std::move(dependencies);
    metadata.capabilities = {"api"};
    metadata.features = {"gui"};
    metadata.platforms = {"linux"};
    metadata.architectures = {"x86_64"};
    metadata.abis = {"glibc"};
    return RepositorySnapshot::create(repository_id, revision,
        {{metadata, RepositoryProvenance{repository_id, revision,
                                         MetadataReference("metadata/" + package_name)}}});
}

} // namespace

int main()
{
    const auto directory_entries = [] {
        std::set<std::string> entries;
        for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::current_path()))
            entries.insert(entry.path().filename().string());
        return entries;
    };
    const auto directory_before = directory_entries();

    const auto repo_a = make_snapshot("repo-a", "foo", "1.0", {"blocked"});
    const auto repo_b = make_snapshot("repo-b", "foo", "1.0");
    const auto foo_v2 = make_snapshot("repo-a", "foo", "2.0");

    repository::RepositoryIndex index;
    index.add_public_candidate(repo_b.candidates().front(), repo_b.reference());
    index.add_public_candidate(foo_v2.candidates().front(), foo_v2.reference());
    index.add_public_candidate(repo_a.candidates().front(), repo_a.reference());

    const auto foo_candidates = index.find_candidates("foo");
    assert(foo_candidates.size() == 3);
    assert(foo_candidates[0].identity->repository == RepositoryId("repo-a"));
    assert(foo_candidates[0].identity->version == PackageVersion("1.0"));
    assert(foo_candidates[1].identity->repository == RepositoryId("repo-b"));
    assert(foo_candidates[2].identity->version == PackageVersion("2.0"));
    assert(foo_candidates[0].snapshot == repo_a.reference());
    assert(foo_candidates[0].public_candidate == repo_a.candidates().front());
    assert(foo_candidates[2].identity != foo_candidates[0].identity);
    assert(foo_candidates[0].public_candidate->provenance.metadata_reference == MetadataReference("metadata/foo"));
    assert(foo_candidates[0].platforms == std::vector<std::string>{"linux"});
    assert(foo_candidates[0].architectures == std::vector<std::string>{"x86_64"});
    assert(foo_candidates[0].abis == std::vector<std::string>{"glibc"});

    repository::RepositoryIndex reversed_index;
    reversed_index.add_public_candidate(repo_a.candidates().front(), repo_a.reference());
    reversed_index.add_public_candidate(foo_v2.candidates().front(), foo_v2.reference());
    reversed_index.add_public_candidate(repo_b.candidates().front(), repo_b.reference());
    const auto reversed = reversed_index.find_candidates("foo");
    assert(reversed.size() == foo_candidates.size());
    for (std::size_t i = 0; i < reversed.size(); ++i)
        assert(reversed[i].identity == foo_candidates[i].identity);

    // A mismatched snapshot is rejected; identity is never repaired from URL
    // or package name data.
    bool mismatch_rejected = false;
    try {
        (void)repository::ResolverCandidate::from_public(
            repo_a.candidates().front(), repo_b.reference());
    } catch (const repository::ResolverCandidateError& error) {
        mismatch_rejected = error.code() == repository::ResolverCandidateErrorCode::SnapshotMismatch;
    }
    assert(mismatch_rejected);

    // Public dependency expressions use the existing parser and retain role,
    // context, selector operators, and feature dependency declarations.
    auto rich_snapshot = make_snapshot("rich", "rich", "1.0", {},
        {"runtime(host):(capability(api) >= 1.0 AND (feature(gui) OR NOT helper))"});
    auto rich_metadata = rich_snapshot.candidates().front().package;
    rich_metadata.feature_dependencies["gui"] = {{
        DependencyExpression{"when(platform(linux), runtime(build):(helper OR NOT helper))"},
        DependencyRole::Development, DependencyTargetContext::Host, true}};
    auto rich_candidate = rich_snapshot.candidates().front();
    rich_candidate.package = rich_metadata;
    rich_snapshot = RepositorySnapshot::create(RepositoryId("rich"), RepositoryRevision("rev-1.0-rich"),
        {rich_candidate});
    const auto rich = repository::ResolverCandidate::from_public(
        rich_snapshot.candidates().front(), rich_snapshot.reference());
    assert(rich.dependencies.size() == 1);
    assert(rich.dependencies.front().kind == dependency::DependencyKind::Runtime);
    assert(rich.dependencies.front().context == dependency::DependencyContext::Host);
    assert(rich.feature_dependencies.contains("gui"));
    assert(rich.feature_dependencies.at("gui").size() == 1);
    assert(rich.feature_dependencies.at("gui").front().kind == dependency::DependencyKind::Development);
    assert(rich.feature_dependencies.at("gui").front().context == dependency::DependencyContext::Host);
    assert(rich.feature_dependencies.at("gui").front().expression->kind == dependency::Expression::Kind::Conditional);
    assert(rich.provides.capabilities == std::vector<std::string>{"api"});
    assert(rich.provides.features == std::vector<std::string>{"gui"});

    // The resolver assigns one logical package slot while candidate
    // alternatives retain repository identity through a failed branch.
    repository::RepositoryIndex backtracking_index;
    backtracking_index.add_public_candidate(repo_a.candidates().front(), repo_a.reference());
    backtracking_index.add_public_candidate(repo_b.candidates().front(), repo_b.reference());
    Package root{"root", "1.0", "", "", "", "", {
        dependency::parse_dependency("foo"),
        dependency::parse_dependency("blocked")
    }};
    backtracking_index.add(Package{"blocked", "1.0", "", "", "", "", {}});
    dependency::Environments environments{{"linux", "x86_64", "glibc"},
                                           {"linux", "x86_64", "glibc"},
                                           {"linux", "x86_64", "glibc"}};
    const auto policy = dependency::DependencyResolutionContext::for_phase(
        dependency::DependencyResolutionContext::Phase::RuntimeInstall);
    const auto solved = dependency::resolve_candidates(root, backtracking_index, environments, policy);
    const auto selected_foo = std::find_if(solved.begin(), solved.end(), [](const auto& candidate) {
        return candidate.name == "foo";
    });
    assert(selected_foo != solved.end());
    assert(selected_foo->identity);
    assert(selected_foo->identity->repository == RepositoryId("repo-b"));
    assert(selected_foo->snapshot == repo_b.reference());
    assert(selected_foo->public_candidate == repo_b.candidates().front());

    // Semantic provider assignments and retries likewise retain the full
    // selected provider candidate rather than only its package name.
    const auto provider_a = make_snapshot("provider-a-repo", "provider", "1.0", {"blocked"});
    const auto provider_b = make_snapshot("provider-b-repo", "provider", "1.0");
    auto a_candidate = provider_a.candidates().front();
    auto b_candidate = provider_b.candidates().front();
    a_candidate.package.capabilities = {"choice"};
    b_candidate.package.capabilities = {"choice"};
    const auto a_snapshot = RepositorySnapshot::create(RepositoryId("provider-a-repo"),
        RepositoryRevision("rev-1.0-provider-a-repo"), {a_candidate});
    const auto b_snapshot = RepositorySnapshot::create(RepositoryId("provider-b-repo"),
        RepositoryRevision("rev-1.0-provider-b-repo"), {b_candidate});
    repository::RepositoryIndex provider_index;
    provider_index.add_public_candidate(a_snapshot.candidates().front(), a_snapshot.reference());
    provider_index.add_public_candidate(b_snapshot.candidates().front(), b_snapshot.reference());
    provider_index.add(Package{"blocked", "1.0", "", "", "", "", {}});
    Package provider_root{"provider-root", "1.0", "", "", "", "", {
        dependency::parse_dependency("capability(choice)"),
        dependency::parse_dependency("blocked")
    }};
    const auto provider_solution = dependency::resolve_candidates(
        provider_root, provider_index, environments, policy);
    const auto chosen_provider = std::find_if(provider_solution.begin(), provider_solution.end(),
        [](const auto& candidate) { return candidate.name == "provider"; });
    assert(chosen_provider != provider_solution.end());
    assert(chosen_provider->identity->repository == RepositoryId("provider-b-repo"));
    assert(chosen_provider->snapshot == b_snapshot.reference());

    // Legacy insertion and legacy resolve_plan APIs continue to produce
    // package values without fabricated repository identifiers.
    repository::RepositoryIndex legacy_index;
    legacy_index.add(Package{"leaf", "1.0", "", "", "", "", {}});
    assert(!legacy_index.find_candidates("leaf").front().has_public_identity());
    Package legacy_root{"legacy-root", "1.0", "", "", "", "", {
        dependency::parse_dependency("leaf")
    }};
    const auto legacy_plan = dependency::resolve_plan(legacy_root, legacy_index, environments, policy);
    assert(legacy_plan.packages().size() == 2);

    assert(directory_entries() == directory_before);

    std::cout << "resolver_candidate_test: passed\n";
}
