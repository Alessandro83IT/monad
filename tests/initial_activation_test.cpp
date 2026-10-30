// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation/initial_activation.hpp"
#include "activation_execution_support.hpp"
#include "test_tmp_root.hpp"
#include "artifact/identity.hpp"
#include "store/gc.hpp"
#include "store/manifest.hpp"
#include "store/metadata.hpp"
#include "store/transaction_pins.hpp"
#include <algorithm>
#include <fcntl.h>
#include <iostream>
using namespace athena::activation;
using namespace execution_test;
namespace {
struct Fresh {
    fs::path root;
    ProjectionBinding binding;
    athena::store::ObjectStore objects;
    std::unique_ptr<ProjectionState> state;
    Fresh()
        : root([] {
              return athena::test::make_temporary_directory("athena-d8a");
          }()),
          binding{d1_test::projection_id, (root / "generations").native(),
                  (root / "store").native(), (root / "projection").native()},
          objects(root / "store") {
        for (auto name : {"generations", "store", "projection"}) {
            fs::create_directory(root / name);
            fs::permissions(root / name, fs::perms::owner_all);
        }
        state = std::make_unique<ProjectionState>(binding.generation_authority);
    }
    ~Fresh() {
        state.reset();
        for (const auto &entry : fs::recursive_directory_iterator(root))
            if (fs::is_directory(entry.symlink_status()))
                fs::permissions(entry.path(), fs::perms::owner_all, fs::perm_options::add);
        fs::remove_all(root);
    }
    void bind_store() {
        athena::store::StoreGcLock gc(binding.store_authority);
        athena::store::AuthorityContext(binding.store_authority, binding.generation_authority)
            .validate_binding(true);
    }
    auto inspect() { return inspect_initial(binding, objects, *state); }
    auto bootstrap(ProjectionIo io = {}) {
        return bootstrap_initial(binding, objects, *state, id(30), std::move(io));
    }
    auto recover(ProjectionIo io = {}) {
        return recover_initial(binding, objects, *state, std::move(io));
    }
    auto open() { return ProjectionContext::open(binding, objects); }
    void legacy(unsigned n, bool payload = false) {
        fs::create_directory(root / "generations" / std::to_string(n));
        fs::permissions(root / "generations" / std::to_string(n), fs::perms::owner_all);
        auto bytes = "id = " + std::to_string(n) + "\n";
        if (payload)
            bytes += "[[entries]]\npackage = \"demo\"\nstore = \"" +
                     (root / "store/demo").native() + "\"\n";
        write(root / "generations" / std::to_string(n) / "generation.toml", bytes);
        fs::permissions(root / "generations" / std::to_string(n) / "generation.toml",
                        fs::perms::owner_read | fs::perms::owner_write);
        write(root / "generations/current", std::to_string(n) + "\n");
        fs::permissions(root / "generations/current", fs::perms::owner_read | fs::perms::owner_write);
    }
};
template <class F> InitialActivationError error(F f, InitialCause cause) {
    try {
        f();
    } catch (const InitialActivationError &e) {
        if (e.cause != cause) {
            std::cerr << "expected " << int(cause) << " observed " << int(e.cause) << " stage "
                      << int(e.stage) << "\n";
            if (e.underlying)
                try {
                    std::rethrow_exception(e.underlying);
                } catch (const std::exception &original) {
                    std::cerr << original.what() << "\n";
                }
            throw std::runtime_error("initial error category");
        }
        return e;
    }
    throw std::runtime_error("expected initial error");
}
int fault_count = 0, fault_at = 0;
int sync_fault(int fd) {
    if (++fault_count == fault_at) {
        errno = EIO;
        return -1;
    }
    return ::fsync(fd);
}
ssize_t write_error(int, const void *, size_t) {
    errno = ENOSPC;
    return -1;
}
int renamed_error(int a, const char *b, int c, const char *e, unsigned flags) {
    auto result = athena::core::detail::linux_syscalls().rename(a, b, c, e, flags);
    if (result == 0) {
        errno = EIO;
        return -1;
    }
    return result;
}
std::string bytes(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    require(bool(in), "read exact bytes");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void exact_selection(Fresh &f, const OperationalActivationResult &result, unsigned generation,
                     const std::string &document) {
    auto p = f.open();
    CurrentSelection expected =
        CurrentV1{generation, result.identity.activation_id, result.identity.digest};
    require(inspect_selection(p, *f.state) == expected,
            "exact selected generation/activation/digest");
    require(bytes(f.root / "generations/current") == serialize(expected), "exact CurrentV1 bytes");
    auto doc =
        athena::generation::read_generation_document(f.binding.generation_authority, generation);
    require(doc.bytes == document, "generation document unchanged");
    auto plan = inspect_activation_plan(p, *f.state, result.identity.activation_id);
    require(plan.digest == result.identity.digest && plan.intent.target_generation == generation,
            "exact durable D4 identity");
}
std::vector<std::string> barriers;
int trace_sync(int fd) {
    char name[4096];
    auto n = ::readlink(("/proc/self/fd/" + std::to_string(fd)).c_str(), name, sizeof(name));
    require(n > 0, "barrier descriptor");
    barriers.emplace_back("fsync:" + fs::path(std::string(name, n)).filename().native());
    return ::fsync(fd);
}
int trace_rename(int a, const char *b, int c, const char *e, unsigned flags) {
    barriers.emplace_back("rename:" + std::string(b) + "->" + e);
    return athena::core::detail::linux_syscalls().rename(a, b, c, e, flags);
}
void run() {
    {
        Fresh f;
        require(f.inspect().scenario == InitialScenario::Fresh, "fresh classification");
        ensure_initial_generation(*f.state);
        require(!fs::exists(f.root / "generations/current"), "zero never writes current");
        auto document = bytes(f.root / "generations/0/generation.toml");
        auto result = f.bootstrap();
        exact_selection(f, result, 0, document);
        require(result.reconciliation.state == ReconciliationState::Complete, "bootstrap complete");
        require(f.inspect().scenario == InitialScenario::Selected, "selected classification");
        require(f.recover().state == ReconciliationState::Complete, "selected recovery");
        error([&] { f.bootstrap(); }, InitialCause::Conflict);
        // Pins acquire State themselves. Exercise only after releasing the initial
        // caller-owned State; no recursive acquisition or empty-source pin needed.
        f.state.reset();
        {
            athena::store::TransactionPins pins(
                {f.binding.store_authority, f.binding.generation_authority});
            pins.abandon();
        }
        f.state = std::make_unique<ProjectionState>(f.binding.generation_authority);
        f.recover();
    }
    {
        Fresh f;
        write(f.root / "projection/foreign", "foreign");
        error([&] { f.bootstrap(); }, InitialCause::Conflict);
        require(!fs::exists(f.root / "generations/0"), "foreign root rejected before publication");
    }
    for (unsigned n : {0u, 7u})
        for (bool lf : {false, true}) {
            Fresh f;
            f.legacy(n);
            const auto historical = std::to_string(n) + (lf ? "\n" : "");
            write(f.root / "generations/current", historical);
            auto initial = f.inspect();
            require(initial.scenario == InitialScenario::LegacyEmpty, "legacy empty");
            require(*initial.current == CurrentSelection(HistoricalCurrent{n, historical}),
                    "exact historical generation and LF bytes");
            auto document = bytes(f.root / "generations" / std::to_string(n) / "generation.toml");
            auto result = f.bootstrap();
            exact_selection(f, result, n, document);
            auto published =
                inspect_activation_plan(f.open(), *f.state, result.identity.activation_id);
            auto base = std::get<SelectedBase>(published.intent.base);
            require(base.current == *initial.current &&
                        base.generation_digest ==
                            athena::generation::validate_generation_document(document, n).digest &&
                        std::holds_alternative<EmptyOwnership>(base.ownership),
                    "exact empty ownership base");
            f.recover();
            f.state.reset();
            {
                athena::store::TransactionPins pins(
                    {f.binding.store_authority, f.binding.generation_authority});
                pins.abandon();
            }
            require(athena::store::GarbageCollector(
                        athena::store::AuthorityContext(f.binding.store_authority,
                                                        f.binding.generation_authority))
                        .collect()
                        .empty(),
                    "converted legacy GC compatibility");
        }
    {
        Fresh f;
        f.legacy(7, true);
        auto parent = f.root / "store/demo";
        auto source = parent / "nested";
        fixture_fs::create_directories(source / "bin");
        write(source / "bin/a", "retained");
        athena::store::write_manifest(parent, {});
        athena::store::write_metadata(parent, {"parent", "1", "fixture", "checksum", "cmake"});
        // This is a pre-existing legacy fixture, not publication under test.
        fs::permissions(parent / ".athena", fs::perms::owner_all);
        for (auto name : {"manifest.toml", "metadata.toml"})
            fs::permissions(parent / ".athena" / name, fs::perms::owner_read | fs::perms::owner_write);
        auto document =
            "id = 7\n[[entries]]\npackage = \"demo\"\nstore = \"" + source.native() + "\"\n";
        write(f.root / "generations/7/generation.toml", document);
        f.bind_store();
        f.state.reset();
        auto removed = athena::store::GarbageCollector(
                           athena::store::AuthorityContext(f.binding.store_authority,
                                                           f.binding.generation_authority))
                           .collect();
        require(removed.empty() && bytes(source / "bin/a") == "retained",
                "nested authoritative legacy root protects containing GC candidate");
    }
    for (const auto &relative :
         {".athena-gc-trash/payload", ".athena-locks/payload", "objects/payload"}) {
        Fresh f;
        f.legacy(7, true);
        auto document = "id = 7\n[[entries]]\npackage = \"demo\"\nstore = \"" +
                        (f.root / "store" / relative).native() + "\"\n";
        write(f.root / "generations/7/generation.toml", document);
        error(
            [&] { migrate_initial(f.binding, f.objects, *f.state, id(30), d1_test::adoption_id); },
            InitialCause::LegacyLocatorUnsupported);
        require(!fs::exists(f.root / "projection/.athena-activation") &&
                    !fs::exists(f.root / "store/objects"),
                "administrative locator refused before binding/adoption");
    }
    for (bool nonempty : {false, true})
        for (bool public_directory : {false, true}) {
            Fresh f;
            f.legacy(7, nonempty);
            auto gen = f.root / "generations/7";
            fs::permissions(public_directory ? gen : gen / "generation.toml",
                            public_directory ? fs::perms(0755) : fs::perms(0644));
            auto original = bytes(f.root / "generations/current");
            error(
                [&] {
                    if (nonempty)
                        migrate_initial(f.binding, f.objects, *f.state, id(30),
                                        d1_test::adoption_id);
                    else
                        f.bootstrap();
                },
                InitialCause::LegacyMetadataUnsupported);
            require(bytes(f.root / "generations/current") == original &&
                        !fs::exists(f.root / "projection/.athena-activation") &&
                        !fs::exists(f.root / "store/objects"),
                    "unsupported legacy refused before binding/selection");
        }
    {
        Fresh f;
        f.legacy(1, true);
        f.bind_store(); // S -> G1, with no registered transaction/pins.
        auto other = f.root / "generations2";
        fs::create_directory(other);
        fs::permissions(other, fs::perms::owner_all);
        fs::create_directory(other / "1");
        fs::permissions(other / "1", fs::perms::owner_all);
        write(other / "1/generation.toml", bytes(f.root / "generations/1/generation.toml"));
        fs::permissions(other / "1/generation.toml", fs::perms(0600));
        write(other / "current", "1\n");
        auto binding = f.binding;
        binding.generation_authority = other.native();
        f.state.reset(); // Never acquire two State leases in one process.
        ProjectionState second(other);
        error([&] { migrate_initial(binding, f.objects, second, id(30), d1_test::adoption_id); },
              InitialCause::StoreBindingConflict);
        require(bytes(other / "current") == "1\n" &&
                    !fs::exists(other / ".athena-projection-authority.toml") &&
                    !fs::exists(f.root / "projection/.athena-activation"),
                "G2 never selects G1 store payload");
    }
    {
        Fresh f;
        f.legacy(7, true);
        auto p = ProjectionContext::create_binding(f.binding, f.objects, *f.state);
        const auto current =
            serialize(CurrentSelection(CurrentV1{7, id(30), PlanDigest(d1_test::digest('a'))}));
        write(f.root / "generations/current", current);
        fs::permissions(f.root / "generations/current", fs::perms(0600));
        auto e = error([&] { f.recover(); }, InitialCause::StoreBindingConflict);
        require(e.stage == InitialStage::StoreBinding &&
                    e.outcome == ProjectionOutcome::OutcomeUncertain,
                "nonempty selected recovery requires complete C binding");
        require(bytes(f.root / "generations/current") == current &&
                    !fs::exists(f.root / "store/objects"),
                "missing C recovery neither binds nor changes selection");
    }
    for (int damage : {0, 1, 2}) {
        Fresh f;
        f.legacy(7);
        f.bind_store();
        if (damage == 0)
            fs::remove(f.root / "generations/.athena-store-authority.toml");
        if (damage == 1)
            fs::remove(f.root / "store/objects/v1/sha256/.athena-transactions/authority.toml");
        if (damage == 2)
            write(f.root / "store/objects/v1/sha256/.athena-transactions/authority.toml",
                  "corrupt");
        error([&] { f.bootstrap(); }, InitialCause::StoreBindingConflict);
        require(bytes(f.root / "generations/current") == "7\n",
                "store binding damage never selects");
    }
    {
        Fresh f;
        barriers.clear();
        auto ops = athena::core::detail::linux_syscalls();
        ops.fsync = trace_sync;
        ops.rename = trace_rename;
        ensure_initial_generation(*f.state, ops);
        require(barriers == std::vector<std::string>{"fsync:generation.toml",
                                                     "fsync:.initial-generation",
                                                     "rename:.initial-generation->0",
                                                     "fsync:generations", "fsync:generations"},
                "zero file/directory/rename/parent durability order");
        require(f.inspect().scenario == InitialScenario::VerifiedPreparation,
                "published zero is verified preparation, not fresh");
    }
    {
        Fresh f;
        f.legacy(1, true);
        require(f.inspect().scenario == InitialScenario::LegacyNonempty, "legacy nonempty");
        error([&] { f.bootstrap(); }, InitialCause::ExplicitMigrationRequired);
        require(!fs::exists(f.root / "projection/.athena-activation"),
                "no implicit migration mutation");
        auto e = error(
            [&] { migrate_initial(f.binding, f.objects, *f.state, id(30), d1_test::adoption_id); },
            InitialCause::PrimitiveFailure);
        require(e.stage == InitialStage::Migration && e.underlying &&
                    e.outcome == ProjectionOutcome::OutcomeUncertain,
                "explicit D3 failure retained");
        require(std::holds_alternative<HistoricalCurrent>(*f.inspect().current),
                "failed migration never selects");
    }
    {
        Fresh f;
        f.legacy(7, true);
        ProjectionIo io;
        io.durable.write = write_error;
        auto e = error(
            [&] {
                migrate_initial(f.binding, f.objects, *f.state, id(30), d1_test::adoption_id, io);
            },
            InitialCause::StoreBindingConflict);
        require(e.outcome == ProjectionOutcome::OutcomeUncertain && e.underlying,
                "C binding uncertainty retained before D3");
        try {
            std::rethrow_exception(e.underlying);
        } catch (const athena::core::detail::PersistenceError &original) {
            require(original.stage == athena::core::detail::PersistenceError::Stage::Write &&
                        original.error == ENOSPC,
                    "C binding preserves original error");
        }
        auto registry = f.root / "store/objects/v1/sha256/.athena-transactions";
        auto names = athena::store::administrative::entries(
            athena::core::detail::open_directory(registry).get());
        require(names.size() == 1 && names[0].starts_with(".athena-tmp-") &&
                    !fs::exists(f.root / "projection/.athena-activation"),
                "uncertain C authority temporary never cleaned");
        error(
            [&] { migrate_initial(f.binding, f.objects, *f.state, id(30), d1_test::adoption_id); },
            InitialCause::StoreBindingConflict);
        require(fs::exists(registry / names[0]) && bytes(f.root / "generations/current") == "7\n",
                "partial C retry cannot manufacture binding or select");
    }
    for (bool after : {false, true}) {
        Fresh f;
        f.legacy(7, true);
        ProjectionIo io;
        auto ops = athena::core::detail::linux_syscalls();
        ops.rename = after ? renamed_error : +[](int, const char *, int, const char *, unsigned) {
            errno = EIO;
            return -1;
        };
        io.durable = ops;
        auto e = error(
            [&] {
                migrate_initial(f.binding, f.objects, *f.state, id(30), d1_test::adoption_id, io);
            },
            InitialCause::StoreBindingConflict);
        require(e.outcome == ProjectionOutcome::OutcomeUncertain, "C authority rename uncertainty");
        auto registry = f.root / "store/objects/v1/sha256/.athena-transactions";
        auto names = athena::store::administrative::entries(
            athena::core::detail::open_directory(registry).get());
        require(names.size() == 1 &&
                    (after ? names[0] == "authority.toml" : names[0].starts_with(".athena-tmp-")),
                "C authority pre/post rename residue retained");
        require(bytes(f.root / "generations/current") == "7\n" &&
                    !fs::exists(f.root / "projection/.athena-activation"),
                "C fault precedes D2/D3/D6");
        error([&] { f.inspect(); }, InitialCause::StoreBindingConflict);
    }
    for (bool left : {false, true}) {
        Fresh f;
        if (left)
            write(f.root / "generations/.athena-projection-authority.toml", serialize(f.binding));
        else
            fs::create_directory(f.root / "projection/.athena-activation");
        error([&] { f.inspect(); }, InitialCause::BindingRecoveryRequired);
        error([&] { f.bootstrap(); }, InitialCause::BindingRecoveryRequired);
        error([&] { f.recover(); }, InitialCause::BindingRecoveryRequired);
    }
    {
        Fresh f;
        fs::create_directory(f.root / "projection/.athena-activation");
        write(f.root / "generations/.athena-projection-authority.toml", serialize(f.binding));
        error([&] { f.inspect(); }, InitialCause::BindingRecoveryRequired);
    }
    {
        Fresh f;
        f.legacy(4);
        fs::remove(f.root / "generations/current");
        error([&] { f.inspect(); }, InitialCause::Conflict);
    }
    {
        Fresh f;
        fs::create_symlink("missing", f.root / "generations/current");
        error([&] { f.inspect(); }, InitialCause::PrimitiveFailure);
    }
    {
        Fresh f;
        write(f.root / "generations/.athena-tmp-foreign", "foreign");
        error([&] { f.inspect(); }, InitialCause::BindingRecoveryRequired);
    }
    {
        Fresh f;
        fs::create_directory(f.root / "generations/.initial-generation");
        fs::permissions(f.root / "generations/.initial-generation", fs::perms::owner_all);
        error([&] { f.bootstrap(); }, InitialCause::Conflict);
        require(fs::exists(f.root / "generations/.initial-generation"),
                "incomplete preparation retained");
    }
    {
        Fresh f;
        fs::create_directory(f.root / "generations/.initial-generation");
        fs::permissions(f.root / "generations/.initial-generation", fs::perms(0700));
        write(f.root / "generations/.initial-generation/generation.toml",
              athena::generation::serialize(athena::generation::make_generation(0, {})));
        fs::permissions(f.root / "generations/.initial-generation/generation.toml",
                        fs::perms(0600));
        require(f.inspect().scenario == InitialScenario::VerifiedPreparation,
                "complete zero preparation verified");
        f.bootstrap();
    }
    for (const char *invalid : {"7\n\n", "07", "7 ", "7\r\n", "-1"}) {
        Fresh f;
        f.legacy(7);
        write(f.root / "generations/current", invalid);
        auto e = error([&] { f.bootstrap(); }, InitialCause::PrimitiveFailure);
        require(e.stage == InitialStage::CurrentObservation && e.underlying,
                "malformed historical current retains observation cause");
        require(bytes(f.root / "generations/current") == invalid,
                "malformed historical bytes preserved");
    }
    {
        Fresh f;
        f.legacy(7);
        write(f.root / "generations/7/generation.toml", "corrupt");
        auto e = error([&] { f.inspect(); }, InitialCause::PrimitiveFailure);
        require(e.stage == InitialStage::GenerationRead && e.underlying,
                "legacy generation read never attributed to zero/binding");
    }
    {
        Fresh f;
        f.bootstrap();
        auto current = bytes(f.root / "generations/current");
        ProjectionIo io;
        auto real = io.stat;
        io.stat = [real](int fd, const char *name, int flags, unsigned mask, struct statx *st) {
            if (std::string(name) == "current") {
                errno = EIO;
                return -1;
            }
            return real(fd, name, flags, mask, st);
        };
        auto e = error([&] { inspect_initial(f.binding, f.objects, *f.state, io); },
                       InitialCause::PrimitiveFailure);
        require(e.stage == InitialStage::CurrentObservation && e.underlying,
                "current IO is not D2 binding recovery");
        try {
            std::rethrow_exception(e.underlying);
        } catch (const ExecutionError &original) {
            require(original.error == EIO && original.cause == ExecutionCause::Io &&
                        std::string(original.what()) == "Current inspection",
                    "original type/errno/diagnostic");
        }
        require(bytes(f.root / "generations/current") == current,
                "observation error preserves selected bytes");
    }
    {
        Fresh f;
        f.bootstrap();
        auto current = bytes(f.root / "generations/current");
        write(f.root / "generations/0/generation.toml", "corrupt");
        auto e = error([&] { f.recover(); }, InitialCause::PrimitiveFailure);
        require(e.stage == InitialStage::GenerationRead && e.underlying &&
                    e.outcome == ProjectionOutcome::OutcomeUncertain,
                "selected generation read failure preserves uncertainty/original cause");
        require(bytes(f.root / "generations/current") == current,
                "failed generation recovery preserves current");
    }
    for (int boundary = 1; boundary <= 4; ++boundary) {
        Fresh f;
        auto ops = athena::core::detail::linux_syscalls();
        fault_at = boundary;
        fault_count = 0;
        ops.fsync = sync_fault;
        auto e = error([&] { ensure_initial_generation(*f.state, ops); },
                       InitialCause::PrimitiveFailure);
        require(e.outcome == ProjectionOutcome::OutcomeUncertain, "generation barrier uncertainty");
        require(!fs::exists(f.root / "generations/current"), "generation fault never selects");
        ensure_initial_generation(*f.state);
        f.bootstrap();
        f.recover();
    }
    {
        Fresh f;
        auto ops = athena::core::detail::linux_syscalls();
        ops.rename = renamed_error;
        error([&] { ensure_initial_generation(*f.state, ops); }, InitialCause::PrimitiveFailure);
        require(fs::exists(f.root / "generations/0") &&
                    !fs::exists(f.root / "generations/.initial-generation"),
                "post rename root preserved");
        ensure_initial_generation(*f.state);
        f.bootstrap();
    }
    {
        Fresh f;
        auto ops = athena::core::detail::linux_syscalls();
        ops.write = write_error;
        error([&] { ensure_initial_generation(*f.state, ops); }, InitialCause::PrimitiveFailure);
        error([&] { ensure_initial_generation(*f.state); }, InitialCause::Conflict);
        require(fs::exists(f.root / "generations/.initial-generation/generation.toml"),
                "failed write residue preserved");
    }
    {
        Fresh f;
        ProjectionIo io;
        auto real = io.mkdir;
        io.mkdir = [real](int fd, const char *name, mode_t mode) {
            auto rc = real(fd, name, mode);
            if (rc == 0 && std::string(name) == ".athena-activation") {
                errno = EIO;
                return -1;
            }
            return rc;
        };
        error([&] { f.bootstrap(io); }, InitialCause::BindingRecoveryRequired);
        error([&] { f.bootstrap(); }, InitialCause::BindingRecoveryRequired);
        require(fs::exists(f.root / "generations/0") && !fs::exists(f.root / "generations/current"),
                "D2 uncertain residue preserves zero without current");
    }
    {
        Fresh f;
        ensure_initial_generation(*f.state);
        write(f.root / "generations/0/generation.toml", "corrupt");
        error([&] { ensure_initial_generation(*f.state); }, InitialCause::Conflict);
    }
    {
        Fresh f;
        auto p = ProjectionContext::create_binding(f.binding, f.objects, *f.state);
        ensure_initial_generation(*f.state);
        auto doc = athena::generation::read_generation_document(f.binding.generation_authority, 0);
        OperationalPlanInput request{
            id(31), f.binding, AbsentBase{}, {}, {0, doc.digest, doc.bytes, {}}, {}};
        auto plan = build_operational_plan(request);
        publish_activation_plan(p, *f.state, plan.intent, plan.plan);
        error([&] { f.recover(); }, InitialCause::Conflict);
        require(!inspect_selection(p, *f.state), "recovery never promotes published plan");
        f.bootstrap();
        require(std::get<CurrentV1>(*inspect_selection(p, *f.state)).activation_id == id(30),
                "authorized bootstrap only");
    }
    {
        Fresh f;
        ProjectionIo io;
        auto real = io.rename;
        io.rename = [real](int a, const char *b, int c, const char *e, unsigned flags) {
            if (std::string(e) == "current") {
                errno = EIO;
                return -1;
            }
            return real(a, b, c, e, flags);
        };
        std::optional<PlanIdentity> authorized;
        try {
            f.bootstrap(io);
            throw std::runtime_error("expected pre-rename failure");
        } catch (const OperationalActivationError &e) {
            require(e.failure == OperationalFailure::SelectionUncertain && e.identity,
                    "pre-rename uncertainty");
            authorized = e.identity;
        }
        require(!fs::exists(f.root / "generations/current") &&
                    fs::exists(f.root / "generations" / (".athena-current-" + id(30).value())),
                "preparation retained without current");
        require(f.inspect().scenario == InitialScenario::RecoveryRequired,
                "preparation never fresh");
        error([&] { f.recover(); }, InitialCause::Conflict);
        error([&] { bootstrap_initial(f.binding, f.objects, *f.state, id(31)); },
              InitialCause::Conflict);
        auto document = bytes(f.root / "generations/0/generation.toml");
        auto result = f.bootstrap();
        require(result.identity.activation_id == authorized->activation_id &&
                    result.identity.digest == authorized->digest,
                "retry retains authorized identity/digest/base");
        exact_selection(f, result, 0, document);
        f.recover();
    }
    {
        Fresh f;
        ProjectionIo io;
        auto real = io.rename;
        io.rename = [real](int a, const char *b, int c, const char *e, unsigned flags) {
            auto result = real(a, b, c, e, flags);
            if (result == 0 && std::string(e) == "current") {
                errno = EIO;
                return -1;
            }
            return result;
        };
        try {
            f.bootstrap(io);
            throw std::runtime_error("expected selection fault");
        } catch (const OperationalActivationError &e) {
            require(e.failure == OperationalFailure::SelectionUncertain, "selection uncertainty");
        }
        barriers.clear();
        ProjectionIo trace;
        trace.fsync = trace_sync;
        f.recover(trace);
        auto barrier = std::find(barriers.begin(), barriers.end(), "fsync:current");
        require(barrier != barriers.end() && std::next(barrier) != barriers.end() &&
                    *std::next(barrier) == "fsync:generations",
                "visible current requires file and authority barriers");
        auto p = f.open();
        require(std::get<CurrentV1>(*inspect_selection(p, *f.state)).activation_id == id(30),
                "recover exact selected identity");
        ProjectionIo bad;
        bad.fsync = [](int) {
            errno = EIO;
            return -1;
        };
        error([&] { f.recover(bad); }, InitialCause::PrimitiveFailure);
        f.recover();
    }
    {
        Fresh f;
        f.bootstrap();
        auto selected = bytes(f.root / "generations/current");
        write(f.root / "projection/.athena-activation/plans" / id(30).value() / "plan.toml",
              "corrupt");
        error([&] { f.recover(); }, InitialCause::PrimitiveFailure);
        require(bytes(f.root / "generations/current") == selected,
                "corruption preserves exact selected current");
    }
}
} // namespace
void migration(const std::string &kind) {
    Fresh f;
    f.legacy(1, true);
    auto source = f.root / "store/demo";
    fs::create_directories(source / "bin");
    write(source / "bin/a", "payload");
    athena::store::write_manifest(source, {{"bin/a"}});
    athena::store::write_metadata(source, {"demo", "1", "fixture", "checksum", "cmake"});
    fs::path payload = source;
    if (kind != "tree") {
        f.bind_store(); // Only authority records, never an early transaction/pin.
        if (kind == "artifact") {
            payload = f.root / "artifact-source";
            fs::create_directories(payload / "bin");
            write(payload / "bin/a", "payload");
            fs::create_directory(payload / "empty");
        }
        auto model = athena::artifact::scan(payload);
        auto artifact = athena::artifact::derive_artifact_id(model);
        auto object = f.objects.publish_unprotected_for_test(payload, artifact);
        if (kind == "alias") {
            fs::rename(source, f.root / "original-tree");
            fs::create_symlink(fs::relative(object, source.parent_path()), source);
        } else {
            using namespace athena::core;
            RepositorySnapshotRef snapshot{RepositoryId("repo"), RepositoryRevision("rev"),
                                           SchemaVersion(1), ContentDigest(d1_test::digest('a')),
                                           SnapshotId(d1_test::digest('b'))};
            auto generation = athena::generation::make_generation(1, {});
            generation.realizations.push_back(
                {{PackageId("demo"), PackageVersion("1"), snapshot.repository_id, snapshot,
                  RepositoryProvenance{snapshot.repository_id, snapshot.revision,
                                       MetadataReference("recipe"), snapshot}},
                 artifact,
                 {},
                 ArtifactOrigin::AmbientSourceBuild,
                 athena::installed::SemanticState{PackageMetadata{PackageId("demo"),
                                                                 PackageVersion("1")}}});
            write(f.root / "generations/1/generation.toml",
                  athena::generation::serialize(generation));
            source = object;
            fs::create_directory(f.root / "projection/empty");
        }
    }
    fs::create_directory(f.root / "projection/bin");
    auto link = f.root / "projection/bin/a";
    fs::create_symlink(source / "bin/a", link);
    struct stat before{};
    require(::lstat(link.c_str(), &before) == 0, "original link inode");
    const auto document = bytes(f.root / "generations/1/generation.toml");
    ProjectionIo io;
    auto real = io.rename;
    io.rename = [real](int a, const char *b, int c, const char *e, unsigned flags) {
        if (std::string(e) == "current") {
            errno = EIO;
            return -1;
        }
        return real(a, b, c, e, flags);
    };
    std::optional<PlanIdentity> identity;
    try {
        migrate_initial(f.binding, f.objects, *f.state, id(30), d1_test::adoption_id, io);
        throw std::runtime_error("expected migration pre-rename fault");
    } catch (const OperationalActivationError &e) {
        require(e.failure == OperationalFailure::SelectionUncertain && e.identity,
                "migration pre-rename identity retained");
        identity = e.identity;
    }
    require(bytes(f.root / "generations/current") == "1\n",
            "migration fault preserves historical bytes");
    auto result = migrate_initial(f.binding, f.objects, *f.state, id(30), d1_test::adoption_id);
    require(result.identity.digest == identity->digest &&
                result.reconciliation.state == ReconciliationState::Complete,
            "explicit migration exact retry");
    exact_selection(f, result, 1, document);
    struct stat after{};
    require(::lstat(link.c_str(), &after) == 0 && before.st_dev == after.st_dev &&
                before.st_ino == after.st_ino,
            "adoption preserves link inode");
    f.recover();
    auto p = f.open();
    auto plan = inspect_activation_plan(p, *f.state, result.identity.activation_id);
    auto op = std::find_if(plan.plan.operations.begin(), plan.plan.operations.end(),
                           [](const auto &op) { return op.path == "bin/a"; });
    require(op != plan.plan.operations.end(), "link operation exists");
    auto witness = std::get<KeepLink>(op->state).new_witness;
    require(witness.owner == decltype(witness.owner)(result.identity.activation_id) &&
                witness.entry_id == op->id,
            "witness derives from actual link operation");
    p.validate_owned_symlink("bin/a", (source / "bin/a").native(), witness);
    f.state.reset();
    {
        athena::store::TransactionPins pins(
            {f.binding.store_authority, f.binding.generation_authority});
        pins.abandon();
    }
    athena::store::GarbageCollector(
        athena::store::AuthorityContext(f.binding.store_authority, f.binding.generation_authority))
        .collect();
    require(fs::exists(source / "bin/a"), "selected payload remains visible to bound GC");
    f.state = std::make_unique<ProjectionState>(f.binding.generation_authority);
    f.recover();
    fs::remove(link);
    fs::create_symlink(source / "bin/a", link);
    auto current = bytes(f.root / "generations/current");
    error([&] { f.recover(); }, InitialCause::PrimitiveFailure);
    require(bytes(f.root / "generations/current") == current,
            "foreign inode preserves selected authority");
}
int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "migration")
            for (const auto &kind : {"tree", "alias", "artifact"})
                migration(kind);
        else
            run();
    } catch (const InitialActivationError &e) {
        std::cerr << "stage " << int(e.stage) << " cause " << int(e.cause) << " outcome "
                  << int(e.outcome) << "\n";
        if (e.underlying)
            try {
                std::rethrow_exception(e.underlying);
            } catch (const std::exception &original) {
                std::cerr << original.what() << "\n";
            }
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
