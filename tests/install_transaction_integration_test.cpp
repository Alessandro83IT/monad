// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "realization_executor_support.hpp"
#include "install/transaction.hpp"
#include "installed/environment.hpp"
#include "store/metadata.hpp"
#include "store/manifest.hpp"
#include <algorithm>
#include <iostream>
using namespace execution_test;
namespace {
fs::path source(StoreFixture& f, const std::string& name, const std::string& content) {
    auto archive = archive_fixture(f, content);
    auto saved = f.root / (name + ".tar.gz");
    fs::copy_file(archive, saved);
    return saved;
}
core::ArtifactId expected(StoreFixture& f, const std::string& name, const std::string& bytes) {
    const auto root = f.root / name;
    fs::create_directories(root / "usr/share/fixture");
    std::ofstream(root / "usr/share/fixture/payload") << bytes;
    return artifact::derive_artifact_id(artifact::scan(root));
}
template<class F> void fails(F action) {
    bool failed = false;
    try { action(); } catch (const std::exception&) { failed = true; }
    require(failed, "expected transaction failure");
}
}
int main() {
    StoreFixture fixture;
    fs::create_directory(fixture.root / "probe");
    try { (void)artifact::scan(fixture.root / "probe"); }
    catch (const artifact::Error& e) {
        if (e.code() != artifact::ErrorCode::InspectionFailure ||
            std::string(e.what()).find("hidden trusted.*") == std::string::npos) throw;
        std::cout << "SKIP install transaction publication: " << e.what() << '\n'; return 77;
    }
    fs::create_directory(fixture.root / "workspace");
    fs::create_directory(fixture.root / "objects");
    store::ObjectStore objects(fixture.root / "objects");
    realization::ExecutionOptions execution{fixture.root / "workspace"};
    generation::GenerationManager generations(fixture.root / "state");
    generations.initialize();
    for (const auto& [name, directory] : std::vector<std::pair<std::string, fs::path>>{
            {"app", fixture.root / "old-app"}, {"kept-legacy", fixture.root / "kept"}}) {
        fs::create_directories(directory);
        store::write_metadata(directory, {name, "1", "", "", "", {}});
        store::write_manifest(directory, {});
    }
    auto seed = generations.create({{"app", (fixture.root / "old-app").native()},
                                    {"kept-legacy", (fixture.root / "kept").native()}});
    const auto target = fixture.root / "target";
    generations.switch_to(seed.id, target, objects);
    const auto first_source = source(fixture, "first", "one");
    auto first = plan({recipe(first_source)});
    const auto original = first.result();
    const auto id_one = expected(fixture, "expected-one", "one");
    install::install_plan(first, generations, objects, execution, target);
    auto active = generations.current();
    require(active.id != seed.id, "successful transaction did not select target");
    require(first.result() == original, "plan changed");
    require(active.entries.size() == 1 && active.entries[0].package == "kept-legacy" &&
            active.entries[0].store_path == (fixture.root / "kept").native() && active.entries[0].semantics,
            "legacy replacement/preservation and compatibility capture");
    require(active.realizations == std::vector<realization::RealizationRecord>{{first.selections()[0].identity(), id_one, std::nullopt, core::ArtifactOrigin::AmbientSourceBuild, installed::from_selection(first, first.selections()[0])}}, "realization membership mismatch");
    const auto payload = target / "usr/share/fixture/payload";
    require(fs::read_symlink(payload) == objects.object_path(id_one) / "usr/share/fixture/payload", "activation not from ObjectStore");
    require(read_bytes(payload) == "one", "activated bytes");
    auto stable = [&] (std::uint64_t id, std::size_t count) {
        require(generations.current().id == id && generations.list().size() == count, "failed transaction changed generations");
        require(fs::read_symlink(payload) == objects.object_path(id_one) / "usr/share/fixture/payload", "failed transaction changed active link");
        require(read_bytes(payload) == "one", "failed transaction changed active bytes");
        require(fs::is_empty(fixture.root / "workspace"), "workspace leaked");
    };
    const auto second_source = source(fixture, "second", "two");
    const auto id_two = expected(fixture, "expected-two", "two");
    auto later_bad = recipe(fixture.root / "missing", "zzz");
    auto failing = plan({recipe(second_source), later_bad});
    const auto failing_original = failing.result();
    auto count = generations.list().size();
    rejects(realization::ExecutionErrorCode::Acquisition, [&] {
        install::install_plan(failing, generations, objects, execution, target);
    });
    stable(active.id, count);
    require(failing.result() == failing_original, "failed plan changed");
    objects.verify(id_two); // The first selection published before the later failure.

    // Replacing a realization keeps one logical member and switches artifacts.
    auto update = plan({recipe(second_source)});
    install::install_plan(update, generations, objects, execution, target);
    auto updated = generations.current();
    require(updated.realizations.size() == 1 && updated.realizations[0].artifact_id == id_two,
            "realization replacement duplicated membership");
    require(read_bytes(payload) == "two" &&
            fs::read_symlink(payload) == objects.object_path(id_two) / "usr/share/fixture/payload",
            "replacement artifact not activated");
    install::install_plan(first, generations, objects, execution, target);

    // Two different selections share a published payload. Their membership is
    // canonicalized by Generation, independently of plan execution order.
    auto multi = plan({recipe(first_source, "zeta"), recipe(first_source, "alpha")});
    auto multi_original = multi.result();
    install::install_plan(multi, generations, objects, execution, target);
    active = generations.current();
    require(active.realizations.size() == 3, "unselected realization lost");
    require(std::is_sorted(active.realizations.begin(), active.realizations.end()), "membership not canonical");
    for (const auto& selection : multi.selections())
        require(std::any_of(active.realizations.begin(), active.realizations.end(), [&](const auto& record) {
            return record == realization::RealizationRecord{selection.identity(), id_one, std::nullopt, core::ArtifactOrigin::AmbientSourceBuild, installed::from_selection(multi, selection)};
        }), "multi-package record absent");
    require(multi.result() == multi_original, "multi plan changed");
    auto reversed_result = multi.result();
    std::reverse(reversed_result.selections.begin(), reversed_result.selections.end());
    auto reversed = core::InstallPlanBuilder(std::move(reversed_result)).build();
    install::install_plan(reversed, generations, objects, execution, target);
    require(generations.current().realizations == active.realizations,
            "membership depends on plan order");
    active = generations.current();
    count = generations.list().size();
    // Replacement has a different object at the same file path, conflicting
    // with the still-live shared object. C.3 must restore the previous state.
    auto replacement = plan({recipe(second_source)});
    fails([&] { install::install_plan(replacement, generations, objects, execution, target); });
    stable(active.id, count);
    objects.verify(id_two);

    // Existing create() validates the ID before persistence. Exercise a
    // creation failure without changing that primitive's contract.
    auto obstruction = fixture.root / "state/generations/9223372036854775807";
    fs::create_directory(obstruction);
    fails([&] { install::install_plan(first, generations, objects, execution, target); });
    fs::remove(obstruction);
    stable(active.id, count);
    install::install_plan(plan({}), generations, objects, execution, target);
    stable(active.id, count);
    require(!fs::exists(fixture.root / "objects/app-1"), "legacy store entry created");
    for (const auto& entry : fs::directory_iterator(fixture.root / "objects"))
        require(entry.path().filename() == "objects", "unexpected legacy store content");
}
