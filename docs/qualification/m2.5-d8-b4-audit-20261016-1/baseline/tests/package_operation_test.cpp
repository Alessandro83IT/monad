// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation/package_operation.hpp"
#include "activation/operational_request_codec.hpp"
#include "activation_execution_support.hpp"
#include "store/object_store_detail.hpp"
#include "artifact/identity.hpp"
#include "artifact/scanner.hpp"
#include "store/gc.hpp"
#include <sys/wait.h>
#include <iostream>
#include <set>
using namespace athena;
using namespace athena::activation;
using namespace execution_test;
namespace fs = std::filesystem;
// SIMULATED scanner provider, confined to this executable by ELF interposition.
// Reads real payload bytes/types; does not certify trusted xattrs, ownership,
// hardlink policy or hostile-writer exclusion. Production scanner is unchanged.
#ifndef ATHENA_B4_REAL_SCANNER
namespace athena::artifact {
NormalizedArtifact scan(const fs::path& root) {
    if (!fs::is_directory(fs::symlink_status(root))) throw Error(ErrorCode::InvalidRoot, "", "test scan root");
    NormalizedArtifact result;
    for (const auto& item : fs::recursive_directory_iterator(root)) {
        const auto path = item.path().lexically_relative(root).native();
        const auto status = item.symlink_status();
        if (fs::is_directory(status)) result.entries.push_back({path, Directory{}});
        else if (fs::is_symlink(status)) result.entries.push_back({path, SymbolicLink{fs::read_symlink(item.path()).native()}});
        else if (fs::is_regular_file(status)) {
            std::ifstream stream(item.path(), std::ios::binary);
            const std::string bytes{std::istreambuf_iterator<char>(stream), {}};
            if (!stream || stream.bad()) throw Error(ErrorCode::ReadFailure, path, "test scan read");
            result.entries.push_back({path, RegularFile{(status.permissions() & fs::perms::owner_exec) != fs::perms::none, bytes}});
        } else throw Error(ErrorCode::UnsupportedFileType, path, "test scan type");
    }
    normalize(result); return result;
}
}
#endif
namespace {
std::string read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary); require(bool(stream), "read record");
    return {std::istreambuf_iterator<char>(stream), {}};
}
core::PackageMetadata package(std::string name, std::vector<std::string> dependencies = {}) {
    core::PackageMetadata value{core::PackageId(name), core::PackageVersion("1")};
    value.dependencies = std::move(dependencies); return value;
}
core::InstallRequest request(std::vector<core::PackageMetadata> packages, std::string root = "app") {
    const core::RepositoryId repo("b4"); std::vector<core::RepositoryCandidate> candidates;
    for (auto& p : packages) candidates.push_back({p, {repo, core::RepositoryRevision("one"), core::MetadataReference(p.package_id.value())}});
    core::InstallRequest result;
    result.snapshots.push_back(core::RepositorySnapshot::create(repo, core::RepositoryRevision("one"), candidates));
    result.repositories[repo] = {};
    result.targets = {{{core::SelectorKind::Package, root, {}}}};
    result.target = {"linux", "x86_64", "gnu", ""}; result.environments = {result.target, result.target};
    result.active_roles = {core::DependencyRole::Runtime}; return result;
}
struct PackageFixture : Fixture {
    fs::path archive_path = root / "requests";
    PackageFixture() {
        bootstrap_source_authority();
        fs::create_directory(archive_path); fs::permissions(archive_path, fs::perms::owner_all);
        auto dir = core::detail::open_directory(archive_path);
        auto parent = core::detail::open_directory(root);
        core::detail::directory_barrier(dir.get()); core::detail::directory_barrier(parent.get());
    }
    ~PackageFixture() {
        for (const auto& entry : fs::recursive_directory_iterator(root))
            if (fs::is_directory(entry.symlink_status())) ::chmod(entry.path().c_str(), 0700);
    }
    CurrentV1 current() {
        auto p = open(); return std::get<CurrentV1>(*inspect_selection(p, *state));
    }
    auto archive(RequestArchiveIo io = {}) { return OperationalRequestArchive::open(archive_path, binding, std::move(io)); }
    generation::Generation generation(unsigned n) {
        return generation::read_generation_document(binding.generation_authority, n).generation;
    }
    void put(const generation::Generation& value, bool decorated = false) {
        // Existing producer seam: exact complete generation metadata supplied to
        // B4, never advertised as production pinned publication qualification.
        auto text = generation::serialize(value);
        if (decorated) text = "# exact B4 document\r\n" + text;
        write(root / "generations" / std::to_string(value.id) / "generation.toml", text);
    }
    std::vector<realization::RealizationRecord> records(const core::InstallRequest& in) {
        const auto plan = core::plan_install(in); std::vector<realization::RealizationRecord> result;
        for (const auto& selected : plan.selections()) {
            const auto name = selected.candidate.package.package_id.value();
            artifact::NormalizedArtifact tree{1, {{"share", artifact::Directory{}},
                {"share/" + name, artifact::RegularFile{false, "payload for " + name}}}};
            artifact::normalize(tree); auto id = artifact::derive_artifact_id(tree);
            const auto path = objects.object_path(id);
            if (!fs::exists(path)) {
                fs::create_directories(path);
                store::object_detail::materialize(path, tree); store::object_detail::finalize(path, tree);
            }
            result.push_back({selected.identity(), id, {}, core::ArtifactOrigin::AmbientSourceBuild,
                installed::from_selection(plan, selected)});
        }
        return result;
    }
    std::vector<OperationalSourceProjection> inventory(unsigned n) {
        std::vector<OperationalSourceProjection> result;
        for (const auto& record : generation(n).realizations) {
            OperationalSourceProjection source{ArtifactSource{record.artifact_id}, {}, {}};
            for (const auto& entry : artifact::scan(objects.object_path(record.artifact_id)).entries) {
                if (std::holds_alternative<artifact::Directory>(entry.value)) source.directories.push_back(entry.path);
                else source.links.push_back(entry.path);
            }
            result.push_back(std::move(source));
        }
        return result;
    }
    std::vector<OperationalWitness> witnesses(const CurrentV1& selected) {
        auto p = open(); const auto plan = inspect_activation_plan(p, *state, selected.activation_id);
        std::vector<OperationalWitness> result;
        for (const auto& operation : plan.plan.operations) std::visit([&](const auto& value) {
            if constexpr (requires { value.new_witness; }) result.push_back({operation.path, value.new_witness});
        }, operation.state);
        return result;
    }
    PackageOperationInput input(unsigned target = 1, unsigned activation = 20) {
        const auto selected = current();
        return {{id(activation), binding, {selected, digest(selected.generation), ActivationOwnership{selected.activation_id}},
            target, inventory(selected.generation), inventory(target), witnesses(selected)}};
    }
    auto prepare_install(const core::InstallRequest& in, unsigned target = 1, unsigned activation = 20) {
        auto p = open(); return prepare_install_operation(p, *state, input(target, activation), in);
    }
    fs::path record(unsigned activation = 20) { return archive_path / (id(activation).value() + ".toml"); }
    bool plan(unsigned activation = 20) { return fs::exists(root / "projection/.athena-activation/plans" / id(activation).value()); }
};
void roundtrip(bool dependencies) {
    PackageFixture f;
    auto in = dependencies ? request({package("app", {"dep >= 1"}), package("dep")}) : request({package("app")});
    const auto records = f.records(in);
    const auto target = build_install_operation_target(f.generation(0), 1, in, records);
    f.put(target, true);
    const auto exact = read(f.root / "generations/1/generation.toml");
    const auto original = f.current(); auto raw = f.input(); auto p = f.open(); auto archive = f.archive();
    auto op = prepare_install_operation(p, *f.state, raw, in);
    require(f.current() == original && f.plan() && fs::is_empty(f.archive_path), "prepare retains D4 but does not select/archive");
    require(op.generation().request().input().target.document == exact, "exact target bytes retained");
    auto expected = op.reference(); const auto frozen = serialize_operational_request(op.generation().request());
    raw.generation.target_sources.clear(); in.targets.clear();
    require(serialize_operational_request(op.generation().request()) == frozen, "detached request");
    require(persist_package_operation(p, *f.state, archive, op) == RequestPublication::Published, "B2 publication");
    require(persist_package_operation(p, *f.state, archive, op) == RequestPublication::AlreadyPublished, "B2 retry");
    require(read(f.record()) == frozen && f.current() == original, "no premature current mutation");
    f.state.reset();
    generation::GenerationManager manager(f.root / "generations", generation::GenerationManager::GenerationDirectoryTag{});
    bool retained = false;
    try { manager.discard(1); } catch (const std::runtime_error& e) { retained = std::string(e.what()).find("retained by an activation plan") != std::string::npos; }
    require(retained, "D4 protects target before first activation and across State release");
    require(store::GarbageCollector(store::AuthorityContext(f.binding.store_authority, f.binding.generation_authority)).collect().empty(), "GC retains complete closure");
    f.state = std::make_unique<ProjectionState>(f.binding.generation_authority);
    auto recovered = recover_package_operation(p, *f.state, archive, expected);
    require(f.current() == original && serialize_operational_request(recovered.generation().request()) == frozen, "recovery never executes");
    activate_package_operation(p, *f.state, archive, expected);
    require(f.current().generation == 1 && fs::is_symlink(f.root / "projection/share/app"), "install through fresh D6/D7");
    require(activate_package_operation(p, *f.state, archive, expected).publication == PlanPublication::AlreadyPublished, "exact selected retry");
    if (dependencies) {
        rejected([&] { build_remove_operation_target(f.generation(1), 2, {"dep"}); });
        require(fs::exists(f.root / "projection/share/dep"), "reverse dependency refusal leaves projection");
    }
    const std::vector<std::string> names = dependencies ? std::vector<std::string>{"app", "dep"} : std::vector<std::string>{"app"};
    f.put(build_remove_operation_target(f.generation(1), 2, names));
    auto removal = prepare_remove_operation(p, *f.state, f.input(2, 21), names);
    require(f.current().generation == 1, "remove preparation cannot change current");
    persist_package_operation(p, *f.state, archive, removal);
    activate_package_operation(p, *f.state, archive, removal.reference());
    require(f.current().generation == 2 && !fs::exists(f.root / "projection/share/app"), "remove generation only");
    for (const auto& r : records) require(fs::exists(f.objects.object_path(r.artifact_id)), "remove never deletes store");
    rejected([&] { activate_package_operation(p, *f.state, archive, expected); });
    require(f.current().activation_id == id(21), "old install replay cannot rebase");
}
void negatives() {
    PackageFixture f; auto in = request({package("app", {"dep"}), package("dep")});
    auto records = f.records(in); auto base = f.generation(0);
    rejected([&] { build_install_operation_target(base, 1, in, {records.front()}); });
    auto wrong = records; wrong.front().semantics.reset();
    rejected([&] { build_install_operation_target(base, 1, in, wrong); });
    wrong = records; wrong.back() = wrong.front();
    rejected([&] { build_install_operation_target(base, 1, in, wrong); });
    rejected([&] { build_install_operation_target(base, 0, in, records); });
    rejected([&] { build_install_operation_target(base, 1, request({package("app", {"missing"})}), records); });
    rejected([&] { build_remove_operation_target(base, 1, {"missing"}); });
    auto conflict = package("app"); conflict.conflicts = {"dep"};
    auto conflict_request = request({conflict, package("dep")});
    conflict_request.targets.push_back({{core::SelectorKind::Package, "dep", {}}});
    rejected([&] { core::plan_install(conflict_request); });
    f.put(build_install_operation_target(base, 1, in, records)); auto raw = f.input(); auto p = f.open();
    auto incomplete = raw; incomplete.generation.target_sources.pop_back();
    rejected([&] { prepare_install_operation(p, *f.state, incomplete, in); });
    incomplete = raw; incomplete.generation.target_sources.front().links.clear();
    rejected([&] { prepare_install_operation(p, *f.state, incomplete, in); });
    incomplete = raw; incomplete.generation.binding.projection_root = "/unrelated";
    rejected([&] { prepare_install_operation(p, *f.state, incomplete, in); });
    incomplete = raw; incomplete.generation.base.current = CurrentV1{0, id(99), PlanDigest(d1_test::digest('f'))};
    rejected([&] { prepare_install_operation(p, *f.state, incomplete, in); });
    incomplete = raw; incomplete.generation.target_sources.front().links = {"../escape"};
    rejected([&] { prepare_install_operation(p, *f.state, incomplete, in); });
    rejected([&] { prepare_install_operation(p, *f.state, raw, in, PlanDigest(d1_test::digest('f'))); });
    const auto document = f.root / "generations/1/generation.toml";
    fs::rename(document, f.root / "saved-target"); fs::create_symlink(f.root / "saved-target", document);
    rejected([&] { prepare_install_operation(p, *f.state, raw, in); });
    fs::remove(document); fs::rename(f.root / "saved-target", document);
    const auto object = f.objects.object_path(records.front().artifact_id);
    const auto object_permissions = fs::status(object).permissions();
    fs::permissions(object, fs::perms::owner_all); // fixture-only move of immutable directory
    fs::rename(object, f.root / "saved-object");
    rejected([&] { prepare_install_operation(p, *f.state, raw, in); });
    fs::rename(f.root / "saved-object", object);
    fs::permissions(object, object_permissions);
    require(!f.plan() && f.current().generation == 0, "all invalid prepares precede D4 and selection");
    const auto path = f.objects.object_path(records.front().artifact_id) / "share" / records.front().candidate.package.value();
    fs::permissions(path, fs::perms::owner_write, fs::perm_options::add); write(path, "corrupt");
    rejected([&] { prepare_install_operation(p, *f.state, raw, in); });
    require(!f.plan() && f.current().generation == 0, "corrupt payload refused before retention");
}
void tamper() {
    PackageFixture f; auto in = request({package("app")}); auto records = f.records(in);
    f.put(build_install_operation_target(f.generation(0), 1, in, records)); auto op = f.prepare_install(in);
    auto p = f.open(); auto archive = f.archive(); persist_package_operation(p, *f.state, archive, op);
    const auto original = read(f.record());
    write(f.record(), original + "# corrupt canonical envelope\n");
    rejected([&] { activate_package_operation(p, *f.state, archive, op.reference()); });
    write(f.record(), original);
    auto reference = op.reference(); reference.generation.request.envelope = RequestEnvelopeDigest(d1_test::digest('f'));
    rejected([&] { recover_package_operation(p, *f.state, archive, reference); });
    reference = op.reference(); reference.kind = static_cast<PackageOperationKind>(99);
    rejected([&] { recover_package_operation(p, *f.state, archive, reference); });
    const auto retention = f.root / "projection/.athena-activation/plans" / id(20).value();
    fs::rename(retention, f.root / "saved-retention");
    rejected([&] { recover_package_operation(p, *f.state, archive, op.reference()); });
    rejected([&] { activate_package_operation(p, *f.state, archive, op.reference()); });
    require(!f.plan() && f.current().generation == 0 && read(f.record()) == original, "missing D4 root is never rebuilt from B2");
    fs::rename(f.root / "saved-retention", retention);
    const auto document = f.root / "generations/1/generation.toml";
    const auto exact = read(document); write(document, exact + "# changed\n");
    rejected([&] { activate_package_operation(p, *f.state, archive, op.reference()); });
    write(document, exact);
    const auto payload = f.objects.object_path(records.front().artifact_id) / "share/app";
    fs::permissions(payload, fs::perms::owner_write, fs::perm_options::add); write(payload, "changed after prepare");
    rejected([&] { activate_package_operation(p, *f.state, archive, op.reference()); });
    require(f.current().generation == 0, "fresh D6 refuses changed payload after preparation");
    write(payload, "payload for app"); fs::permissions(payload, fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read);
    auto prepared_again = f.prepare_install(in);
    require(prepared_again.reference().generation.request.envelope == op.reference().generation.request.envelope, "exact reprepare frozen identity");
    const auto record = f.record(); fs::remove(record); fs::create_symlink(document, record);
    rejected([&] { activate_package_operation(p, *f.state, archive, op.reference()); });
    fs::remove(record); write(record, original); fs::permissions(record, fs::perms::owner_read | fs::perms::owner_write);
    activate_package_operation(p, *f.state, archive, op.reference());
}
PreparedPackageOperation operation(PackageFixture& f, bool removal) {
    auto in = request({package("app")}); auto records = f.records(in);
    f.put(build_install_operation_target(f.generation(0), 1, in, records));
    auto op = f.prepare_install(in);
    if (!removal) return op;
    auto p = f.open(); auto archive = f.archive();
    persist_package_operation(p, *f.state, archive, op);
    activate_package_operation(p, *f.state, archive, op.reference());
    f.put(build_remove_operation_target(f.generation(1), 2, {"app"}));
    return prepare_remove_operation(p, *f.state, f.input(2, 21), {"app"});
}
void semantic_variants() {
    PackageFixture f;
    auto provider = package("provider"); provider.capabilities = {"api"}; provider.features = {"gui"};
    auto in = request({provider}, "provider");
    in.targets.push_back({{core::SelectorKind::Capability, "api", {core::VersionConstraintKind::Comparison,
        core::VersionComparisonOperator::GreaterEqual, "1", {}}}});
    in.targets.push_back({{core::SelectorKind::Feature, "gui", {}}});
    in.feature_states["gui"] = true; in.profile = "desktop";
    auto records = f.records(in); auto target = build_install_operation_target(f.generation(0), 1, in, records);
    require(target.realizations.size() == 1, "capability and feature are selectors, not extra package identities");
    rejected([&] { build_remove_operation_target(target, 2, {"api"}); });
    rejected([&] { build_remove_operation_target(target, 2, {"gui"}); });
    // Profile state survives an upgrade whose request does not restate it.
    auto next = in; next.feature_states.clear(); next.targets.resize(1);
    auto next_records = f.records(next);
    auto next_target = build_install_operation_target(target, 2, next, next_records);
    require(next_target.realizations.front().semantics->feature_states.at("gui"), "existing profile inheritance");
    auto normalized_records = next_target.realizations;
    require(generation::serialize(build_install_operation_target(target, 2, next, normalized_records)) == generation::serialize(next_target),
        "already-normalized completed records remain valid");
    auto dep_in = request({package("app", {"dep"}), package("dep")});
    auto with_deps = build_install_operation_target(f.generation(0), 1, dep_in, f.records(dep_in));
    auto remaining = build_remove_operation_target(with_deps, 2, {"app"});
    require(remaining.realizations.size() == 1 && remaining.realizations.front().candidate.package.value() == "dep", "remove has no implicit autoremove");
    rejected([&] { build_remove_operation_target(with_deps, 2, {"app", "app"}); });
}
void collisions_and_concurrency() {
    PackageFixture f; auto in = request({package("app")}); auto records = f.records(in);
    f.put(build_install_operation_target(f.generation(0), 1, in, records));
    f.put(build_install_operation_target(f.generation(0), 2, in, records));
    auto first = f.prepare_install(in, 1, 20);
    rejected([&] { f.prepare_install(in, 2, 20); });
    auto second = f.prepare_install(in, 2, 22);
    auto p = f.open(); auto archive = f.archive();
    persist_package_operation(p, *f.state, archive, first); persist_package_operation(p, *f.state, archive, second);
    require(f.plan(20) && f.plan(22) && f.current().generation == 0, "both contenders retained, no selection");
    f.state.reset(); pid_t children[2]; unsigned index = 0;
    for (const auto* op : {&first, &second}) {
        const auto& ref = op->reference().generation.request;
        const auto child = ::fork(); require(child >= 0, "fork contenders"); children[index++] = child;
        if (!child) {
            ::execl("/proc/self/exe", "package_operation_test", "activate", f.root.c_str(),
                ref.plan.activation_id.value().c_str(), ref.plan.digest.value().c_str(), ref.envelope.value().c_str(), nullptr);
            ::_exit(99);
        }
    }
    unsigned winners = 0, losers = 0;
    for (const auto child : children) {
        int status = 0; require(::waitpid(child, &status, 0) == child && WIFEXITED(status), "wait contenders");
        winners += WEXITSTATUS(status) == 0; losers += WEXITSTATUS(status) == 77;
    }
    require(winners == 1 && losers == 1, "exactly one original base wins State serialization");
    f.state = std::make_unique<ProjectionState>(f.binding.generation_authority);
    require(f.plan(20) && f.plan(22) && fs::exists(f.record(20)) && fs::exists(f.record(22)), "stale loser stays retained without cleanup/rebase");
}
fs::path sync_fault_path;
bool sync_fault_after = false, sync_fault_hit = false;
int synchronization_fault(int fd) {
    const bool match = fs::read_symlink("/proc/self/fd/" + std::to_string(fd)) == sync_fault_path;
    if (match) { sync_fault_hit = true; if (!sync_fault_after) { errno = EIO; return -1; } }
    const auto rc = ::fsync(fd); if (match && rc == 0) { errno = EIO; return -1; } return rc;
}
void preparation_barrier_faults() {
    for (bool removal : {false, true}) for (bool after : {false, true}) for (unsigned boundary = 0; boundary < 4; ++boundary) {
        PackageFixture f; auto in = request({package("app")}); auto records = f.records(in);
        f.put(build_install_operation_target(f.generation(0), 1, in, records)); unsigned target = 1, activation = 20;
        if (removal) {
            auto installed = f.prepare_install(in); auto p = f.open(); auto archive = f.archive();
            persist_package_operation(p, *f.state, archive, installed);
            activate_package_operation(p, *f.state, archive, installed.reference());
            f.put(build_remove_operation_target(f.generation(1), 2, {"app"})); target = 2; activation = 21;
        }
        auto raw = f.input(target, activation); const auto original = f.current(); auto p = f.open();
        sync_fault_path = boundary == 0 ? f.objects.object_path(records.front().artifact_id) / "share/app" :
            boundary == 1 ? f.root / "generations" / std::to_string(target) / "generation.toml" :
            boundary == 2 ? f.root / "generations" / std::to_string(target) : f.root / "generations";
        sync_fault_after = after; sync_fault_hit = false; raw.durability.fsync = synchronization_fault;
        rejected([&] {
            if (removal) prepare_remove_operation(p, *f.state, raw, {"app"});
            else prepare_install_operation(p, *f.state, raw, in);
        });
        require(sync_fault_hit && !f.plan(activation) && f.current() == original, "source/generation barrier faults precede D4 and selection");
        raw.durability = core::detail::linux_syscalls();
        auto op = removal ? prepare_remove_operation(p, *f.state, raw, {"app"}) : prepare_install_operation(p, *f.state, raw, in);
        auto archive = f.archive(); persist_package_operation(p, *f.state, archive, op);
        activate_package_operation(p, *f.state, archive, op.reference());
        require(f.current().generation == target, "original request retries after preparation barrier failure");
    }
}
fs::path substitution_file;
bool substitution_hit = false;
int substitute_generation_directory(int fd) {
    const auto rc = ::fsync(fd);
    if (rc == 0 && !substitution_hit && fs::read_symlink("/proc/self/fd/" + std::to_string(fd)) == substitution_file) {
        substitution_hit = true;
        const auto bytes = read(substitution_file); const auto directory = substitution_file.parent_path();
        fs::rename(directory, directory.parent_path().parent_path() / "substituted-generation");
        fs::create_directory(directory); fs::permissions(directory, fs::perms::owner_all);
        write(substitution_file, bytes); fs::permissions(substitution_file, fs::perms::owner_read | fs::perms::owner_write);
    }
    return rc;
}
void same_bytes_substitution() {
    PackageFixture f; auto in = request({package("app")}); auto records = f.records(in);
    f.put(build_install_operation_target(f.generation(0), 1, in, records)); auto raw = f.input();
    auto p = f.open(); const auto original = f.current(); const auto digest = f.digest(1);
    substitution_file = f.root / "generations/1/generation.toml"; substitution_hit = false;
    raw.durability.fsync = substitute_generation_directory;
    rejected([&] { prepare_install_operation(p, *f.state, raw, in); });
    require(substitution_hit && f.digest(1) == digest && !f.plan() && f.current() == original,
        "same bytes in a substituted generation instance cannot satisfy the old barriers");
    raw.durability = core::detail::linux_syscalls();
    auto op = prepare_install_operation(p, *f.state, raw, in); auto archive = f.archive();
    persist_package_operation(p, *f.state, archive, op); activate_package_operation(p, *f.state, archive, op.reference());
}
void bounded_legacy_capture() {
    PackageFixture f; auto in = request({package("app")}); const auto records = f.records(in);
    const auto legacy = f.root / "legacy"; fs::create_directories(legacy / ".athena");
    const auto metadata = legacy / ".athena/metadata.toml"; write(metadata, "# sparse oversized legacy metadata");
    fs::resize_file(metadata, max_document_size + 1);
    auto base = generation::make_generation(1, {{"old", legacy.native(), {}}}, records);
    rejected([&] { build_install_operation_target(base, 2, in, records); });
    rejected([&] { build_remove_operation_target(base, 2, {"app"}); });
    require(fs::file_size(metadata) == max_document_size + 1, "bounded legacy capture never rewrites metadata");
}
void retention_faults() {
    for (bool removal : {false, true}) for (bool after : {false, true}) {
        PackageFixture f; auto in = request({package("app")}); auto records = f.records(in);
        f.put(build_install_operation_target(f.generation(0), 1, in, records));
        unsigned target = 1, activation = 20;
        if (removal) {
            auto installed = f.prepare_install(in); auto p = f.open(); auto archive = f.archive();
            persist_package_operation(p, *f.state, archive, installed);
            activate_package_operation(p, *f.state, archive, installed.reference());
            f.put(build_remove_operation_target(f.generation(1), 2, {"app"})); target = 2; activation = 21;
        }
        const auto raw = f.input(target, activation); const auto original = f.current();
        bool hit = false; ProjectionIo io; const auto rename = io.rename;
        io.rename = [&](int a, const char* b, int c, const char* d, unsigned flags) {
            const bool match = std::string_view(d) == id(activation).value();
            if (match) { hit = true; if (!after) { errno = EIO; return -1; } }
            auto rc = rename(a, b, c, d, flags); if (match && after && rc == 0) { errno = EIO; return -1; } return rc;
        };
        auto broken = f.open(io);
        rejected([&] {
            if (removal) prepare_remove_operation(broken, *f.state, raw, {"app"});
            else prepare_install_operation(broken, *f.state, raw, in);
        });
        require(hit && f.current() == original && fs::is_empty(f.archive_path) == !removal, "D4 prepare faults cannot select or archive new request");
        auto fresh = f.open();
        auto op = removal ? prepare_remove_operation(fresh, *f.state, raw, {"app"}) : prepare_install_operation(fresh, *f.state, raw, in);
        auto archive = f.archive(); persist_package_operation(fresh, *f.state, archive, op);
        activate_package_operation(fresh, *f.state, archive, op.reference());
        require(f.current().generation == target, "same preparation retries uncertain D4 retention");
    }
}
void execution_faults() {
    for (bool removal : {false, true}) for (unsigned mode = 0; mode < 3; ++mode) {
        PackageFixture f; auto op = operation(f, removal); auto p = f.open(); auto archive = f.archive();
        persist_package_operation(p, *f.state, archive, op); const auto frozen = read(f.record(removal ? 21 : 20));
        ProjectionIo io; const auto rename = io.rename; const auto sync = io.fsync;
        io.rename = [=](int a, const char* b, int c, const char* d, unsigned flags) {
            const bool current = std::string_view(d) == "current";
            if (mode == 0 && current) { errno = EIO; return -1; }
            auto rc = rename(a, b, c, d, flags);
            if (mode == 1 && current && rc == 0) { errno = EIO; return -1; } return rc;
        };
        io.fsync = [&](int fd) {
            if (mode == 2 && fs::read_symlink("/proc/self/fd/" + std::to_string(fd)) == f.root / "generations/current") {
                errno = EIO; return -1;
            }
            return sync(fd);
        };
        auto broken = f.open(io); bool hit = false;
        try { activate_package_operation(broken, *f.state, archive, op.reference()); }
        catch (const OperationalActivationError& e) {
            hit = true;
            require(e.stage == (mode == 2 ? OperationalStage::Reconciliation : OperationalStage::Selection) &&
                e.failure == (mode == 2 ? OperationalFailure::AfterSelection : OperationalFailure::SelectionUncertain) &&
                e.identity && e.identity->digest == op.generation().request().identity().digest && e.underlying,
                "D8 errors retain stage, conservative outcome and pinned identity");
        }
        require(hit && read(f.record(removal ? 21 : 20)) == frozen, "D6/D7 faults preserve B2");
        activate_package_operation(p, *f.state, archive, op.reference());
        require(f.current().activation_id == op.generation().request().identity().activation_id, "D6/D7 retry never rebases");
    }
}
void archive_faults() {
    for (bool removal : {false, true}) for (bool after : {false, true}) for (unsigned boundary = 1; boundary <= 7; ++boundary) {
        PackageFixture f; auto op = operation(f, removal); const auto original = f.current();
        auto p = f.open(); unsigned event = 0; RequestArchiveIo io;
        const auto write = io.write; const auto sync = io.files.fsync; const auto rename = io.files.rename;
        io.write = [&](int fd, const void* bytes, size_t n) -> ssize_t {
            const bool hit = ++event == boundary; if (hit && !after) { errno = EIO; return -1; }
            auto rc = write(fd, bytes, n); if (hit) { errno = EIO; return -1; } return rc;
        };
        io.files.fsync = [&](int fd) {
            const bool hit = ++event == boundary; if (hit && !after) { errno = EIO; return -1; }
            auto rc = sync(fd); if (hit) { errno = EIO; return -1; } return rc;
        };
        io.files.rename = [&](int a, const char* b, int c, const char* d, unsigned flags) {
            const bool hit = ++event == boundary; if (hit && !after) { errno = EIO; return -1; }
            auto rc = rename(a, b, c, d, flags); if (hit) { errno = EIO; return -1; } return rc;
        };
        auto broken = f.archive(io);
        rejected([&] { persist_package_operation(p, *f.state, broken, op); });
        require(event >= boundary && f.current() == original && f.plan(removal ? 21 : 20), "B2 faults preserve retention and current");
        auto archive = f.archive(); persist_package_operation(p, *f.state, archive, op);
        activate_package_operation(p, *f.state, archive, op.reference());
        require(f.current().activation_id == op.generation().request().identity().activation_id, "same B2 identity retry");
    }
    for (bool removal : {false, true}) for (bool after : {false, true}) for (unsigned boundary = 1; boundary <= 3; ++boundary) {
        PackageFixture f; auto op = operation(f, removal); auto p = f.open(); auto archive = f.archive();
        persist_package_operation(p, *f.state, archive, op); const auto original = f.current();
        RequestArchiveIo io; unsigned event = 0; const auto sync = io.files.fsync;
        io.files.fsync = [&](int fd) {
            const bool hit = ++event == boundary; if (hit && !after) { errno = EIO; return -1; }
            auto rc = sync(fd); if (hit) { errno = EIO; return -1; } return rc;
        };
        auto broken = f.archive(io);
        rejected([&] { activate_package_operation(p, *f.state, broken, op.reference()); });
        require(event >= boundary && f.current() == original, "B2 recovery faults never execute");
        activate_package_operation(p, *f.state, archive, op.reference());
    }
}
}
int main(int argc, char** argv) {
#ifdef ATHENA_B4_REAL_SCANNER
    try {
        PackageFixture f; auto in = request({package("app")}); auto records = f.records(in);
        f.put(build_install_operation_target(f.generation(0), 1, in, records));
        const auto original = f.current();
        PackageOperationInput raw{{id(20), f.binding, {original, f.digest(0), ActivationOwnership{original.activation_id}},
            1, {}, {{ArtifactSource{records.front().artifact_id}, {"share/app"}, {"share"}}}, {}}};
        auto p = f.open();
        try { (void)prepare_install_operation(p, *f.state, raw, in); }
        catch (const store::ObjectError& error) {
            require(!f.plan() && fs::is_empty(f.archive_path) && f.current() == original, "real scanner refusal has no early mutation");
            if (std::string(error.what()).find("hidden trusted.*") == std::string::npos) throw;
            std::cout << "SKIP physical B4 source authorization: " << error.what() << '\n'; return 77;
        }
        std::cout << "PASS physical production-scanner B4 preparation\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
#endif
    if (argc == 6 && std::string_view(argv[1]) == "activate") {
        try {
            const fs::path root(argv[2]);
            ProjectionBinding binding{d1_test::projection_id, (root / "generations").native(), (root / "store").native(), (root / "projection").native()};
            store::ObjectStore objects(root / "store"); ProjectionState state(binding.generation_authority);
            auto p = ProjectionContext::open(binding, objects); auto archive = OperationalRequestArchive::open(root / "requests", binding);
            activate_package_operation(p, state, archive, {PackageOperationKind::Install, {GenerationOperationKind::Switch,
                {{ActivationId(argv[3]), PlanDigest(argv[4])}, RequestEnvelopeDigest(argv[5])}}}); return 0;
        } catch (const RequestArchiveError& e) {
            try { std::rethrow_exception(e.underlying); }
            catch (const ExecutionError& cause) { if (cause.cause == ExecutionCause::SelectionConflict) return 77; }
            catch (...) {} return 98;
        } catch (...) { return 99; }
    }
    try { roundtrip(false); roundtrip(true); negatives(); tamper(); semantic_variants(); collisions_and_concurrency(); bounded_legacy_capture(); same_bytes_substitution(); preparation_barrier_faults(); retention_faults(); execution_faults(); archive_faults(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "PASS B4: simulated scanner, physical metadata/payload/symlink mechanics\n";
}
