// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "generation/generation.hpp"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <source_location>
#include <iostream>
#include <unistd.h>
using namespace athena::core;
using namespace athena::generation;
using athena::realization::RealizationRecord;
std::string digest(char c) { return "sha256:" + std::string(64, c); }
template<class F> void rejects(F f, std::source_location location = std::source_location::current()) {
    bool rejected = false;
    try { f(); }
    catch (const TypeError&) { rejected = true; }
    catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) std::cerr << "Expected rejection at line " << location.line() << "\n";
    assert(rejected);
}
RealizationRecord record(std::string name) {
    RepositorySnapshotRef s{RepositoryId("repo"), RepositoryRevision("rev"),
        SchemaVersion(1), ContentDigest(digest('a')), SnapshotId(digest('b'))};
    return {{PackageId(name), PackageVersion("1.0"), s.repository_id, s,
        RepositoryProvenance{s.repository_id, s.revision, MetadataReference("recipe\n\"quoted\""), s}},
        ArtifactId(digest('c')), {}, ArtifactOrigin::AmbientSourceBuild};
}
int main() {
    const std::string old = "id = 7\n[[entries]]\npackage = 'z'\nstore = '/absent/store/z-1'\n"
        "[[entries]]\npackage = 'a'\nstore = 'relative/a-2'\n";
    auto legacy = deserialize_generation(old);
    assert(legacy.id == 7 && legacy.realizations.empty());
    assert(deserialize_generation(serialize(legacy)).entries == legacy.entries);
    assert(deserialize_generation("id = 0").entries.empty());
    // Historical duplicate/empty strings were not forbidden by the old reader.
    auto historical = deserialize_generation("id=1\n[[entries]]\npackage=''\nstore=''\n[[entries]]\npackage=''\nstore='other'");
    assert(deserialize_generation(serialize(historical)).entries == historical.entries);
    Generation escaped{2, {{"quoted\"name", "relative/path\nwith\"quotes"}}};
    assert(deserialize_generation(serialize(escaped)).entries == escaped.entries);
    auto ambient = record("ambient");
    auto controlled = record("controlled");
    controlled.origin = ArtifactOrigin::BuiltFromAthenaIR;
    controlled.build_input_id = BuildInputId(digest('d'));
    auto imported = record("imported");
    imported.origin = ArtifactOrigin::ImportedBinary;
    for (const auto& r : {ambient, controlled, imported}) {
        auto g = make_generation(8, {}, {r});
        auto loaded = deserialize_generation(serialize(g));
        assert(loaded.id == g.id && loaded.entries.empty() && loaded.realizations == g.realizations);
        assert(serialize(loaded) == serialize(g));
    }
    std::vector<RealizationRecord> records{imported, controlled, ambient};
    Generation mixed{9, legacy.entries, records};
    auto text = serialize(mixed);
    std::reverse(mixed.realizations.begin(), mixed.realizations.end());
    assert(serialize(mixed) == text);
    auto loaded = deserialize_generation(text);
    assert(loaded.entries == legacy.entries && loaded.realizations.size() == 3);
    assert(loaded.realizations[0].artifact_id == loaded.realizations[1].artifact_id);
    assert(loaded.realizations[0].candidate != loaded.realizations[1].candidate);
    auto contradictory = ambient;
    contradictory.artifact_id = ArtifactId(digest('e'));
    rejects([&] { make_generation(1, {}, {ambient, contradictory}); });
    rejects([&] { make_generation(1, {}, {ambient, ambient}); });
    auto other_repo = ambient;
    other_repo.candidate.provenance.metadata_reference = MetadataReference("other");
    rejects([&] { make_generation(1, {}, {ambient, other_repo}); });
    rejects([&] { make_generation(1, {{"ambient", "legacy"}}, {ambient}); });
    for (auto bad : {ambient, controlled, imported}) {
        bad.artifact_id = ArtifactId("/store/objects/v1/sha256/digest");
        rejects([&] { serialize(Generation{1, {}, {bad}}); });
    }
    auto bad = controlled;
    bad.build_input_id = BuildInputId("sha256:bad");
    rejects([&] { serialize(Generation{1, {}, {bad}}); });
    bad = ambient; bad.build_input_id = controlled.build_input_id;
    rejects([&] { serialize(Generation{1, {}, {bad}}); });
    bad = controlled; bad.build_input_id.reset();
    rejects([&] { serialize(Generation{1, {}, {bad}}); });
    // Exercise decoding through the same C.1 validators, not just construction.
    for (const auto& pair : std::vector<std::pair<std::string, std::string>>{
        {digest('c'), "sha256:bad"}, {digest('d'), "sha256:bad"},
        {"ambient_source_build", "built_from_athena_ir"}}) {
        auto malformed = text;
        malformed.replace(malformed.find(pair.first), pair.first.size(), pair.second);
        rejects([&] { deserialize_generation(malformed); });
    }
    for (const auto& malformed : {
        "schema_version=3\nid=1\nmemberships=[]", "schema_version='2'\nid=1\nmemberships=[]",
        "schema_version=2.0\nid=1\nmemberships=[]", "schema_version=2\nid=-1\nmemberships=[]",
        "schema_version=2\nid=1\nentries=[]", "schema_version=2\nid=1\nmemberships='bad'",
        "schema_version=2\nid=1\nmemberships=[1]", "id=1\nentries=[1]",
        "id=1\nentries='bad'", "id=1\n[[entries]]\npackage='x'",
        "id=1\n[[entries]]\npackage=1\nstore='x'", "id=1\nmemberships=[]",
        "schema_version=2\nid=1\n[[memberships]]\nkind='unknown'",
        "schema_version=2\nid=1\n[[memberships]]\npackage='x'\nstore='x'",
        "schema_version=2\nid=1\n[[memberships]]\nkind='realization'\nstore='x'",
        "schema_version=2\nid=1\n[[memberships]]\nkind='legacy'\npackage='x'\nstore='x'\nartifact_id='x'",
        "id=1\n[[entries]]\npackage='x'\nstore='x'\nkind='realization'"})
        rejects([&] { deserialize_generation(malformed); });
    for (const auto& path : {"objects/v1/sha256/abc", "/store/objects/v1/sha256/abc", "sha256:abc"})
        rejects([&] { serialize(Generation{1, {{"x", path}}}); });
    // Above codecs use nonexistent references and never inspect any payload.
    // Persistence writes metadata only; loading does not rewrite old documents.
    char pattern[] = "/tmp/athena-membership-XXXXXX";
    auto* directory = mkdtemp(pattern);
    assert(directory);
    const std::filesystem::path root(directory);
    std::filesystem::create_directories(root / "generations/7");
    const auto file = root / "generations/7/generation.toml";
    { std::ofstream out(file); out << old; }
    GenerationManager manager(root);
    manager.initialize();
    assert(manager.get(7).entries == legacy.entries);
    { std::ifstream in(file); assert(std::string((std::istreambuf_iterator<char>(in)), {}) == old); }
    // These context-boundary fixtures are modern, so provide explicit known
    // semantics. Unknown schema-3 activation is tested separately.
    auto context_entries = legacy.entries;
    for (auto& entry : context_entries) {
        entry.semantics = athena::installed::SemanticState{PackageMetadata{PackageId(entry.package), PackageVersion("1")}};
        entry.semantics->legacy_restricted = true;
    }
    auto context_records = records;
    for (auto& record : context_records)
        record.semantics = athena::installed::SemanticState{PackageMetadata{record.candidate.package, record.candidate.version}};
    auto created = manager.create(context_entries, context_records);
    assert(manager.get(created.id).realizations == created.realizations);
    assert(manager.list().size() == 3 && manager.current().id == 0);
    // These are missing-context tests, replacing the C.2 unsupported-kind guard.
    auto requires_context = [&](std::uint64_t id) {
        bool rejected = false;
        try { manager.switch_to(id, root / "target"); }
        catch (const std::runtime_error& error) {
            rejected = std::string(error.what()).find("ObjectStore context") != std::string::npos;
        }
        assert(rejected);
    };
    requires_context(created.id);
    assert(manager.current().id == 0 && !std::filesystem::exists(root / "target"));
    auto count = manager.list().size();
    rejects([&] { manager.create({}, {ambient, contradictory}); });
    assert(manager.list().size() == count);
    { std::ofstream out(root / "generations/current"); out << created.id; }
    assert(manager.current().realizations == created.realizations);
    requires_context(0);
    assert(manager.current().id == created.id && !std::filesystem::exists(root / "target"));
    requires_context(created.id);
    { std::ofstream out(file); out << "id=8"; }
    rejects([&] { manager.get(7); });
    std::filesystem::remove_all(root);
}
