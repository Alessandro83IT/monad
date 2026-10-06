// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "object_store_test_support.hpp"
#include "artifact/scanner.hpp"

int main() {
    StoreFixture fixture;
    const auto model = payload_model();
    const auto source = fixture.root / "source", independent = fixture.root / "independent";
    fs::create_directory(source); fs::create_directory(independent);
    mechanics::materialize(source, model); mechanics::materialize(independent, model);
    const auto source_before = snapshot(source);
    require(::chmod((independent / "bin/tool").c_str(), 0700) == 0 &&
            ::chmod((independent / "binary").c_str(), 0600) == 0 &&
            ::chmod((independent / "empty").c_str(), 0700) == 0, "independent normalized modes");
    const auto independent_before = snapshot(independent);
    const auto id = derive_artifact_id(model);
    const auto root = fixture.root / "store"; fs::create_directory(root);
    ObjectStore store(root);
    // Scanner limitation is detected with the real publication entry point.
    try { store.publish(source, id); }
    catch (const ObjectError& error) {
        if (error.code() != ObjectErrorCode::InvalidSource || std::string(error.what()).find("hidden trusted.*") == std::string::npos) throw;
        exact_payload(source, model, false);
        unchanged(source, source_before); unchanged(independent, independent_before);
        require(fs::is_empty(root), "failed scan changed store");
        std::cout << "SKIP authoritative store/scanner publication, tampering and concurrency: " << error.what() << '\n';
        return 77;
    }
    exact_payload(store.object_path(id), model, true);
    exact_payload(source, model, false);
    unchanged(source, source_before);
    require(scan(source) == model && scan(store.object_path(id)) == model, "semantic model differs");
    // Record source inode/mode/ownership/mtime/ctime; readlink atime is excluded
    // according to the scanner's documented kernel limitation.
    struct stat before{}, after{}; require(::lstat((source / "bin/tool").c_str(), &before) == 0, "source stat");
    require(store.publish(source, id) == store.object_path(id), "idempotent republish");
    require(store.publish(independent, id) == store.object_path(id), "independent source convergence");
    require(::lstat((source / "bin/tool").c_str(), &after) == 0, "source stat after");
    require(before.st_ino == after.st_ino && before.st_mode == after.st_mode && before.st_uid == after.st_uid && before.st_gid == after.st_gid &&
            before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
            before.st_ctim.tv_sec == after.st_ctim.tv_sec && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec, "source mutated");
    store.verify(id); no_private_work(store.object_path(id).parent_path());
    const auto empty = fixture.root / "empty-source"; fs::create_directory(empty);
    const auto empty_id = derive_artifact_id({});
    exact_payload(store.publish(empty, empty_id), {}, true);
    rejects(ObjectErrorCode::SourceIdentityMismatch, [&] { store.publish(source, empty_id); });
    store.verify(empty_id);
    // Legacy namespace is opaque to publication; no package metadata is added.
    fs::create_directories(root / "legacy-1/.athena");
    std::ofstream(root / "legacy-1/.athena/metadata.toml") << "legacy sentinel";
    store.publish(source, id);
    require(read_bytes(root / "legacy-1/.athena/metadata.toml") == "legacy sentinel", "legacy changed");
    require(!fs::exists(store.object_path(id) / ".athena/manifest.toml"), "legacy manifest inserted");

    for (int scenario = 0; scenario < 2; ++scenario) {
        const auto concurrent_root = fixture.root / ("concurrent-" + std::to_string(scenario)); fs::create_directory(concurrent_root);
        ObjectStore concurrent_store(concurrent_root);
        concurrent(concurrent_store.object_path(id), model, [&](int index) {
            concurrent_store.publish(scenario == 0 || index == 0 ? source : independent, id);
            concurrent_store.verify(id);
        });
        no_private_work(concurrent_store.object_path(id).parent_path());
    }
    // Every tamper fixture is independently published, then deliberately
    // unhardened in test setup. Production never repairs an existing object.
    for (int mutation = 0; mutation < 5; ++mutation) {
        const auto corrupt_root = fixture.root / ("tamper-" + std::to_string(mutation)); fs::create_directory(corrupt_root);
        ObjectStore corrupt_store(corrupt_root); const auto object = corrupt_store.publish(source, id);
        require(::chmod(object.c_str(), 0755) == 0 && ::chmod((object / "bin").c_str(), 0755) == 0, "tamper directory setup");
        if (mutation == 0) { require(::chmod((object / "binary").c_str(), 0644) == 0, "tamper write setup"); std::ofstream(object / "binary") << "modified"; }
        if (mutation == 1) fs::remove(object / "binary");
        if (mutation == 2) std::ofstream(object / "extra") << "extra";
        if (mutation == 3) { fs::remove(object / "link"); fs::create_symlink("other", object / "link"); }
        if (mutation == 4) require(::chmod((object / "bin/tool").c_str(), 0444) == 0, "tamper executable");
        const auto changed = scan(object);
        rejects(ObjectErrorCode::CollisionCorruption, [&] { corrupt_store.verify(id); });
        rejects(ObjectErrorCode::CollisionCorruption, [&] { corrupt_store.publish(source, id); });
        require(scan(object) == changed, "conflict was modified");
        no_private_work(object.parent_path());
    }
    // Destination cannot be inspected completely; do not follow or remove it.
    const auto corrupt_root = fixture.root / "symlink-destination"; fs::create_directory(corrupt_root);
    ObjectStore corrupt_store(corrupt_root);
    fs::create_directories(corrupt_store.object_path(id).parent_path());
    fs::create_symlink(source, corrupt_store.object_path(id));
    rejects(ObjectErrorCode::ExistingObjectVerification, [&] { corrupt_store.publish(source, id); });
    require(fs::is_symlink(corrupt_store.object_path(id)), "existing symlink removed");
    no_private_work(corrupt_store.object_path(id).parent_path());
    exact_payload(source, model, false);
    unchanged(source, source_before); unchanged(independent, independent_before);
    std::cout << "authoritative publication, idempotence, tampering, cleanup and both process concurrency scenarios passed\n";
}
