#include <algorithm>
#include "activation/activation.hpp"
#include "generation/generation.hpp"
#include "store/manifest.hpp"
#include "object_store_test_support.hpp"
using namespace athena::activation;
template<class F> void fails(F action) {
    try { action(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("expected projection conflict");
}
int main() {
    StoreFixture fixture;
    const auto source = fixture.root / "source", target = fixture.root / "target";
    const auto model = payload_model();
    fs::create_directory(source);
    mechanics::materialize(source, model); mechanics::finalize(source, model);
    const auto before = snapshot(source);
    Projection projection;
    for (const auto& entry : model.entries)
        if (std::holds_alternative<Directory>(entry.value)) projection.directories.push_back(entry.path);
        else projection.links.push_back(entry.path);
    activate(source, target, projection);
    require(read_bytes(target / "binary") == std::string("\0\xff\n", 3), "binary projection");
    require((mode(target / "bin/tool") == 0777), "executable is a target symlink");
    require((fs::status(target / "bin/tool").permissions() & fs::perms::owner_exec) != fs::perms::none, "executable preserved");
    require(fs::is_directory(target / "empty") && fs::is_empty(target / "empty"), "empty directory projection");
    for (const auto& name : {"absolute", "escaping", "link"}) {
        require(fs::read_symlink(target / name) == source / name, "literal source link projection");
        require(fs::read_symlink(source / name) == std::get<SymbolicLink>(std::find_if(model.entries.begin(), model.entries.end(), [&](const auto& e) { return e.path == name; })->value).target, "literal target unchanged");
    }
    fails([&] { activate(source, target, projection); });
    unchanged(source, before); exact_payload(source, model, true);
    deactivate(source, target, projection);
    for (const auto& path : projection.links) require(target_is_free(target / path), "deactivation removes links");
    require(fs::is_directory(target / "empty"), "shared directory scaffolding retained");
    unchanged(source, before); exact_payload(source, model, true);
    // A source symlink must never become a writable parent for another entry.
    fs::create_symlink(source / "bin", target / "redirect");
    fails([&] { activate(source, target, Projection{{"redirect/new"}, {}}); });
    require(!fs::exists(source / "bin/new"), "source not mutated via target ancestor");
    fs::create_directory(target / "occupied");
    fails([&] { activate(source, target, Projection{{"occupied"}, {}}); });
    // Fail after one link was created; preserve preexisting directories/objects.
    fails([&] { activate(source, target, Projection{{"binary", "binary"}, {"new-empty"}}); });
    require(target_is_free(target / "binary") && !fs::exists(target / "new-empty"), "projection transaction rollback");
    fails([&] { activate(source, target, Projection{{"deep/parents/file", "deep/parents/file"}, {"explicit/empty/leaf"}}); });
    require(!fs::exists(target / "deep") && !fs::exists(target / "explicit") && fs::is_directory(target / "empty"),
            "failed activation removes all newly created ancestors and preserves existing directories");
    fails([&] { activate(source, target, Projection{{"../escape"}, {}}); });
    unchanged(source, before);
    fails([&] { activate(source, source, Projection{{}, {"new-directory"}}); });
    require(!fs::exists(source / "new-directory"), "overlapping target cannot mutate source");

    // Exercise switch rollback across multiple additions without depending on
    // scanner privilege: legacy sources use the same central switch transaction.
    const auto old = fixture.root / "old", first = fixture.root / "first", second = fixture.root / "second";
    for (const auto& path : {old, first, second}) fs::create_directory(path);
    std::ofstream(old / "old") << "old";
    std::ofstream(old / "new-dir") << "previous file";
    std::ofstream(first / "new") << "new";
    fs::create_directory(first / "new-dir");
    std::ofstream(first / "new-dir/child") << "child";
    std::ofstream(second / "new") << "conflict";
    athena::store::write_manifest(old, {{"old"}, {"new-dir"}});
    athena::store::write_manifest(first, {{"new"}, {"new-dir/child"}});
    athena::store::write_manifest(second, {{"new"}});
    athena::generation::GenerationManager manager(fixture.root / "state"); manager.initialize();
    const auto previous = manager.create({{"old", old.string()}});
    const auto next = manager.create({{"first", first.string()}, {"second", second.string()}});
    const auto switch_root = fixture.root / "switch-target";
    manager.switch_to(previous.id, switch_root);
    fails([&] { manager.switch_to(next.id, switch_root); });
    require(manager.current().id == previous.id && fs::is_symlink(switch_root / "old") &&
        target_is_free(switch_root / "new") && fs::is_symlink(switch_root / "new-dir"), "switch undoes prior additions and new directories before restoring removals");
    // Capability-independent directory replacement exercises implicit scaffolding.
    const auto tree = fixture.root / "tree", replacement = fixture.root / "replacement";
    fs::create_directories(tree / "node"); fs::create_directory(replacement);
    std::ofstream(tree / "node/child") << "child";
    std::ofstream(replacement / "node") << "replacement";
    athena::store::write_manifest(tree, {{"node/child"}});
    athena::store::write_manifest(replacement, {{"node"}});
    const auto tree_generation = manager.create({{"tree", tree.string()}});
    const auto replacement_generation = manager.create({{"replacement", replacement.string()}});
    manager.switch_to(tree_generation.id, switch_root);
    manager.switch_to(replacement_generation.id, switch_root);
    require(manager.current().id == replacement_generation.id && read_bytes(switch_root / "node") == "replacement",
            "implicit directory to file updates current after success");
    manager.switch_to(tree_generation.id, switch_root);
    require(fs::is_directory(switch_root / "node") && read_bytes(switch_root / "node/child") == "child",
            "file to implicit directory");
    const auto replacement_conflict = manager.create({{"replacement", replacement.string()}, {"duplicate", replacement.string()}});
    fails([&] { manager.switch_to(replacement_conflict.id, switch_root); });
    require(manager.current().id == tree_generation.id && fs::is_directory(switch_root / "node") &&
            read_bytes(switch_root / "node/child") == "child", "rollback restores removed implicit scaffolding and current");

    const auto peer = fixture.root / "peer"; fs::create_directories(peer / "node");
    std::ofstream(peer / "node/peer") << "peer";
    athena::store::write_manifest(peer, {{"node/peer"}});
    const auto shared = manager.create({{"tree", tree.string()}, {"peer", peer.string()}});
    const auto peer_only = manager.create({{"peer", peer.string()}});
    manager.switch_to(shared.id, switch_root);
    manager.switch_to(peer_only.id, switch_root);
    require(fs::is_directory(switch_root / "node") && read_bytes(switch_root / "node/peer") == "peer" &&
            target_is_free(switch_root / "node/child"), "shared implicit directory survives membership removal");
    std::ofstream(switch_root / "node/user") << "sentinel";
    fails([&] { manager.switch_to(replacement_generation.id, switch_root); });
    require(manager.current().id == peer_only.id && read_bytes(switch_root / "node/user") == "sentinel" &&
            read_bytes(switch_root / "node/peer") == "peer", "nonempty old scaffold preserves unrelated user content");
    const auto occupied_root = fixture.root / "occupied-root";
    fs::create_directories(occupied_root / "node");
    athena::generation::GenerationManager occupied(fixture.root / "occupied-state"); occupied.initialize();
    const auto occupied_generation = occupied.create({{"replacement", replacement.string()}});
    fails([&] { occupied.switch_to(occupied_generation.id, occupied_root); });
    require(occupied.current().id == 0 && fs::is_directory(occupied_root / "node") && fs::is_empty(occupied_root / "node"),
            "unrelated preexisting empty directory is never deleted");

    athena::generation::GenerationManager nested(fixture.root / "nested-state"); nested.initialize();
    const auto nested_conflict = nested.create({{"first", replacement.string()}, {"second", replacement.string()}});
    const auto nested_root = fixture.root / "new-parent/deeper/target";
    fails([&] { nested.switch_to(nested_conflict.id, nested_root); });
    require(nested.current().id == 0 && !fs::exists(fixture.root / "new-parent"),
            "failed switch removes created target-root ancestors descendant-first");

}
