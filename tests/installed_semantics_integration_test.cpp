// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "installed_semantics_support.hpp"
#include "cli_install_support.hpp"
using namespace semantic_test;
namespace {
fs::path source(StoreFixture& f, const std::string& name, const std::string& version) {
    const auto directory = f.root / (name + version) / "project";
    fs::create_directories(directory);
    std::ofstream(directory / "CMakeLists.txt") <<
        "cmake_minimum_required(VERSION 3.20)\nproject(fixture LANGUAGES NONE)\n"
        "install(FILES payload DESTINATION share/" << name << ")\n";
    std::ofstream(directory / "payload") << name << version;
    const auto archive = f.root / (name + version + ".tar.gz");
    const auto command = "tar -czf '" + archive.native() + "' -C '" + directory.parent_path().native() + "' project";
    require(std::system(command.c_str()) == 0, "create integration source"); return archive;
}
}
int main() {
    StoreFixture fixture;
    fs::create_directory(fixture.root / "probe");
    try { (void)artifact::scan(fixture.root / "probe"); }
    catch (const artifact::Error& e) {
        if (e.code() != artifact::ErrorCode::InspectionFailure || std::string(e.what()).find("hidden trusted.*") == std::string::npos) throw;
        std::cout << "SKIP installed semantic lifecycle: " << e.what() << '\n'; return 77;
    }
    const auto paths = PathLayout::from_root(fixture.root / "athena");
    fs::create_directories(paths.store); fs::create_directories(paths.build);
    generation::GenerationManager manager(paths.generation_state, generation::GenerationManager::GenerationDirectoryTag{});
    manager.initialize(); store::ObjectStore objects(paths.store);
    realization::ExecutionOptions execution{paths.build};
    const auto a_source = source(fixture, "A", "1");
    const auto b_source = source(fixture, "B", "2");
    const auto u_source = source(fixture, "U", "1");
    auto initial = semantic_test::plan({pkg("A", "1", {"B >= 2"}), pkg("B", "2"), pkg("U")}, {}, {}, {},
        {{"A", a_source}, {"B", b_source}, {"U", u_source}});
    install::install_plan(initial, manager, objects, execution, paths.target_root);
    const auto historical = manager.current();
    require(historical.realizations.size() == 3, "initial memberships");
    installed::validate_environment(manager.get(historical.id));
    for (const auto& r : historical.realizations) {
        require(r.semantics.has_value(), "realized semantic state absent"); objects.verify(r.artifact_id);
        require(realization::deserialize_record(realization::serialize(r)) == r, "realization semantic persistence");
    }
    fs::remove(a_source); fs::remove(b_source); fs::remove(u_source);
    const auto removed = cli_test::invoke_command(paths, "remove", {"U"});
    if (removed.status != 0) throw std::runtime_error("CLI unrelated realization removal failed: " + removed.output);
    const auto partial = manager.current();
    require(partial.realizations.size() == 2 && fs::is_symlink(paths.target_root / "usr/share/A/payload") &&
        !fs::exists(paths.target_root / "usr/share/U/payload"), "partial removal projection");
    installed::validate_environment(partial); // Original sources/catalog not present.

    auto unchanged_failure = [&](auto action) {
        const auto before_current = manager.current().id;
        const auto before_state = snapshot(paths.generation_state);
        const auto before_objects = snapshot(paths.store);
        const auto before_target = snapshot(paths.target_root);
        action();
        require(manager.current().id == before_current, "semantic failure changed current");
        unchanged(paths.generation_state, before_state); unchanged(paths.store, before_objects); unchanged(paths.target_root, before_target);
    };
    unchanged_failure([&] { rejects([&] { install::remove_packages({"B"}, paths.target_root, paths); }, "B >= 2"); });
    const auto upgrade_source = source(fixture, "B", "3");
    auto upgrade = semantic_test::plan({pkg("B", "3")}, {}, {}, {}, {{"B", upgrade_source}});
    install::install_plan(upgrade, manager, objects, execution, paths.target_root);
    auto upgraded = manager.current();
    require(read_bytes(paths.target_root / "usr/share/B/payload") == "B3", "valid replacement not activated");
    installed::validate_environment(upgraded);
    auto downgrade = semantic_test::plan({pkg("B", "1")}, {}, {}, {}, {{"B", fixture.root / "missing"}});
    unchanged_failure([&] { rejects([&] { install::install_plan(downgrade, manager, objects, execution, paths.target_root); }, "B >= 2"); });
    // The CLI loads only a replacement candidate; preserved consumer semantics
    // must come from generation state rather than a mutable original catalog.
    cli_test::manifest(paths, "B", fixture.root / "missing");
    unchanged_failure([&] {
        const auto result = cli_test::invoke(paths, {"B"});
        require(result.status != 0 && result.output.find("B >= 2") != std::string::npos,
            "CLI failed to reject downgrade against preserved consumer");
    });
    fs::remove_all(paths.repository_state);
    manager.switch_to(historical.id, paths.target_root, objects);
    require(read_bytes(paths.target_root / "usr/share/B/payload") == "B2" &&
        fs::is_symlink(paths.target_root / "usr/share/U/payload"), "offline rollback failed");
    installed::validate_environment(manager.current());
    require(cli_test::invoke_command(paths, "list").status == 0 &&
            cli_test::invoke_command(paths, "info", {"A"}).status == 0,
            "list/info regressed with semantic records");
    const auto orphan_source = source(fixture, "orphan", "1");
    auto orphan_plan = semantic_test::plan({pkg("orphan")}, {}, {}, {}, {{"orphan", orphan_source}});
    auto orphan = realization::realize(orphan_plan, objects, execution).front();
    const auto collected = store::GarbageCollector(paths.store, paths.generation_state.parent_path()).collect();
    require(collected == std::vector<fs::path>{objects.object_path(orphan.artifact_id)}, "GC did not collect only orphan");
    for (const auto& g : manager.list()) for (const auto& r : g.realizations) objects.verify(r.artifact_id);
    install::remove_packages({"A", "B"}, paths.target_root, paths);
    require(manager.current().realizations.size() == 1, "batch dependent/provider removal");
    install::remove_packages({"U"}, paths.target_root, paths);
    require(manager.current().realizations.empty(), "complete removal");
    manager.switch_to(upgraded.id, paths.target_root, objects);
    require(read_bytes(paths.target_root / "usr/share/B/payload") == "B3", "new historical generation rollback");
    std::cout << "installed semantic realization/partial removal/replacement/rollback/GC lifecycle passed\n";
}
