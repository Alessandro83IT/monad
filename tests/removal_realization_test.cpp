// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filesystem_fixture_support.hpp"
#include "install/generation_builder.hpp"
#include "install/remover.hpp"
#include "installed/environment.hpp"
#include "store/metadata.hpp"
#include "store/manifest.hpp"
#include "object_store_test_support.hpp"
using namespace athena;
using namespace athena::core;
using namespace athena::generation;
realization::RealizationRecord record(std::string name, ArtifactId id) {
    RepositorySnapshotRef s{RepositoryId("repo"), RepositoryRevision("rev"), SchemaVersion(1),
        ContentDigest("sha256:" + std::string(64, 'a')), SnapshotId("sha256:" + std::string(64, 'b'))};
    return {{PackageId(name), PackageVersion("1"), s.repository_id, s,
        RepositoryProvenance{s.repository_id, s.revision, {}, s}}, id, {}, ArtifactOrigin::AmbientSourceBuild};
}
template<class F> std::string failure(F action) {
    try { action(); } catch (const std::exception& e) { return e.what(); }
    throw std::runtime_error("expected removal failure");
}
int main(int argc, char**) {
    StoreFixture fixture;
    const auto paths = PathLayout::from_root(fixture.root / "athena");
    fixture_fs::create_directories(paths.store);
    GenerationManager manager(paths.generation_state, GenerationManager::GenerationDirectoryTag{});
    manager.initialize();
    ObjectStore objects(paths.store);
    const auto target = paths.target_root;
    const auto a = record("a", ArtifactId("sha256:" + std::string(64, 'c')));
    const auto b = record("b", ArtifactId("sha256:" + std::string(64, 'd')));
    const auto legacy = paths.store / "legacy-1";
    fixture_fs::create_directories(legacy / "bin");
    std::ofstream(legacy / "bin/legacy") << "legacy";
    store::write_manifest(legacy, {{"bin/legacy"}});
    store::write_metadata(legacy, {"legacy", "1", "", "", "", {}});
    const Generation mixed{9, {{"legacy", legacy.string()}}, {a, b}};
    install::RemovePlan removal; removal.add("a");
    auto candidate = install::build_removal_generation(mixed, removal);
    require(candidate.entries == mixed.entries && candidate.realizations == std::vector{b},
        "removing realization preserves unrelated and legacy memberships");
    removal = {}; removal.add("legacy");
    candidate = install::build_removal_generation(mixed, removal);
    require(candidate.entries.empty() && candidate.realizations == mixed.realizations,
        "legacy removal preserves realizations");
    removal = {}; removal.add("a"); removal.add("b"); removal.add("legacy");
    candidate = install::build_removal_generation(mixed, removal);
    require(candidate.entries.empty() && candidate.realizations.empty(), "complete removal yields empty candidate");
    require(mixed.entries.size() == 1 && mixed.realizations == std::vector{a, b}, "builder leaves current immutable");
    // Pure/preflight checks run separately and never depend on scanner capability.
    const auto missing = manager.create_unprotected_for_test({}, {a});
    // Select fixture metadata without activation to exercise offline preflight.
    std::ofstream(paths.generation_state / "current") << missing.id << '\n';
    const auto count = manager.list().size();
    require(failure([&] { install::remove_packages({"unknown"}, target, paths); }).find("not installed") != std::string::npos,
        "unknown realization request rejected");
    require(failure([&] { install::remove_packages({""}, target, paths); }).find("empty name") != std::string::npos,
        "empty name rejected");
    install::remove_packages({}, target, paths);
    require(failure([&] { install::remove_packages({"a"}, target, paths); }).find("dependency state") == std::string::npos,
        "all-realization removal reaches authoritative object verification");
    require(manager.current().id == missing.id && manager.list().size() == count && !fs::exists(target),
        "failed activation preserves current and discards candidate");
    const auto pair = manager.create_unprotected_for_test({}, {a, b});
    std::ofstream(paths.generation_state / "current") << pair.id << '\n';
    require(failure([&] { install::remove_packages({"a"}, target, paths); }).find("dependency state") != std::string::npos,
        "unknown dependencies of surviving realization fail closed");
    require(manager.current().id == pair.id && manager.list().size() == count + 1,
        "dependency rejection creates no generation");
    const auto mixed_state = manager.create_unprotected_for_test(mixed.entries, {a});
    std::ofstream(paths.generation_state / "current") << mixed_state.id << '\n';
    require(failure([&] { install::remove_packages({"legacy"}, target, paths); }).find("dependency state") != std::string::npos,
        "legacy removal also rejects unknown realization dependencies");
    store::write_metadata(legacy, {"legacy", "1", "", "", "", {"a"}});
    require(failure([&] { install::remove_packages({"a"}, target, paths); }).find("Unsatisfied dependency") != std::string::npos,
        "offline legacy dependency on removed realization rejected");
    require(manager.current().id == mixed_state.id && manager.list().size() == count + 2,
        "mixed dependency failures preserve state");
    store::write_metadata(legacy, {"legacy", "1", "", "", "", {}});
    if (argc == 1) return 0;
    std::ofstream(paths.generation_state / "current") << 0 << '\n';
    NormalizedArtifact model{1, {{"bin", Directory{}}, {"bin/app", RegularFile{false, "app"}}}};
    normalize(model);
    const auto source = fixture.root / "source"; fs::create_directory(source);
    mechanics::materialize(source, model);
    const auto id = derive_artifact_id(model);
    try { objects.publish_unprotected_for_test(source, id); }
    catch (const ObjectError& e) {
        if (e.code() != ObjectErrorCode::InvalidSource || std::string(e.what()).find("hidden trusted.*") == std::string::npos) throw;
        std::cout << "SKIP removal ObjectStore integration: " << e.what() << '\n';
        return 77;
    }
    const auto object_before = snapshot(objects.object_path(id));
    auto modern_record = record("app", id);
    modern_record.semantics = installed::SemanticState{PackageMetadata{PackageId("app"), PackageVersion("1")}};
    const auto historical = manager.create_unprotected_for_test(mixed.entries, {modern_record});
    manager.switch_to(historical.id, target, objects);
    require(fs::is_symlink(target / "bin/app"), "realization activated");
    install::remove_packages({"app", "app"}, target, paths);
    const auto after = manager.current();
    auto compatible_history = historical;
    installed::capture_legacy_semantics(compatible_history);
    require(after.id != historical.id && after.realizations.empty() && after.entries == compatible_history.entries,
        "new immutable mixed generation preserves legacy");
    require(!fs::exists(target / "bin/app") && fs::is_symlink(target / "bin/legacy"), "projection removed only for app");
    unchanged(objects.object_path(id), object_before);
    require(manager.get(historical.id).realizations == historical.realizations, "history unchanged");
    manager.switch_to(historical.id, target, objects);
    require(fs::is_symlink(target / "bin/app"), "historical rollback restores realization");
    // A surviving legacy dependency on the removed realization must reject.
    store::write_metadata(legacy, {"legacy", "1", "", "", "", {"app"}});
    const auto before_failure = snapshot(target);
    const auto history_count = manager.list().size();
    require(failure([&] { install::remove_packages({"app"}, target, paths); }).find("Unsatisfied dependency") != std::string::npos,
        "legacy dependency on realization remains enforced");
    require(manager.current().id == historical.id && manager.list().size() == history_count, "legacy dependency failure preserves history");
    unchanged(target, before_failure);
    store::write_metadata(legacy, {"legacy", "1", "", "", "", {}});
    // Corrupt projection prevents removal; transaction must retain current.
    fs::remove(target / "bin/app"); std::ofstream(target / "bin/app") << "user file";
    const auto blocked = snapshot(target);
    failure([&] { install::remove_packages({"app"}, target, paths); });
    require(manager.current().id == historical.id && manager.list().size() == history_count,
        "activation failure leaves current unchanged and discards candidate");
    unchanged(target, blocked); unchanged(objects.object_path(id), object_before);
}
