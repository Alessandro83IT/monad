// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <athena/core/api.hpp>
#include <cassert>
#include <type_traits>

using namespace athena::core;

template<class Fn> void rejects(Fn fn) {
    bool rejected = false;
    try { fn(); } catch (const TypeError&) { rejected = true; }
    assert(rejected);
}

int main() {
    static_assert(static_cast<int>(ArtifactOrigin::BuiltFromAthenaIR) == 0);
    static_assert(static_cast<int>(ArtifactOrigin::ImportedBinary) == 1);
    static_assert(!std::is_same_v<ArtifactId, BuildInputId>);
    static_assert(!std::is_same_v<ArtifactId, ContentDigest>);
    ArtifactMetadata ambient{SchemaVersion(1),
        ArtifactId("sha256:" + std::string(64, 'a')),
        ContentDigest("sha256:" + std::string(64, 'b')),
        ManifestDigest("sha256:" + std::string(64, 'c')),
        Provenance{ArtifactOrigin::AmbientSourceBuild}, std::nullopt};
    ambient.validate();
    auto document = serialize(ambient);
    assert(document.find("ambient_source_build") != std::string::npos);
    assert(document.find("build_input_id") == std::string::npos);
    assert(deserialize_artifact_metadata(document) == ambient);
    auto controlled = ambient;
    controlled.provenance.origin = ArtifactOrigin::BuiltFromAthenaIR;
    rejects([&] { controlled.validate(); });
    controlled.build_input_id = BuildInputId("sha256:" + std::string(64, 'd'));
    controlled.validate();
    assert(deserialize_artifact_metadata(serialize(controlled)) == controlled);
    auto imported = ambient;
    imported.provenance.origin = ArtifactOrigin::ImportedBinary;
    imported.validate();
    assert(deserialize_artifact_metadata(serialize(imported)) == imported);
    for (auto value : {ambient, controlled, imported}) {
        auto invalid = value;
        invalid.artifact_id = ArtifactId{};
        rejects([&] { invalid.validate(); });
        invalid = value;
        invalid.build_input_id = BuildInputId{};
        rejects([&] { invalid.validate(); });
    }
    for (auto value : {ambient, imported}) {
        value.build_input_id = controlled.build_input_id;
        rejects([&] { value.validate(); });
        rejects([&] { (void)serialize(value); });
    }
    auto invalid = ambient;
    invalid.provenance.origin = static_cast<ArtifactOrigin>(99);
    rejects([&] { invalid.validate(); });
    auto malformed = document;
    malformed.replace(malformed.find("ambient_source_build"), 20, "unknown_origin");
    rejects([&] { (void)deserialize_artifact_metadata(malformed); });
    rejects([] { (void)ArtifactId(""); });
    rejects([] { (void)BuildInputId(""); });

    PackageMetadata package{PackageId("sample"), PackageVersion("1.0")};
    SourceRecipe recipe{SchemaVersion(1), package.package_id, package.version};
    recipe.sources.push_back({MetadataReference("source.tar")});
    recipe.build_backend = "cmake";
    RepositoryCandidate candidate{package,
        RepositoryProvenance{RepositoryId("repo"), RepositoryRevision("rev"),
                             MetadataReference("sample.toml")}, recipe, std::nullopt, ambient};
    candidate.validate();
    auto contradictory = candidate;
    contradictory.artifact = imported;
    rejects([&] { contradictory.validate(); });
    ImportedBinaryMetadata binary{SchemaVersion(1), package, MetadataReference("archive.tar")};
    binary.original_digest = ContentDigest("sha256:" + std::string(64, 'e'));
    auto binary_candidate = candidate;
    binary_candidate.source_recipe.reset();
    binary_candidate.imported_binary = binary;
    binary_candidate.artifact = imported;
    binary_candidate.validate();
    assert(binary.original_digest->value() != imported.artifact_id.value());
    assert(deserialize_imported_binary_metadata(serialize(binary)) == binary);
    binary_candidate.artifact = ambient;
    rejects([&] { binary_candidate.validate(); });

    auto snapshot = RepositorySnapshot::create(RepositoryId("repo"), RepositoryRevision("rev"), {candidate});
    assert(deserialize_repository_snapshot(serialize(snapshot)) == snapshot);
    InstallRequest request;
    request.targets = {{DependencySelector{SelectorKind::Package, "sample", {}}}};
    request.snapshots = {snapshot};
    request.repositories.emplace(RepositoryId("repo"), RepositorySelectionRule{});
    auto plan = plan_install(request);
    const auto& selected = plan.selections().front();
    assert(selected.candidate.artifact == ambient);
    assert(!selected.candidate.artifact->build_input_id);
    assert(selected.candidate.provenance.repository_id == candidate.provenance.repository_id);
    assert(selected.candidate.provenance.revision == candidate.provenance.revision);
    assert(selected.candidate.provenance.metadata_reference == candidate.provenance.metadata_reference);
    assert(selected.identity().snapshot == snapshot.reference());
    candidate.artifact.reset();
    request.snapshots = {RepositorySnapshot::create(RepositoryId("repo"), RepositoryRevision("rev"), {candidate})};
    assert(!plan_install(request).selections().front().candidate.artifact);
}
