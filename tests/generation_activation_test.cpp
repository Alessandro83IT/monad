#include <algorithm>
#include "generation/generation.hpp"
#include "activation/activation.hpp"
using athena::activation::target_is_free;
#include "store/manifest.hpp"
#include "object_store_test_support.hpp"
#include "artifact/scanner.hpp"
using namespace athena::core;
using namespace athena::generation;
using athena::realization::RealizationRecord;
RealizationRecord record(std::string name, ArtifactId id) {
    RepositorySnapshotRef snapshot{RepositoryId("repo"), RepositoryRevision("rev"), SchemaVersion(1),
        ContentDigest("sha256:" + std::string(64, 'a')), SnapshotId("sha256:" + std::string(64, 'b'))};
    return {{PackageId(name), PackageVersion("1"), snapshot.repository_id, snapshot,
        RepositoryProvenance{snapshot.repository_id, snapshot.revision, {}, snapshot}}, id, {}, ArtifactOrigin::AmbientSourceBuild};
}
template<class F> std::string failure(F action) {
    try { action(); } catch (const std::runtime_error& error) { return error.what(); }
    throw std::runtime_error("expected switch failure");
}
int main() {
    StoreFixture fixture;
    const auto root = fixture.root / "store", target = fixture.root / "target";
    fs::create_directory(root);
    ObjectStore objects(root);
    GenerationManager manager(fixture.root / "state"); manager.initialize();
    // Missing authoritative objects fail even on hosts unable to scan metadata.
    const auto missing = manager.create({}, {record("missing", ArtifactId("sha256:" + std::string(64, 'e')))});
    fs::create_directories(objects.object_path(missing.realizations[0].artifact_id).parent_path());
    rejects(ObjectErrorCode::ExistingObjectVerification, [&] { manager.switch_to(missing.id, target, objects); });
    require(manager.current().id == 0 && !fs::exists(target), "missing object pointer/target unchanged");
    const auto model_a = payload_model();
    const auto source_a = fixture.root / "source-a"; fs::create_directory(source_a);
    mechanics::materialize(source_a, model_a);
    const auto id_a = derive_artifact_id(model_a);
    try { objects.publish(source_a, id_a); }
    catch (const ObjectError& error) {
        if (error.code() != ObjectErrorCode::InvalidSource || std::string(error.what()).find("hidden trusted.*") == std::string::npos) throw;
        std::cout << "SKIP generation ObjectStore activation integration: " << error.what() << '\n';
        return 77;
    }
    // Focused type transitions use their own root and generation history.
    GenerationManager transitions(fixture.root / "transition-state"); transitions.initialize();
    const auto transition_root = fixture.root / "transition-target";
    auto publish_generation = [&](std::string name, NormalizedArtifact model) {
        normalize(model);
        const auto source = fixture.root / ("transition-" + name); fs::create_directory(source);
        mechanics::materialize(source, model);
        const auto id = derive_artifact_id(model); objects.publish(source, id);
        return transitions.create({}, {record(name, id)});
    };
    const auto directory = publish_generation("directory", {1, {{"node", Directory{}}}});
    const auto file = publish_generation("file", {1, {{"node", RegularFile{false, "new node"}}}});
    const auto link = publish_generation("link", {1, {{"node", SymbolicLink{"destination"}}}});
    transitions.switch_to(directory.id, transition_root, objects);
    require(fs::is_empty(transition_root / "node"), "explicit empty directory");
    transitions.switch_to(file.id, transition_root, objects);
    require(transitions.current().id == file.id && read_bytes(transition_root / "node") == "new node",
            "directory to file succeeds and advances current");
    transitions.switch_to(directory.id, transition_root, objects);
    require(transitions.current().id == directory.id && fs::is_directory(fs::symlink_status(transition_root / "node")) &&
            fs::is_empty(transition_root / "node"), "file to explicit empty directory");
    transitions.switch_to(link.id, transition_root, objects);
    require(transitions.current().id == link.id && fs::read_symlink(transition_root / "node") ==
            objects.object_path(link.realizations.front().artifact_id) / "node", "directory to source symlink");
    transitions.switch_to(directory.id, transition_root, objects);
    const auto conflicting = transitions.create({}, {file.realizations.front(), link.realizations.front()});
    failure([&] { transitions.switch_to(conflicting.id, transition_root, objects); });
    require(transitions.current().id == directory.id && fs::is_directory(fs::symlink_status(transition_root / "node")) &&
            fs::is_empty(transition_root / "node"), "later conflict restores removed explicit directory and pointer");
    fs::create_directory(transition_root / "unrelated");
    const auto unrelated = publish_generation("unrelated", {1, {{"unrelated", RegularFile{false, "blocked"}}}});
    failure([&] { transitions.switch_to(unrelated.id, transition_root, objects); });
    require(transitions.current().id == directory.id && fs::is_directory(transition_root / "unrelated") &&
            fs::is_directory(transition_root / "node"), "unrelated empty user directory preserved on failure");
    const auto shared_a = publish_generation("shared-a", {1, {{"usr", Directory{}}, {"usr/bin", Directory{}},
        {"usr/bin/a", RegularFile{false, "a"}}}});
    const auto shared_b = publish_generation("shared-b", {1, {{"usr", Directory{}}, {"usr/bin", Directory{}},
        {"usr/bin/b", RegularFile{false, "b"}}}});
    const auto both = transitions.create({}, {shared_a.realizations.front(), shared_b.realizations.front()});
    transitions.switch_to(both.id, transition_root, objects);
    transitions.switch_to(shared_b.id, transition_root, objects);
    require(fs::is_directory(transition_root / "usr") && fs::is_directory(transition_root / "usr/bin") &&
            read_bytes(transition_root / "usr/bin/b") == "b" && target_is_free(transition_root / "usr/bin/a"),
            "surviving source retains shared directories");
    // Legacy projections carry links only, exercising implicit parent liveness.
    const auto implicit_source = fixture.root / "implicit-source";
    fs::create_directories(implicit_source / "usr/bin");
    std::ofstream(implicit_source / "usr/bin/c") << "c";
    athena::store::write_manifest(implicit_source, {{"usr/bin/c"}});
    const auto implicit = transitions.create({{"implicit", implicit_source.string()}});
    transitions.switch_to(implicit.id, transition_root, objects);
    require(fs::is_directory(transition_root / "usr/bin") && read_bytes(transition_root / "usr/bin/c") == "c",
            "added source implicit parents remain live");
    const auto mixed_shared = transitions.create(implicit.entries, shared_a.realizations);
    transitions.switch_to(mixed_shared.id, transition_root, objects);
    transitions.switch_to(implicit.id, transition_root, objects);
    require(fs::is_directory(transition_root / "usr/bin") && read_bytes(transition_root / "usr/bin/c") == "c",
            "surviving implicit parents remain live");
    auto model_b = model_a;
    std::get<RegularFile>(std::find_if(model_b.entries.begin(), model_b.entries.end(), [](const auto& e) { return e.path == "bin/tool"; })->value).content = "replacement\n";
    const auto source_b = fixture.root / "source-b"; fs::create_directory(source_b);
    mechanics::materialize(source_b, model_b);
    const auto id_b = derive_artifact_id(model_b); objects.publish(source_b, id_b);
    const auto before_a = snapshot(objects.object_path(id_a)), before_b = snapshot(objects.object_path(id_b));
    const auto legacy = root / "legacy-1"; fs::create_directories(legacy / "bin");
    std::ofstream(legacy / "bin/legacy") << "legacy";
    athena::store::write_manifest(legacy, {{"bin/legacy"}});
    const auto g_legacy = manager.create({{"legacy", legacy.string()}});
    const auto g_a = manager.create({}, {record("pkg", id_a)});
    const auto g_b = manager.create({}, {record("pkg", id_b)});
    auto changed_candidate = record("pkg", id_a);
    changed_candidate.candidate.version = PackageVersion("2");
    const auto g_metadata = manager.create({}, {changed_candidate});
    const auto g_shared = manager.create({}, {record("one", id_a), record("two", id_a)});
    const auto g_mixed = manager.create({{"legacy", legacy.string()}}, {record("pkg", id_a)});
    manager.switch_to(g_legacy.id, target); // legacy -> legacy covered by existing generation_test
    manager.switch_to(g_a.id, target, objects);
    require(target_is_free(target / "bin/legacy"), "legacy removed");
    require(fs::read_symlink(target / "bin/tool") == objects.object_path(id_a) / "bin/tool", "canonical source");
    require(read_bytes(target / "binary") == std::string("\0\xff\n", 3), "binary activated");
    require(fs::is_directory(target / "empty"), "empty directory activated");
    require(fs::read_symlink(target / "escaping") == objects.object_path(id_a) / "escaping", "literal link activated");
    const auto links_before = snapshot(target);
    manager.switch_to(g_a.id, target, objects); unchanged(target, links_before);
    const auto g_a_again = manager.create({}, g_a.realizations);
    manager.switch_to(g_a_again.id, target, objects); unchanged(target, links_before);
    require(manager.current().id == g_a_again.id, "identical sources update only generation metadata");
    GenerationManager direct(fixture.root / "state/generations", GenerationManager::GenerationDirectoryTag{});
    direct.switch_to(g_metadata.id, target, objects); unchanged(target, links_before);
    manager.switch_to(g_shared.id, target, objects); unchanged(target, links_before);
    require(manager.current().id == g_shared.id && manager.current().realizations.size() == 2, "shared artifact metadata retained");
    manager.switch_to(g_a.id, target, objects); unchanged(target, links_before);
    failure([&] { manager.switch_to(g_b.id, objects.object_path(id_a), objects); });
    require(manager.current().id == g_a.id, "source namespace cannot be an activation target");
    manager.switch_to(g_b.id, target, objects);
    require(manager.current().id == g_b.id, "successful activation updates pointer");
    require(read_bytes(target / "bin/tool") == "replacement\n", "same candidate different artifact replaced");
    require(fs::read_symlink(target / "bin/tool") == objects.object_path(id_b) / "bin/tool", "replacement source");
    manager.switch_to(g_legacy.id, target, objects);
    require(target_is_free(target / "binary") && read_bytes(target / "bin/legacy") == "legacy", "realization to legacy");
    manager.switch_to(g_mixed.id, target, objects);
    require(read_bytes(target / "bin/legacy") == "legacy" && read_bytes(target / "bin/tool") == "hello\n", "mixed sources");
    // Two different artifacts collide. Earlier successful additions must also
    // be undone before restoring removed legacy sources.
    manager.switch_to(g_legacy.id, target, objects);
    const auto conflict = manager.create({}, {record("a", id_a), record("b", id_b)});
    auto error = failure([&] { manager.switch_to(conflict.id, target, objects); });
    require(error.find("Conflitto") != std::string::npos, "original conflict retained");
    require(manager.current().id == g_legacy.id && target_is_free(target / "binary") && read_bytes(target / "bin/legacy") == "legacy", "failed additions undone and removals restored");
    manager.switch_to(g_a.id, target, objects);
    std::ofstream(target / "blocked") << "sentinel";
    const auto blocked_source = fixture.root / "blocked-source"; fs::create_directory(blocked_source);
    NormalizedArtifact blocked_model{1, {{"blocked", RegularFile{false, "new"}}}};
    mechanics::materialize(blocked_source, blocked_model);
    const auto blocked_id = derive_artifact_id(blocked_model); objects.publish(blocked_source, blocked_id);
    auto blocked = manager.create({}, {record("blocked", blocked_id)});
    failure([&] { manager.switch_to(blocked.id, target, objects); });
    require(manager.current().id == g_a.id && read_bytes(target / "bin/tool") == "hello\n" && read_bytes(target / "blocked") == "sentinel", "removed object restored and pointer preserved");
    unchanged(objects.object_path(id_a), before_a); unchanged(objects.object_path(id_b), before_b);
    exact_payload(objects.object_path(id_a), model_a, true); exact_payload(objects.object_path(id_b), model_b, true);
    // Legacy namespace remains explicit, even when its locator aliases an object.
    failure([&] { manager.create({{"legacy", objects.object_path(id_a).string()}}); });
    const auto alias = root / "legacy-alias"; fs::create_symlink(objects.object_path(id_a), alias);
    auto alias_generation = manager.create({{"alias", alias.string()}});
    failure([&] { manager.switch_to(alias_generation.id, target, objects); }); // no fabricated legacy manifest
    require(manager.current().id == g_a.id, "alias cannot infer artifact identity");
    // Missing target is checked while the previous object is still valid.
    const auto target_before = snapshot(target);
    fs::rename(objects.object_path(id_b), root / "hidden-b");
    rejects(ObjectErrorCode::ExistingObjectVerification, [&] { manager.switch_to(g_b.id, target, objects); });
    require(manager.current().id == g_a.id, "missing pointer unchanged"); unchanged(target, target_before);
    fs::rename(root / "hidden-b", objects.object_path(id_b));
    // Tampering an unchanged/current object also fails full verification.
    require(::chmod((objects.object_path(id_a) / "binary").c_str(), 0644) == 0, "tamper fixture chmod");
    std::ofstream(objects.object_path(id_a) / "binary") << "tampered";
    rejects(ObjectErrorCode::CollisionCorruption, [&] { manager.switch_to(g_a.id, target, objects); });
    rejects(ObjectErrorCode::CollisionCorruption, [&] { manager.switch_to(g_b.id, target, objects); });
    require(manager.current().id == g_a.id, "tamper pointer unchanged"); unchanged(target, target_before);

    // A legitimate payload may contain a file that happens to parse as a legacy
    // manifest. A legacy alias to it remains a distinct activation source.
    GenerationManager namespaces(fixture.root / "namespace-state"); namespaces.initialize();
    const auto namespace_target = fixture.root / "namespace-target";
    NormalizedArtifact namespace_model{1, {{".athena", Directory{}},
        {".athena/manifest.toml", RegularFile{false, "[[files]]\npath='file'\n"}},
        {"file", RegularFile{false, "namespace payload"}}}};
    const auto namespace_source = fixture.root / "namespace-source"; fs::create_directory(namespace_source);
    mechanics::materialize(namespace_source, namespace_model);
    const auto namespace_id = derive_artifact_id(namespace_model); objects.publish(namespace_source, namespace_id);
    const auto legacy_alias = root / "manifest-alias"; fs::create_symlink(objects.object_path(namespace_id), legacy_alias);
    const auto legacy_namespace = namespaces.create({{"legacy", legacy_alias.string()}});
    const auto object_namespace = namespaces.create({}, {record("object", namespace_id)});
    namespaces.switch_to(legacy_namespace.id, namespace_target);
    namespaces.switch_to(object_namespace.id, namespace_target, objects);
    require(fs::read_symlink(namespace_target / "file") == objects.object_path(namespace_id) / "file", "namespace replacement not inferred from equivalent directory");
    require(fs::is_symlink(namespace_target / ".athena/manifest.toml"), "ordinary object manifest payload activated");
    namespaces.switch_to(legacy_namespace.id, namespace_target, objects);
    require(fs::read_symlink(namespace_target / "file") == legacy_alias / "file" &&
        target_is_free(namespace_target / ".athena/manifest.toml"), "namespace reverse replacement");
    exact_payload(objects.object_path(namespace_id), namespace_model, true);

}
