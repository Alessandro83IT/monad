#include <athena/core/repository.hpp>

#include <cassert>
#include <iostream>
#include <stdexcept>

using namespace athena::core;

template<class Fn>
bool rejects(Fn&& fn)
{
    try { fn(); }
    catch (const std::exception&) { return true; }
    return false;
}

int main()
{
    PackageMetadata package{
        PackageId("libsample"), PackageVersion("2.4.1"), SchemaVersion(1),
        "Sample library", "A test package",
        {"zlib >= 1.2", "openssl OR libressl"},
        {"crypto"}, {"openssl"}, {"oldsample"}
    };
    package.validate();
    const auto package_document = serialize(package);
    assert(package_document == serialize(package));
    const auto package_roundtrip = deserialize_package_metadata(package_document);
    assert(package_roundtrip == package);
    assert(rejects([] {
        PackageMetadata{PackageId("missing-version"), PackageVersion("invalid"), SchemaVersion(1)}.validate();
    }));
    assert(rejects([] {
        PackageMetadata{PackageId(""), PackageVersion("1.0"), SchemaVersion(1)}.validate();
    }));
    assert(rejects([] {
        PackageMetadata{PackageId("future"), PackageVersion("1.0"), SchemaVersion(2)}.validate();
    }));
    assert(rejects([] {
        PackageMetadata{PackageId("bad-dep"), PackageVersion("1.0"), SchemaVersion(1), {}, {}, {"not valid !!!"}}.validate();
    }));

    SourceRecipe recipe{
        SchemaVersion(1), PackageId("libsample"), PackageVersion("2.4.1")
    };
    recipe.sources.push_back({
        MetadataReference("sources/libsample-2.4.1.tar.xz"),
        ContentDigest("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        4096
    });
    recipe.build_inputs.emplace_back("sha256:build-input-opaque");
    recipe.build_backend = "cmake";
    recipe.build_parameters.emplace("BUILD_SHARED_LIBS", "ON");
    recipe.build_ir_reference.emplace("build-ir/libsample.toml");
    recipe.validate();
    const auto recipe_document = serialize(recipe);
    assert(recipe_document == serialize(recipe));
    assert(deserialize_source_recipe(recipe_document) == recipe);
    assert(rejects([] {
        SourceRecipe{SchemaVersion(1), PackageId("src"), PackageVersion("1.0")}.validate();
    }));
    assert(rejects([] {
        SourceRecipe{SchemaVersion(9), PackageId("src"), PackageVersion("1.0")}.validate();
    }));
    assert(rejects([] {
        SourceRecipe invalid{SchemaVersion(1), PackageId("src"), PackageVersion("1.0")};
        invalid.sources.push_back({MetadataReference("source.tar"), ContentDigest("sha256:not-hex")});
        invalid.build_backend = "make";
        invalid.validate();
    }));

    ImportedBinaryMetadata imported{
        SchemaVersion(1), package,
        MetadataReference("arch/core/libsample-2.4.1-1-x86_64.pkg.tar.zst"),
        ImportedBinaryFormat::ArchPackage,
        ContentDigest("sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        8192,
        RepositoryProvenance{
            RepositoryId("arch-core"), RepositoryRevision("sync-15"),
            MetadataReference("core/libsample-2.4.1"),
            RepositorySnapshotRef{
                RepositoryId("arch-core"), RepositoryRevision("sync-15"), SchemaVersion(1),
                ContentDigest("sha256:1111111111111111111111111111111111111111111111111111111111111111"),
                SnapshotId("sha256:2222222222222222222222222222222222222222222222222222222222222222")
            }
        },
        {{"arch.name", {"libsample"}}, {"arch.depends", {"glibc>=2.38"}}}
    };
    imported.validate();
    const auto imported_document = serialize(imported);
    assert(imported_document == serialize(imported));
    assert(deserialize_imported_binary_metadata(imported_document) == imported);
    assert(rejects([] {
        ImportedBinaryMetadata invalid{
            SchemaVersion(1), PackageMetadata{PackageId("b"), PackageVersion("invalid")},
            MetadataReference("archive.pkg"), ImportedBinaryFormat::GenericArchive
        };
        invalid.validate();
    }));
    assert(rejects([] {
        ImportedBinaryMetadata invalid{
            SchemaVersion(1), PackageMetadata{PackageId("b"), PackageVersion("1.0")},
            MetadataReference("archive.pkg"), ImportedBinaryFormat::GenericArchive,
            ContentDigest("sha256:bad-digest")
        };
        invalid.validate();
    }));

    ArtifactMetadata built{
        SchemaVersion(1), ArtifactId("artifact:one"),
        ContentDigest("sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
        ManifestDigest("sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"),
        Provenance{ArtifactOrigin::BuiltFromAthenaIR, "", "", "source.tar", ""},
        BuildInputId("build-input:one")
    };
    built.validate();
    const auto artifact_document = serialize(built);
    assert(artifact_document == serialize(built));
    assert(deserialize_artifact_metadata(artifact_document) == built);

    ArtifactMetadata imported_artifact{
        SchemaVersion(1), ArtifactId("artifact:two"),
        ContentDigest("sha256:eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        ManifestDigest("sha256:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
        Provenance{ArtifactOrigin::ImportedBinary, "arch-core", "sync-15", "archive.pkg", ""},
        std::nullopt
    };
    imported_artifact.validate();
    assert(imported_artifact.provenance.origin != built.provenance.origin);
    assert(deserialize_artifact_metadata(serialize(imported_artifact)) == imported_artifact);
    assert(rejects([] {
        ArtifactMetadata invalid{
            SchemaVersion(1), ArtifactId("artifact:invalid"),
            ContentDigest("sha256:123z"), ManifestDigest("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
            Provenance{ArtifactOrigin::BuiltFromAthenaIR}, BuildInputId("input:1")
        };
        invalid.validate();
    }));
    assert(rejects([] {
        ArtifactMetadata invalid{
            SchemaVersion(1), ArtifactId("artifact:invalid"),
            ContentDigest("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
            ManifestDigest("sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
            Provenance{ArtifactOrigin::BuiltFromAthenaIR}, std::nullopt
        };
        invalid.validate();
    }));

    RepositoryCandidate binary_candidate{
        package,
        RepositoryProvenance{RepositoryId("binary")},
        std::nullopt,
        imported,
        imported_artifact
    };
    binary_candidate.validate();
    RepositoryCandidate mismatched_binary{
        package,
        RepositoryProvenance{RepositoryId("binary")},
        std::nullopt,
        ImportedBinaryMetadata{
            SchemaVersion(1),
            PackageMetadata{PackageId("other"), PackageVersion("1.0")},
            MetadataReference("other.pkg"), ImportedBinaryFormat::GenericArchive
        },
        std::nullopt
    };
    assert(rejects([&] { mismatched_binary.validate(); }));

    std::cout << "metadata_schema_test: passed\n";
}
