#ifdef NDEBUG
#undef NDEBUG
#endif
#include "realization/record.hpp"
#include "store/object_store.hpp"
#include <algorithm>
#include <cassert>

using namespace athena::core;
using namespace athena::realization;
std::string id(char c) { return "sha256:" + std::string(64, c); }
template<class F> void rejects(F f) {
    bool rejected = false;
    try { f(); } catch (const TypeError&) { rejected = true; }
    assert(rejected);
}
int main() {
    RepositorySnapshotRef s{RepositoryId("repo-A"), RepositoryRevision("revision-X"),
        SchemaVersion(1), ContentDigest(id('a')), SnapshotId(id('b'))};
    RealizationRecord ambient{{PackageId("foo"), PackageVersion("1.0"), s.repository_id, s,
        RepositoryProvenance{s.repository_id, s.revision, MetadataReference("foo/recipe\n\"quoted\""), s}},
        ArtifactId(id('c')), {}, ArtifactOrigin::AmbientSourceBuild};
    auto controlled = ambient;
    controlled.origin = ArtifactOrigin::BuiltFromAthenaIR;
    controlled.build_input_id = BuildInputId(id('d'));
    auto imported = ambient;
    imported.origin = ArtifactOrigin::ImportedBinary;
    for (const auto& r : {ambient, controlled, imported}) {
        r.validate();
        auto text = serialize(r);
        assert(deserialize_record(text) == r);
        assert(serialize(deserialize_record(text)) == text);
        assert(r.object_reference() == "objects/v1/sha256/" + std::string(64, 'c'));
        // Mapping and codecs are pure: this root is neither created nor inspected.
        athena::store::ObjectStore store("/nonexistent-athena-realization-contract-root");
        assert(store.object_path(r.artifact_id) ==
            std::filesystem::path("/nonexistent-athena-realization-contract-root") / r.object_reference());
    }
    auto other = ambient;
    other.candidate.repository = RepositoryId("repo-B");
    other.candidate.snapshot.repository_id = other.candidate.repository;
    other.candidate.snapshot.snapshot_id = SnapshotId(id('e'));
    other.candidate.provenance.repository_id = other.candidate.repository;
    other.candidate.provenance.snapshot = other.candidate.snapshot;
    other.validate();
    assert(other != ambient && other.artifact_id == ambient.artifact_id);
    auto output = ambient;
    output.artifact_id = ArtifactId(id('f'));
    output.validate();
    assert(output != ambient && output.candidate == ambient.candidate);
    auto another_controlled = controlled;
    another_controlled.artifact_id = output.artifact_id;
    another_controlled.validate();
    assert(another_controlled != controlled && another_controlled.build_input_id == controlled.build_input_id);
    for (const auto& malformed : std::vector<std::string>{"", "opaque", "sha256:abc", "sha256:" + std::string(64, 'A'),
                                  "sha256:" + std::string(64, 'g')}) {
        auto bad = ambient;
        bad.artifact_id = malformed.empty() ? ArtifactId{} : ArtifactId(malformed);
        rejects([&] { bad.validate(); });
        bad = controlled;
        bad.build_input_id = malformed.empty() ? BuildInputId{} : BuildInputId(malformed);
        rejects([&] { bad.validate(); });
    }
    auto bad = ambient;
    bad.candidate.package = PackageId{};
    rejects([&] { bad.validate(); });
    bad = ambient; bad.candidate.repository = RepositoryId("wrong");
    rejects([&] { bad.validate(); });
    bad = ambient; bad.candidate.snapshot.snapshot_id.reset();
    rejects([&] { bad.validate(); });
    bad = ambient; bad.candidate.provenance.revision = RepositoryRevision("wrong");
    rejects([&] { bad.validate(); });
    bad = ambient; bad.candidate.provenance.snapshot->metadata_digest = ContentDigest(id('f'));
    rejects([&] { bad.validate(); });
    for (auto r : {ambient, imported}) {
        r.build_input_id = controlled.build_input_id;
        rejects([&] { r.validate(); });
    }
    bad = controlled; bad.build_input_id.reset();
    rejects([&] { bad.validate(); });
    bad = ambient; bad.origin = static_cast<ArtifactOrigin>(99);
    rejects([&] { bad.validate(); });
    auto text = serialize(ambient);
    auto unknown = text;
    unknown.replace(unknown.find("schema_version = 1"), 18, "schema_version = 2");
    rejects([&] { (void)deserialize_record(unknown); });
    for (auto path : {"/tmp/private", "objects/v1/sha256/.publish-private", "objects//v1/sha256/digest"})
        rejects([&] { (void)deserialize_record("object_reference = '" + std::string(path) + "'\n" + text); });
    rejects([&] { (void)deserialize_record("build_input_id = 1\n" + text); });
    rejects([&] { (void)deserialize_record("bad TOML ["); });
    auto invalid_origin = text;
    invalid_origin.replace(invalid_origin.find("ambient_source_build"), 20, "unknown_origin");
    rejects([&] { (void)deserialize_record(invalid_origin); });
    auto sparse = ambient;
    sparse.candidate.provenance.revision.reset();
    sparse.candidate.provenance.snapshot.reset();
    sparse.candidate.provenance.metadata_reference.reset();
    assert(deserialize_record(serialize(sparse)) == sparse);
    std::vector<RealizationRecord> records{other, output, controlled, ambient, imported, sparse};
    auto reverse = records;
    std::reverse(reverse.begin(), reverse.end());
    std::sort(records.begin(), records.end());
    std::sort(reverse.begin(), reverse.end());
    assert(records == reverse);
    for (const auto& a : records) {
        assert(!(a < a));
        for (const auto& b : records) {
            assert((a == b) == (!(a < b) && !(b < a)));
            for (const auto& c : records)
                if (a < b && b < c) assert(a < c);
        }
    }
}
