// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include <athena/core/repository.hpp>

#include <algorithm>
#include <cassert>
#include <iostream>
#include <type_traits>
#include <utility>

using namespace athena::core;

namespace {

RepositoryCandidate candidate(
    const std::string& id,
    const std::string& version,
    const std::string& revision,
    const std::string& description = "fixture"
)
{
    return RepositoryCandidate{
        PackageMetadata{
            PackageId(id), PackageVersion(version), SchemaVersion(1),
            id, description
        },
        RepositoryProvenance{
            RepositoryId("fixture-repo"), RepositoryRevision(revision),
            MetadataReference("packages/" + id + ".toml"), std::nullopt
        }
    };
}

}

int main()
{
    const auto a = candidate("alpha", "1.2.0", "rev-17");
    const auto z = candidate("zeta", "3.0", "rev-17");

    const auto first = RepositorySnapshot::create(
        RepositoryId("fixture-repo"), RepositoryRevision("rev-17"), {a, z});
    const auto second = RepositorySnapshot::create(
        RepositoryId("fixture-repo"), RepositoryRevision("rev-17"), {z, a});

    // Insertion order does not affect snapshot ordering, bytes, metadata digest, or identity.
    assert(first == second);
    assert(first.candidates().front().package.package_id == PackageId("alpha"));
    assert(serialize(first) == serialize(second));
    assert(first.metadata_digest() == second.metadata_digest());
    assert(first.id() == second.id());
    assert(serialize(first) == serialize(first));

    const auto changed = RepositorySnapshot::create(
        RepositoryId("fixture-repo"), RepositoryRevision("rev-17"),
        {a, candidate("zeta", "3.0", "rev-17", "changed description")});
    assert(serialize(first) != serialize(changed));
    assert(first.metadata_digest() != changed.metadata_digest());
    assert(first.id() != changed.id());

    auto a_new_revision = a;
    auto z_new_revision = z;
    a_new_revision.provenance.revision = RepositoryRevision("rev-18");
    z_new_revision.provenance.revision = RepositoryRevision("rev-18");
    const auto new_revision = RepositorySnapshot::create(
        RepositoryId("fixture-repo"), RepositoryRevision("rev-18"),
        {a_new_revision, z_new_revision});
    assert(first.metadata_digest() == new_revision.metadata_digest());
    assert(first.id() != new_revision.id());
    auto a_other_repository = a;
    auto z_other_repository = z;
    a_other_repository.provenance.repository_id = RepositoryId("other-repo");
    z_other_repository.provenance.repository_id = RepositoryId("other-repo");
    const auto new_repository = RepositorySnapshot::create(
        RepositoryId("other-repo"), RepositoryRevision("rev-17"),
        {a_other_repository, z_other_repository});
    assert(first.metadata_digest() == new_repository.metadata_digest());
    assert(first.id() != new_repository.id());

    const auto document = serialize(first);
    const auto restored = deserialize_repository_snapshot(document);
    assert(restored == first);
    assert(serialize(restored) == document);

    const auto reference = first.reference();
    reference.validate();
    assert(reference.repository_id == first.repository_id());
    assert(reference.revision == first.revision());
    assert(reference.schema_version == first.schema_version());
    assert(reference.metadata_digest == first.metadata_digest());
    assert(reference.snapshot_id == first.id());
    const auto annotated_candidates = first.candidates_with_snapshot_reference();
    assert(annotated_candidates.size() == first.candidates().size());
    assert(annotated_candidates.front().provenance.snapshot == reference);
    static_assert(!std::is_same_v<SnapshotId, ArtifactId>);

    // Snapshot access is read-only; mutating a detached value copy cannot change it.
    static_assert(std::is_const_v<std::remove_reference_t<
        decltype(std::declval<const RepositorySnapshot&>().candidates())>>);
    auto detached = first.candidates().front();
    detached.package.description = "detached mutation";
    assert(first.candidates().front().package.description == "fixture");
    auto detached_annotated = annotated_candidates.front();
    detached_annotated.package.description = "annotated detached mutation";
    assert(first.candidates().front().package.description == "fixture");
    assert(first.id() == second.id());

    // An empty repository is a valid, deterministic snapshot.
    const auto empty = RepositorySnapshot::create(
        RepositoryId("empty"), RepositoryRevision("rev-1"), {});
    assert(empty.candidates().empty());
    assert(empty == deserialize_repository_snapshot(serialize(empty)));

    auto rejects = [](auto&& operation) {
        try { operation(); }
        catch (const std::exception&) { return true; }
        return false;
    };
    assert(rejects([&] {
        RepositorySnapshot::create(
            RepositoryId("fixture-repo"), RepositoryRevision("rev-17"),
            {candidate("alpha", "1.2.0", "other-rev")});
    }));
    assert(rejects([&] {
        auto mismatched = candidate("alpha", "1.2.0", "rev-17");
        mismatched.provenance.repository_id = RepositoryId("other-repo");
        RepositorySnapshot::create(
            RepositoryId("fixture-repo"), RepositoryRevision("rev-17"), {mismatched});
    }));
    assert(rejects([&] {
        RepositorySnapshot::create(
            RepositoryId("fixture-repo"), RepositoryRevision("rev-17"),
            {a, candidate("alpha", "1.2.0", "rev-17", "duplicate")});
    }));
    assert(rejects([&] {
        RepositorySnapshot::create(
            RepositoryId("fixture-repo"), RepositoryRevision("rev-17"),
            {RepositoryCandidate{
                PackageMetadata{PackageId("bad"), PackageVersion("invalid")},
                RepositoryProvenance{RepositoryId("fixture-repo")}
            }});
    }));
    assert(rejects([&] {
        RepositorySnapshot::create(
            RepositoryId("fixture-repo"), RepositoryRevision("rev-17"), {}, SchemaVersion(2));
    }));
    assert(rejects([&] {
        RepositorySnapshotRef{
            RepositoryId("fixture-repo"), RepositoryRevision("rev-17"), SchemaVersion(1),
            ContentDigest("sha512:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
            SnapshotId("sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
        }.validate();
    }));
    assert(rejects([&] {
        auto recursive = a;
        recursive.provenance.snapshot = first.reference();
        RepositorySnapshot::create(
            RepositoryId("fixture-repo"), RepositoryRevision("rev-17"), {recursive});
    }));

    auto corrupted = document;
    const auto marker = corrupted.find("snapshot_id");
    assert(marker != std::string::npos);
    const auto digest = corrupted.find("sha256:", marker);
    assert(digest != std::string::npos);
    const auto hex = digest + std::string("sha256:").size();
    assert(hex != std::string::npos);
    corrupted[hex] = corrupted[hex] == 'a' ? 'b' : 'a';
    assert(rejects([&] { (void)deserialize_repository_snapshot(corrupted); }));

    std::cout << "repository_snapshot_test: passed\n";
}
