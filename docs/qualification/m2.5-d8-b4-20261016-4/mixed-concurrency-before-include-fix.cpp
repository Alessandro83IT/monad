// SPDX-License-Identifier: GPL-3.0-or-later
// Supplemental qualification consumer of the SAME frozen B4 fixture/scanner.
#define main b4_embedded_unit_main
#include "package_operation_test.cpp"
#undef main
namespace {
void gate(int fd) { char byte; require(::read(fd, &byte, 1) == 1, "mixed worker gate"); ::close(fd); }
int mixed_worker(int argc, char** argv) {
    if (argc == 4 && std::string_view(argv[1]) == "gc") {
        gate(std::stoi(argv[3]));
        const fs::path root(argv[2]);
        store::GarbageCollector collector(store::AuthorityContext(root / "store", root / "generations"));
        require(collector.collect().empty(), "GC must retain all B4/D4 closures"); return 0;
    }
    require(argc == 8, "mixed worker arguments"); gate(std::stoi(argv[7]));
    const fs::path root(argv[3]);
    ProjectionBinding binding{d1_test::projection_id, (root / "generations").native(), (root / "store").native(), (root / "projection").native()};
    store::ObjectStore objects(root / "store"); ProjectionState state(binding.generation_authority);
    auto p = ProjectionContext::open(binding, objects); auto archive = OperationalRequestArchive::open(root / "requests", binding);
    RequestArchiveReference request{{ActivationId(argv[4]), PlanDigest(argv[5])}, RequestEnvelopeDigest(argv[6])};
    if (std::string_view(argv[2]) == "rollback")
        activate_generation_operation(p, state, archive, {GenerationOperationKind::Rollback, request});
    else activate_package_operation(p, state, archive,
        {std::string_view(argv[2]) == "remove" ? PackageOperationKind::Remove : PackageOperationKind::Install,
         {GenerationOperationKind::Switch, request}});
    return 0;
}
void zero_link_roundtrip() {
    PackageFixture f; auto in = request({package("meta")}, "meta"); const auto plan = core::plan_install(in);
    const artifact::NormalizedArtifact tree; const auto artifact_id = artifact::derive_artifact_id(tree);
    const auto object = f.objects.object_path(artifact_id); fs::create_directories(object);
    store::object_detail::finalize(object, tree);
    std::vector<realization::RealizationRecord> records{{plan.selections().front().identity(), artifact_id, {},
        core::ArtifactOrigin::AmbientSourceBuild, installed::from_selection(plan, plan.selections().front())}};
    f.put(build_install_operation_target(f.generation(0), 1, in, records)); auto op = f.prepare_install(in);
    const auto& target = op.generation().request().input().target;
    require(target.sources.size() == 1 && target.sources.front().links.empty(), "zero-link membership retained explicitly");
    auto p = f.open(); auto archive = f.archive(); persist_package_operation(p, *f.state, archive, op);
    activate_package_operation(p, *f.state, archive, op.reference());
    f.put(build_remove_operation_target(f.generation(1), 2, {"meta"}));
    auto remove = prepare_remove_operation(p, *f.state, f.input(2, 21), {"meta"});
    persist_package_operation(p, *f.state, archive, remove); activate_package_operation(p, *f.state, archive, remove.reference());
    require(f.current().generation == 2 && fs::exists(object), "zero-link removal never deletes source object");
    std::cout << "PASS complete zero-link install/remove roundtrip; scanner=SIMULATED\n";
}
void remove_preserves_dependency() {
    PackageFixture f; auto in = request({package("app", {"dep"}), package("dep")}); auto records = f.records(in);
    f.put(build_install_operation_target(f.generation(0), 1, in, records)); auto op = f.prepare_install(in);
    auto p = f.open(); auto archive = f.archive(); persist_package_operation(p, *f.state, archive, op);
    activate_package_operation(p, *f.state, archive, op.reference());
    f.put(build_remove_operation_target(f.generation(1), 2, {"app"}));
    auto remove = prepare_remove_operation(p, *f.state, f.input(2, 21), {"app"});
    const auto plan = build_operational_plan(remove.generation().request().input());
    require(std::any_of(plan.plan.operations.begin(), plan.plan.operations.end(), [](const auto& value) {
        return value.path == "share/dep" && std::holds_alternative<KeepLink>(value.state);
    }), "remaining dependency has an explicit KEEP operation");
    persist_package_operation(p, *f.state, archive, remove); activate_package_operation(p, *f.state, archive, remove.reference());
    require(f.current().generation == 2 && fs::is_symlink(f.root / "projection/share/dep") &&
        !fs::exists(f.root / "projection/share/app"), "remove preserves dependency projection through D7");
    std::cout << "PASS remove preserves dependency and KEEP witness; scanner=SIMULATED\n";
}
void mixed() {
    PackageFixture f; auto initial = request({package("app")}); auto first_records = f.records(initial);
    f.put(build_install_operation_target(f.generation(0), 1, initial, first_records));
    auto first = f.prepare_install(initial); auto p = f.open(); auto archive = f.archive();
    persist_package_operation(p, *f.state, archive, first); activate_package_operation(p, *f.state, archive, first.reference());
    auto adding = request({package("extra")}, "extra"); auto extra_records = f.records(adding);
    f.put(build_install_operation_target(f.generation(1), 2, adding, extra_records));
    fs::create_directory(f.root / "generations/3"); fs::permissions(f.root / "generations/3", fs::perms::owner_all);
    f.put(build_remove_operation_target(f.generation(1), 3, {"app"}));
    fs::permissions(f.root / "generations/3/generation.toml", fs::perms::owner_read | fs::perms::owner_write);
    auto installing = f.prepare_install(adding, 2, 22);
    auto removing = prepare_remove_operation(p, *f.state, f.input(3, 23), {"app"});
    auto rollback = prepare_generation_operation(p, *f.state, GenerationOperationKind::Rollback, f.input(0, 24).generation);
    persist_package_operation(p, *f.state, archive, installing); persist_package_operation(p, *f.state, archive, removing);
    persist_generation_operation(p, *f.state, archive, rollback);
    std::vector<RequestArchiveReference> references{installing.reference().generation.request, removing.reference().generation.request, rollback.reference().request};
    const std::vector<std::string> kinds{"install", "remove", "rollback"};
    f.state.reset(); int gates[2]; require(::pipe(gates) == 0, "mixed pipe");
    std::vector<pid_t> children;
    for (unsigned i = 0; i < 4; ++i) {
        const auto child = ::fork(); require(child >= 0, "mixed fork"); children.push_back(child);
        if (!child) {
            ::close(gates[1]); const auto fd = std::to_string(gates[0]);
            if (i == 3) ::execl("/proc/self/exe", "b4-mixed", "gc", f.root.c_str(), fd.c_str(), nullptr);
            else {
                const auto& r = references[i];
                ::execl("/proc/self/exe", "b4-mixed", "worker", kinds[i].c_str(), f.root.c_str(),
                    r.plan.activation_id.value().c_str(), r.plan.digest.value().c_str(), r.envelope.value().c_str(), fd.c_str(), nullptr);
            }
            ::_exit(99);
        }
    }
    ::close(gates[0]); require(::write(gates[1], "xxxx", 4) == 4, "release all competitors"); ::close(gates[1]);
    unsigned winners = 0, stale = 0;
    for (unsigned i = 0; i < children.size(); ++i) {
        int status; require(::waitpid(children[i], &status, 0) == children[i] && WIFEXITED(status), "mixed wait");
        if (i == 3) require(WEXITSTATUS(status) == 0, "concurrent GC succeeds");
        else { winners += WEXITSTATUS(status) == 0; stale += WEXITSTATUS(status) == 77; }
    }
    require(winners == 1 && stale == 2, "one winner across install/remove/rollback, no implicit rebase");
    f.state = std::make_unique<ProjectionState>(f.binding.generation_authority);
    const auto selected = f.current(); require(selected.activation_id == id(22) || selected.activation_id == id(23) || selected.activation_id == id(24), "winner identity");
    require(f.plan(22) && f.plan(23), "both B4 roots survive contention independent of winner");
    for (const auto& records : {first_records, extra_records}) for (const auto& record : records)
        require(fs::exists(f.objects.object_path(record.artifact_id)), "GC cannot collect a pending B4 closure");
    std::cout << "PASS mixed install/remove/rollback/GC; winner=" << selected.activation_id.value() << "; scanner=SIMULATED\n";
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1) return mixed_worker(argc, argv);
        zero_link_roundtrip(); remove_preserves_dependency(); mixed(); return 0;
    } catch (const RequestArchiveError& e) {
        try { std::rethrow_exception(e.underlying); }
        catch (const ExecutionError& cause) { if (cause.cause == ExecutionCause::SelectionConflict) return 77; }
        catch (...) {} std::cerr << "mixed archive failure\n"; return 98;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
