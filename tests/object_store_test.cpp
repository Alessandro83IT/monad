#include "object_store_test_support.hpp"
int main() {
    StoreFixture fixture;
    ObjectStore store(fixture.root);
    const athena::core::ArtifactId id("sha256:" + std::string(64, 'a'));
    require(store.object_path(id) == fixture.root / "objects/v1/sha256" / std::string(64, 'a'), "exact digest mapping");
    for (const auto& text : std::vector<std::string>{"opaque", "sha256:abc", "sha256:" + std::string(64, 'A'),
                            "sha256:" + std::string(63, 'a') + "g", "sha256:" + std::string(64, 'a') + "/.."})
        rejects(ObjectErrorCode::MalformedArtifactId, [&] { store.object_path(athena::core::ArtifactId(text)); });
    rejects(ObjectErrorCode::MalformedArtifactId, [&] { store.publish(fixture.root, athena::core::ArtifactId("invalid")); });
    require(!fs::exists(fixture.root / "objects"), "invalid id created namespace");
    rejects(ObjectErrorCode::InvalidSource, [&] { store.publish(fixture.root / "missing", id); });
    rejects(ObjectErrorCode::InvalidSource, [&] { store.publish({}, id); });
    rejects(ObjectErrorCode::InvalidSource, [&] { store.publish(fs::path(std::string("a\0b", 3)), id); });
    require(!fs::exists(fixture.root / "objects"), "invalid source created namespace");
    rejects(ObjectErrorCode::InvalidSource, [&] { store.publish(fixture.root, id); });
    require(!fs::exists(fixture.root / "objects"), "source containing namespace was mutated");
    rejects(ObjectErrorCode::StoreDirectory, [&] { ObjectStore invalid("relative"); });
    // Namespace failures are testable without scanner privilege, and links
    // cannot redirect namespace inspection into an unrelated store.
    fs::create_symlink(fixture.root / "missing", fixture.root / "objects");
    rejects(ObjectErrorCode::StoreDirectory, [&] { store.verify(id); });
    require(fs::is_symlink(fixture.root / "objects"), "namespace link changed");
    fs::remove(fixture.root / "objects");

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
