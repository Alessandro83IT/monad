// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "object_store_test_support.hpp"
#include <sys/syscall.h>
#include "store/gc.hpp"
#include "generation/generation.hpp"
int main() {
    StoreFixture fixture;
    ObjectStore store(fixture.root);
    const athena::core::ArtifactId id("sha256:" + std::string(64, 'a'));
    require(store.object_path(id) == fixture.root / "objects/v1/sha256" / std::string(64, 'a'), "exact digest mapping");
    for (const auto& text : std::vector<std::string>{"opaque", "sha256:abc", "sha256:" + std::string(64, 'A'),
                            "sha256:" + std::string(63, 'a') + "g", "sha256:" + std::string(64, 'a') + "/.."})
        rejects(ObjectErrorCode::MalformedArtifactId, [&] { store.object_path(athena::core::ArtifactId(text)); });
    rejects(ObjectErrorCode::MalformedArtifactId, [&] { store.publish_unprotected_for_test(fixture.root, athena::core::ArtifactId("invalid")); });
    require(!fs::exists(fixture.root / "objects"), "invalid id created namespace");
    rejects(ObjectErrorCode::InvalidSource, [&] { store.publish_unprotected_for_test(fixture.root / "missing", id); });
    rejects(ObjectErrorCode::InvalidSource, [&] { store.publish_unprotected_for_test({}, id); });
    rejects(ObjectErrorCode::InvalidSource, [&] { store.publish_unprotected_for_test(fs::path(std::string("a\0b", 3)), id); });
    require(!fs::exists(fixture.root / "objects"), "invalid source created namespace");
    rejects(ObjectErrorCode::InvalidSource, [&] { store.publish_unprotected_for_test(fixture.root, id); });
    require(!fs::exists(fixture.root / "objects"), "source containing namespace was mutated");
    rejects(ObjectErrorCode::StoreDirectory, [&] { ObjectStore invalid("relative"); });
    // Namespace failures are testable without scanner privilege, and links
    // cannot redirect namespace inspection into an unrelated store.
    fs::create_symlink(fixture.root / "missing", fixture.root / "objects");
    rejects(ObjectErrorCode::StoreDirectory, [&] { store.verify(id); });
    require(fs::is_symlink(fixture.root / "objects"), "namespace link changed");
    fs::remove(fixture.root / "objects");

    require(store.list().empty(), "absent namespace");
    fs::create_symlink(fixture.root / "missing", fixture.root / "objects");
    rejects(ObjectErrorCode::StoreDirectory, [&] { store.list(); });
    fs::remove(fixture.root / "objects");
    const auto ns = store.object_path(id).parent_path();
    fs::create_directories(ns);
    const athena::core::ArtifactId second("sha256:" + std::string(64, 'b'));
    fs::create_directories(store.object_path(second));
    fs::create_directories(store.object_path(id) / "nested");
    std::ofstream(store.object_path(id) / "nested/file") << "payload";
    fs::create_directory_symlink(fixture.root, store.object_path(id) / "external");
    fs::permissions(store.object_path(id) / "nested", fs::perms::owner_read | fs::perms::owner_exec);
    fs::permissions(store.object_path(id), fs::perms::owner_read | fs::perms::owner_exec);
    fs::create_directory(ns / ".publish-keep");
    fs::create_directory(ns / std::string(64, 'A'));
    std::ofstream(ns / std::string(64, 'c')) << "unrelated";
    const athena::core::ArtifactId link("sha256:" + std::string(64, 'd'));
    fs::create_directory_symlink(fixture.root, store.object_path(link));
    require(store.list() == std::vector<athena::core::ArtifactId>{id, second}, "canonical sorted directories only");
    athena::generation::GenerationManager manager(fixture.root / "state"); manager.initialize();
    athena::store::GarbageCollector gc(fixture.root, fixture.root / "state");
    bool permission_rejected = false;
    try { gc.collect(); } catch (const athena::core::detail::PersistenceError& e) {
        require(!e.outcome_uncertain && e.error == EACCES, "readonly detach classification"); permission_rejected = true;
    }
    if (permission_rejected) {
        require(fs::exists(store.object_path(id) / "nested/file"), "failed detach modified authority");
        // Fixture-only permission change: production never chmods authority.
        fs::permissions(store.object_path(id), fs::perms::owner_all);
    }
    require(gc.collect().size() == 2 && gc.collect().empty(), "recoverable removal/idempotence");
    require(fs::is_symlink(store.object_path(link)) && fs::exists(ns / ".publish-keep") && fs::exists(ns / std::string(64, 'c')), "ownership boundary");

    // Same-device bind mounts differ by mount ID. Missing identity must
    // fail closed even if both records would otherwise compare equal.
    struct statx expected_mount {};
    expected_mount.stx_mask = STATX_MNT_ID;
    expected_mount.stx_mnt_id = 100;
    expected_mount.stx_dev_major = 8;
    expected_mount.stx_dev_minor = 1;
    auto actual_mount = expected_mount;
    mechanics::require_same_gc_mount(expected_mount, actual_mount);
    actual_mount.stx_mnt_id = 101;
    rejects(ObjectErrorCode::Cleanup, [&] { mechanics::require_same_gc_mount(expected_mount, actual_mount); });
    actual_mount = expected_mount;
    actual_mount.stx_mask = 0;
    rejects(ObjectErrorCode::Cleanup, [&] { mechanics::require_same_gc_mount(expected_mount, actual_mount); });
    rejects(ObjectErrorCode::Cleanup, [&] { mechanics::require_same_gc_mount(actual_mount, expected_mount); });
    actual_mount = expected_mount;
    ++actual_mount.stx_dev_minor;
    rejects(ObjectErrorCode::Cleanup, [&] { mechanics::require_same_gc_mount(expected_mount, actual_mount); });
    // Real statx mount IDs can be inspected without mounting anything.
    const int fixture_fd = ::open(ns.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    const int proc_fd = ::open("/proc", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    require(fixture_fd >= 0 && proc_fd >= 0, "open mount identity fixtures");
    struct statx fixture_mount {}, proc_mount {};
    require(::syscall(SYS_statx, fixture_fd, "", AT_EMPTY_PATH, STATX_MNT_ID, &fixture_mount) == 0 &&
            ::syscall(SYS_statx, proc_fd, "", AT_EMPTY_PATH, STATX_MNT_ID, &proc_mount) == 0,
            "read real mount identities");
    ::close(fixture_fd); ::close(proc_fd);
    rejects(ObjectErrorCode::Cleanup, [&] { mechanics::require_same_gc_mount(fixture_mount, proc_mount); });
    // Actual bind-mount deletion integration needs CAP_SYS_ADMIN; these
    // checks exercise the production mount decision without that privilege.

    const auto model = payload_model();
    const auto root = fixture.root / "private"; fs::create_directory(root);
    mechanics::materialize(root, model); exact_payload(root, model, false);
    mechanics::finalize(root, model); exact_payload(root, model, true);
    const auto empty = fixture.root / "empty"; fs::create_directory(empty);
    mechanics::materialize(empty, {}); mechanics::finalize(empty, {}); exact_payload(empty, {}, true);

    // Invalid model/traversal cannot write outside a private root.
    const auto defensive = fixture.root / "defensive"; fs::create_directory(defensive);
    for (const auto& path : {"../escaped", "/escaped", "a//b"}) {
        try { mechanics::materialize(defensive, {1, {{path, RegularFile{false, "bad"}}}}); throw std::runtime_error("accepted traversal"); }
        catch (const Error&) {}
    }
    require(fs::is_empty(defensive), "invalid model partially materialized");
    fs::create_symlink(fixture.root, defensive / "bin");
    rejects(ObjectErrorCode::Materialization, [&] { mechanics::materialize(defensive, model); });
    require(!fs::exists(fixture.root / "tool"), "followed materialization link");

    // The production cleanup helper restores hardened directories but never
    // follows literal links to external content or touches other private work.
    const auto abandoned = fixture.root / ".publish-owned";
    const auto other_work = fixture.root / ".publish-other";
    fs::create_directories(abandoned / "payload"); fs::create_directory(other_work);
    mechanics::materialize(abandoned / "payload", model);
    std::ofstream(fixture.root / "sentinel") << "keep";
    fs::create_symlink(fixture.root / "sentinel", abandoned / "payload/external");
    mechanics::finalize(abandoned / "payload", model);
    require(mechanics::cleanup_private(abandoned).empty(), "hardened cleanup failed");
    require(!fs::exists(abandoned) && fs::exists(other_work), "cleanup ownership boundary");
    require(read_bytes(fixture.root / "sentinel") == "keep", "cleanup followed link");
    const auto failed = fixture.root / ".publish-failed"; fs::create_directories(failed / "payload");
    const NormalizedArtifact cannot_materialize{1, {
        {"a", RegularFile{false, "partial private work"}},
        {std::string(300, 'z'), RegularFile{false, "exceeds Linux component limit"}}
    }};
    rejects(ObjectErrorCode::Materialization, [&] { mechanics::materialize(failed / "payload", cannot_materialize); });
    require(fs::exists(failed / "payload/a"), "failure fixture did not create partial work");
    require(mechanics::cleanup_private(failed).empty() && !fs::exists(failed), "partial failure cleanup");

    // Real Linux primitive: no-replace works for both directories and symlinks.
    const int directory = ::open(fixture.root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    require(directory >= 0, "open test directory");
    require(mechanics::rename_no_replace(directory, "private", directory, "final"), "first rename");
    require(!mechanics::rename_no_replace(directory, "empty", directory, "final"), "overwrote object");
    exact_payload(fixture.root / "final", model, true);
    fs::create_symlink("nonexistent", fixture.root / "conflict");
    require(!mechanics::rename_no_replace(directory, "empty", directory, "conflict"), "overwrote symlink");
    rejects(ObjectErrorCode::Publication, [&] { mechanics::rename_no_replace(directory, "missing", directory, "new"); });
    ::close(directory);
    // Process-level mechanics run without privileged scanner success. This is
    // evidence for visibility/no-replace only, never a substitute for scanning.
    concurrent(fixture.root / "concurrent", model, [&](int index) {
        const auto private_root = fixture.root / ("child-" + std::to_string(index));
        fs::create_directory(private_root); mechanics::materialize(private_root, model); mechanics::finalize(private_root, model);
        int fd = ::open(fixture.root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        require(fd >= 0, "child directory");
        mechanics::rename_no_replace(fd, private_root.filename().c_str(), fd, "concurrent"); ::close(fd);
        exact_payload(fixture.root / "concurrent", model, true);
    });
    std::cout << "object store path/materialization/no-replace/process visibility mechanics passed\n";
}
