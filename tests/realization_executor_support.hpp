#pragma once
#include "realization/executor.hpp"
#include "artifact/scanner.hpp"
#include "artifact/identity.hpp"
#include "hash/sha256.hpp"
#include "object_store_test_support.hpp"
#include <cstdlib>
namespace execution_test {
using namespace athena;
inline core::SourceRecipe recipe(const fs::path& source, std::string name = "app") {
    core::SourceRecipe r{core::SchemaVersion{}, core::PackageId(name), core::PackageVersion("1")};
    r.sources.push_back({core::MetadataReference("file://" + source.native())});
    r.build_backend = "cmake";
    return r;
}
inline core::InstallPlan plan(std::vector<core::SourceRecipe> recipes, bool payload = true,
                             bool imported = false, bool metadata = false) {
    const core::RepositoryId repository("test");
    std::vector<core::RepositoryCandidate> candidates;
    for (const auto& r : recipes) {
        core::PackageMetadata package{r.package_id, r.version};
        core::RepositoryCandidate candidate{package, core::RepositoryProvenance{repository}};
        if (payload) candidate.source_recipe = r;
        if (imported) candidate.imported_binary = core::ImportedBinaryMetadata{
            core::SchemaVersion{}, package, core::MetadataReference("file:///unavailable")};
        if (metadata) candidate.artifact = core::ArtifactMetadata{core::SchemaVersion{},
            core::ArtifactId("sha256:" + std::string(64, 'a')), core::ContentDigest("sha256:" + std::string(64, 'b')),
            core::ManifestDigest("sha256:" + std::string(64, 'c')), {core::ArtifactOrigin::AmbientSourceBuild}};
        candidates.push_back(std::move(candidate));
    }
    auto snapshot = core::RepositorySnapshot::create(repository, core::RepositoryRevision("one"), candidates);
    core::ResolutionResult result;
    for (auto& candidate : snapshot.candidates_with_snapshot_reference())
        result.selections.push_back({std::move(candidate), snapshot.reference(), {}});
    return core::InstallPlanBuilder(std::move(result)).build();
}
inline fs::path archive_fixture(StoreFixture& fixture, const std::string& content) {
    const auto directory = fixture.root / "input" / "project";
    fs::create_directories(directory);
    std::ofstream(directory / "CMakeLists.txt") <<
        "cmake_minimum_required(VERSION 3.20)\nproject(fixture LANGUAGES NONE)\n"
        "install(FILES payload DESTINATION share/fixture)\n";
    std::ofstream(directory / "payload") << content;
    const auto archive = fixture.root / "fixture.tar.gz";
    const auto command = "tar -czf '" + archive.native() + "' -C '" + directory.parent_path().native() + "' project";
    require(std::system(command.c_str()) == 0, "create source archive");
    return archive;
}
inline void setup(StoreFixture& fixture) {
    fs::create_directory(fixture.root / "workspace");
    fs::create_directory(fixture.root / "objects");
    fs::create_directory(fixture.root / "generations");
    std::ofstream(fixture.root / "generations" / "sentinel") << "unchanged";
    fs::create_symlink("generations/sentinel", fixture.root / "current");
}
inline void boundary(const StoreFixture& fixture) {
    require(fs::is_empty(fixture.root / "workspace"), "temporary execution state leaked");
    require(read_bytes(fixture.root / "generations" / "sentinel") == "unchanged", "generation mutated");
    require(fs::read_symlink(fixture.root / "current") == "generations/sentinel", "activation changed");
    require(std::distance(fs::directory_iterator(fixture.root / "generations"), fs::directory_iterator{}) == 1, "generation created");
    require(!fs::exists(fixture.root / "objects" / "app-1"), "legacy entry created");
}
template<class F> inline void rejects(realization::ExecutionErrorCode code, F f) {
    try { f(); } catch (const realization::ExecutionError& e) {
        require(e.code() == code, "wrong execution error code"); return;
    }
    throw std::runtime_error("expected execution error");
}
}
