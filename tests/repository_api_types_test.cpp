// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include <athena/core/api.hpp>

#include <cassert>
#include <iostream>
#include <stdexcept>

int main()
{
    using namespace athena::core;

    const RepositoryId core_repo("core");
    const RepositoryRevision revision("sync-0042");
    const ContentDigest metadata_digest("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    const RepositorySnapshotRef snapshot{
        core_repo, revision, SchemaVersion(1), metadata_digest
    };
    snapshot.validate();

    const PackageMetadata metadata{
        PackageId("zlib"), PackageVersion("1.3.1"), SchemaVersion(1)
    };
    metadata.validate();
    const RepositoryCandidate candidate{
        metadata,
        RepositoryProvenance{
            core_repo,
            revision,
            MetadataReference("packages/zlib-1.3.1.toml"),
            snapshot
        }
    };
    candidate.validate();
    assert(candidate.provenance.repository_id == core_repo);
    assert(candidate.provenance.snapshot == snapshot);
    assert(candidate.package.package_id == PackageId("zlib"));

    const RepositoryRequest request{
        SchemaVersion(1),
        {RepositoryId("core"), RepositoryId("extra")},
        {PackageId("zlib"), PackageId("curl")},
        TargetEnvironment{"linux", "x86_64", "gnu", "x86_64-linux-gnu"}
    };
    request.validate();
    const RepositoryRequest same_request{
        SchemaVersion(1),
        {RepositoryId("core"), RepositoryId("extra")},
        {PackageId("zlib"), PackageId("curl")},
        TargetEnvironment{"linux", "x86_64", "gnu", "x86_64-linux-gnu"}
    };
    assert(request == same_request);
    assert(!(request == RepositoryRequest{
        SchemaVersion(1), {RepositoryId("extra"), RepositoryId("core")},
        {PackageId("zlib"), PackageId("curl")}, request.target
    }));

    assert(RepositoryRevision("r1") == RepositoryRevision("r1"));
    assert(RepositoryRevision("r1") < RepositoryRevision("r2"));
    assert(SchemaVersion(1) == candidate.package.schema_version);
    assert(schema_name(SchemaKind::RepositoryRequest) == "repository_request");

    bool rejected = false;
    try {
        RepositoryRequest{SchemaVersion(1), {}, {PackageId("zlib")}, {}}.validate();
    } catch (const TypeError&) {
        rejected = true;
    }
    assert(rejected);

    rejected = false;
    try {
        RepositoryRequest{
            SchemaVersion(99), {RepositoryId("core")}, {PackageId("zlib")}, {}
        }.validate();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);

    rejected = false;
    try {
        RepositoryCandidate{
            metadata,
            RepositoryProvenance{
                RepositoryId("other"), revision, {}, snapshot
            }
        }.validate();
    } catch (const TypeError&) {
        rejected = true;
    }
    assert(rejected);

    rejected = false;
    try {
        RepositorySnapshotRef{
            core_repo, revision, SchemaVersion(99), metadata_digest
        }.validate();
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);

    SourceRecipe source_recipe{
        SchemaVersion(1), PackageId("zlib"), PackageVersion("1.3.1")
    };
    source_recipe.sources.push_back({MetadataReference("sources/zlib.tar.xz")});
    source_recipe.build_backend = "cmake";
    const ImportedBinaryMetadata imported{
        SchemaVersion(1), metadata,
        MetadataReference("arch/core/zlib.pkg.tar.zst"),
        ImportedBinaryFormat::ArchPackage
    };
    const ArtifactMetadata artifact{
        SchemaVersion(1),
        ArtifactId("sha256:artifact"),
        ContentDigest("sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        ManifestDigest("sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
        Provenance{ArtifactOrigin::ImportedBinary},
        {}
    };
    imported.validate();
    artifact.validate();
    assert(source_recipe.sources.front().reference != imported.archive_reference);
    assert(artifact.artifact_id.value() == "sha256:artifact");
    assert(artifact.provenance.origin == ArtifactOrigin::ImportedBinary);

    std::cout << "repository_api_types_test: passed\n";
}
