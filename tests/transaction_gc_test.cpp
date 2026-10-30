// SPDX-License-Identifier: GPL-3.0-or-later
#include "store/transaction_pins.hpp"
#include "store/gc.hpp"
#include "store/gc_trash.hpp"
#include "store/object_store_detail.hpp"
#include "generation/generation.hpp"
#include "artifact/identity.hpp"
#include "artifact/scanner.hpp"
#include "realization_executor_support.hpp"
#include "filesystem_fixture_support.hpp"
#include "test_tmp_root.hpp"
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <functional>
#include <thread>

namespace fs = std::filesystem;
namespace d = athena::core::detail;
namespace a = athena::store::administrative;
using namespace athena;
namespace {
void check(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
template<class F> void rejects(F f) { bool caught = false; try { f(); } catch (const std::exception&) { caught = true; } check(caught, "expected rejection"); }
struct Fixture {
    fs::path root, store, generations;
    Fixture() {
        std::string pattern = (athena::test::temporary_root() / "athena-m25c-XXXXXX").native();
        check(::mkdtemp(pattern.data()), "mkdtemp"); root = pattern;
        store = root / "store"; generations = root / "generations";
        fixture_fs::create_directories(store);
        generation::GenerationManager manager(generations, generation::GenerationManager::GenerationDirectoryTag{}); manager.initialize();
    }
    ~Fixture() { std::error_code e; fs::remove_all(root, e); }
    store::AuthorityContext authority() const { return {store, generations}; }
    generation::GenerationManager manager() const { return {generations, generation::GenerationManager::GenerationDirectoryTag{}}; }
    store::GarbageCollector gc() const { return store::GarbageCollector(authority()); }
    fs::path object(char c) const { return store::ObjectStore(store).object_path(core::ArtifactId("sha256:" + std::string(64, c))); }
    void payload(char c) { fixture_fs::create_directories(object(c) / "nested"); std::ofstream(object(c) / "nested/data") << "complete"; }
};
core::ArtifactId id(char c) { return core::ArtifactId("sha256:" + std::string(64, c)); }
void write(const fs::path& path, const std::string& text) { std::ofstream out(path); out << text; out.close(); check(bool(out), "write fixture"); fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write); }
std::string read(const fs::path& path) { std::ifstream in(path); return {std::istreambuf_iterator<char>(in), {}}; }
void wait_ok(pid_t pid) { int status; check(::waitpid(pid, &status, 0) == pid, "waitpid"); check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "child failed"); }
template<class F> void child(F operation) {
    const auto pid = ::fork(); check(pid >= 0, "fork");
    if (!pid) { try { operation(); ::_exit(0); } catch (const std::exception& e) { std::cerr << "child: " << e.what() << '\n'; ::_exit(1); } }
    wait_ok(pid);
}
void gc_process(const Fixture& fixture) {
    const auto pid = ::fork(); check(pid >= 0, "GC subprocess fork");
    if (!pid) {
        ::execl("/proc/self/exe", "transaction_gc_test", "gc-worker", fixture.store.c_str(), fixture.generations.c_str(), nullptr);
        ::_exit(127);
    }
    wait_ok(pid);
}
struct Pipe {
    int fd[2]; Pipe() { check(::pipe2(fd, O_CLOEXEC) == 0, "pipe"); }
    ~Pipe() { ::close(fd[0]); ::close(fd[1]); }
    void send() const { check(::write(fd[1], "x", 1) == 1, "send"); }
    void close_write() { ::close(fd[1]); fd[1] = -1; }
    void receive() const { char x; check(::read(fd[0], &x, 1) == 1, "receive"); }
};

// Test-local, per-operation injection callbacks. No production mutable hooks.
enum class Point { None, RegisterBefore, RegisterAfter, RegisterBarrier, PinBefore, PinAfter, PinBarrier,
                   DetachBefore, DetachAfter, DetachSourceBarrier, DetachBothBarriers, Reclaim,
                   ObjectBefore, ObjectAfter, ObjectBarrier };
Point pause_point = Point::None, fail_point = Point::None;
int ready_fd = -1, gate_fd = -1;
bool registered = false, pinned = false, detached = false, object_published = false;
void boundary(Point point) {
    if (point == Point::None) return;
    if (pause_point == point) {
        check(::write(ready_fd, "r", 1) == 1, "boundary ready");
        char byte; check(::read(gate_fd, &byte, 1) == 1, "boundary gate");
    }
}
fs::path fd_path(int fd) { return fs::read_symlink("/proc/self/fd/" + std::to_string(fd)); }
int rename_op(int source, const char* name, int dest, const char* target, unsigned flags) {
    Point before = Point::None, after = Point::None;
    const auto parent = fd_path(dest).parent_path().filename();
    if (std::string(target) == "pin.toml") { before = Point::PinBefore; after = Point::PinAfter; }
    else if (std::string(name) == "payload" && a::hex_name(target, 64)) { before = Point::ObjectBefore; after = Point::ObjectAfter; }
    else if (a::hex_name(name, 64) && a::hex_name(target, 64)) { before = Point::DetachBefore; after = Point::DetachAfter; }
    else if (std::string(name).starts_with(".prepare-") && fd_path(dest).filename() == ".athena-transactions") {
        before = Point::RegisterBefore; after = Point::RegisterAfter;
    }
    boundary(before);
    if (before != Point::None && fail_point == before) { errno = EIO; return -1; }
    const int result = d::linux_syscalls().rename(source, name, dest, target, flags);
    if (result == 0) {
        if (after == Point::RegisterAfter) registered = true;
        if (after == Point::PinAfter) pinned = true;
        if (after == Point::DetachAfter) detached = true;
        if (after == Point::ObjectAfter) object_published = true;
        boundary(after);
    }
    return result;
}
int sync_op(int fd) {
    Point point = Point::None;
    const auto path = fd_path(fd); const auto name = path.filename();
    if (registered && name == ".athena-transactions") point = Point::RegisterBarrier;
    if (pinned && a::hex_name(name.string(), 32) && path.parent_path().filename() == ".athena-transactions") point = Point::PinBarrier;
    if (object_published && name == "sha256") point = Point::ObjectBarrier;
    if (detached && name == "sha256") point = Point::DetachSourceBarrier;
    if (detached && path.parent_path().filename() == ".athena-gc-trash") point = Point::DetachBothBarriers;
    if (point != Point::None && fail_point == point) { errno = EIO; return -1; }
    const int result = ::fsync(fd); if (result == 0) boundary(point); return result;
}
int unlink_op(int fd, const char* name, int flags) {
    boundary(Point::Reclaim);
    if (fail_point == Point::Reclaim) { errno = EIO; return -1; }
    return ::unlinkat(fd, name, flags);
}
d::Syscalls ops() { auto result = d::linux_syscalls(); result.rename = rename_op; result.fsync = sync_op; result.unlink = unlink_op; return result; }
void reset() { pause_point = fail_point = Point::None; registered = pinned = detached = object_published = false; }

void pins() {
    Fixture fixture; std::string transaction_id;
    {
        store::TransactionPins transaction(fixture.authority()); transaction_id = transaction.id();
        check(a::hex_name(transaction_id, 32), "ID grammar");
        auto registry = d::open_directory(fixture.authority().registry());
        a::private_directory(registry.get());
        {
            store::StoreGcLock lock(fixture.store);
            auto snapshot = store::scan_pins(fixture.authority()); check(snapshot.transactions && snapshot.artifacts.empty(), "empty registry roots");
        }
        transaction.add(id('b')); transaction.add(id('a')); transaction.add(id('b'));
        check(transaction.artifacts() == std::set<std::string>{id('a').value(), id('b').value()}, "monotonic union");
        rejects([&] { core::FileLock probe(fixture.authority().registry() / transaction_id / "lease", core::LockKind::TransactionLease); });
        // Fork inherits the lease description; GC never acquires transaction leases.
        fixture.payload('a'); fixture.payload('c'); gc_process(fixture);
        check(fs::exists(fixture.object('a')) && !fs::exists(fixture.object('c')), "active pin GC race");
    }
    // Owner disappeared without retirement: valid unlocked pins remain roots.
    {
        auto lease = d::open_directory(fixture.authority().registry() / transaction_id);
        auto file = d::open_at(lease.get(), "lease", O_RDWR);
        check(::flock(file.get(), LOCK_EX | LOCK_NB) == 0, "stale lease probe");
        fixture.payload('d'); check(fixture.gc().collect().size() == 1, "stale pin collection");
        check(fs::exists(fixture.object('a')), "stale pin lost root");
    }
    fixture.payload('b');
    rejects([&] { store::TransactionPins wrong({fixture.store, fixture.root / "other-generations"}); });
    check(fs::exists(fixture.object('a')), "binding rejection changed object");

    Fixture completed;
    auto record = execution_test::plan({execution_test::recipe(completed.root / "source")}).selections()[0].identity();
    realization::RealizationRecord membership{record, id('a'), {}, core::ArtifactOrigin::AmbientSourceBuild};
    {
    store::TransactionPins transaction(completed.authority()); transaction.add(id('a'));
    auto generation = completed.manager().create_unprotected_for_test({}, {membership});
    rejects([&] { transaction.handoff(0); }); // Failed handoff stops the owner conservatively.
    check(fs::exists(completed.authority().registry() / transaction.id()), "failed handoff retired pins");
    }
    Fixture handoff;
    {
        store::TransactionPins owner(handoff.authority()); owner.add(id('a'));
        auto target = handoff.manager().create_unprotected_for_test({}, {membership});
        check(handoff.manager().current().id == 0, "fixture selected target");
        owner.handoff(target.id);
        check(!fs::exists(handoff.authority().registry() / owner.id()), "handoff did not retire");
    }
    handoff.payload('a'); check(handoff.gc().collect().empty(), "unselected generation root lost");
    handoff.manager().discard(1); check(handoff.gc().collect().size() == 1, "discard did not remove root");
}

void wrong_store_activation() {
    Fixture fixture;
    const auto other_store = fixture.root / "other-store";
    fixture_fs::create_directories(other_store);
    const auto record = execution_test::plan({execution_test::recipe(fixture.root / "source")}).selections()[0].identity();
    realization::RealizationRecord membership{record, id('a'), {}, core::ArtifactOrigin::AmbientSourceBuild};
    generation::Generation target_generation;
    {
        store::TransactionPins owner(fixture.authority());
        owner.add(id('a'));
        target_generation = fixture.manager().create_unprotected_for_test({}, {membership});
        owner.handoff(target_generation.id);
    }
    const auto wrong_object = store::ObjectStore(other_store).object_path(id('a'));
    fixture_fs::create_directories(wrong_object);
    const auto activation_root = fixture.root / "activation";
    fixture_fs::create_directories(activation_root);
    write(activation_root / "sentinel", "unchanged");
    rejects([&] { fixture.manager().switch_to(target_generation.id, activation_root, store::ObjectStore(other_store)); });
    check(read(activation_root / "sentinel") == "unchanged" && fixture.manager().current().id == 0,
        "wrong-store activation mutated target or current pointer");
    // The same ArtifactId in B cannot make it adoptable. B's independent GC
    // still sees it as garbage because the failed switch created no B root.
    generation::GenerationManager other_generations(fixture.root / "other-generations",
        generation::GenerationManager::GenerationDirectoryTag{});
    other_generations.initialize();
    check(store::GarbageCollector(other_store, fixture.root / "other-generations",
        store::GarbageCollector::GenerationDirectoryTag{}).collect().size() == 1,
        "wrong-store object was adopted as a root");
}

void wrong_generation_authority() {
    Fixture fixture;
    const auto state = fixture.root / "state";
    const auto generations = state / "generations";
    generation::GenerationManager manager(generations, generation::GenerationManager::GenerationDirectoryTag{});
    manager.initialize();
    const auto package = execution_test::plan({execution_test::recipe(fixture.root / "source")}).selections()[0].identity();
    realization::RealizationRecord membership{package, id('a'), {}, core::ArtifactOrigin::AmbientSourceBuild};
    std::uint64_t rooted_id;
    {
        store::TransactionPins owner({fixture.store, generations}); owner.add(id('a'));
        rooted_id = manager.create_unprotected_for_test({}, {membership}).id;
        owner.handoff(rooted_id);
    }
    fixture_fs::create_directories(fixture.object('a'));
    rejects([&] { store::GarbageCollector(fixture.store, state,
        store::GarbageCollector::GenerationDirectoryTag{}).collect(); });
    check(fs::exists(fixture.object('a')), "state-parent authority deleted a rooted object");
    // The explicit compatibility adapter resolves a state directory to its
    // canonical generations child and can safely establish the matching pair.
    check(store::GarbageCollector(fixture.store, state).collect().empty(),
        "canonical compatibility adapter did not resolve generation authority");
    manager.discard(rooted_id);
    check(store::GarbageCollector(fixture.store, state).collect().size() == 1,
        "compatibility adapter did not collect after persisted root removal");
    const auto empty_state = fixture.root / "empty-state";
    fs::create_directory(empty_state);
    fixture_fs::create_directories(fixture.object('b'));
    rejects([&] { store::GarbageCollector(fixture.store, empty_state,
        store::GarbageCollector::GenerationDirectoryTag{}).collect(); });
    check(fs::exists(fixture.object('b')), "empty ambiguous authority was bound");
}

void unsafe_generation_root() {
    Fixture fixture;
    const auto record = execution_test::plan({execution_test::recipe(fixture.root / "source")}).selections()[0].identity();
    realization::RealizationRecord membership{record, id('a'), {}, core::ArtifactOrigin::AmbientSourceBuild};
    {
        store::TransactionPins owner(fixture.authority()); owner.add(id('a'));
        fixture.manager().create_unprotected_for_test({}, {membership});
        owner.abandon();
    }
    fixture.payload('a'); fixture.payload('b');
    const auto canonical = fixture.generations / "1";
    const auto outside = fixture.root / "outside-generation";
    fs::create_directory(outside);
    fs::remove_all(canonical);
    fs::create_directory_symlink(outside, canonical);
    rejects([&] { fixture.gc().collect(); });
    check(fs::exists(fixture.object('a')) && fs::exists(fixture.object('b')),
        "unsafe canonical generation allowed partial collection");
}

void legacy_alias_roots_object() {
    Fixture fixture;
    const auto alias_id = id('c');
    const auto object = store::ObjectStore(fixture.store).object_path(alias_id);
    fixture_fs::create_directories(object);
    const auto alias = fixture.store / "legacy-object-alias";
    fs::create_symlink(object, alias);
    fixture.manager().create_unprotected_for_test({{"legacy", alias.string()}});
    check(fixture.gc().collect().empty() && fs::exists(object),
        "legacy alias to ArtifactId object did not retain its G root");
}

void validation() {
    Fixture fixture; std::string transaction_id;
    { store::TransactionPins owner(fixture.authority()); owner.add(id('a')); transaction_id = owner.id(); }
    fixture.payload('a'); fixture.payload('b');
    const auto directory = fixture.authority().registry() / transaction_id;
    const auto manifest = directory / "pin.toml", lease = directory / "lease";
    const auto canonical = read(manifest);
    const std::vector<std::string> bad = {
        "broken = [", canonical.substr(0, canonical.size() - 2),
        "schema_version = 2\ntransaction_id = \"" + transaction_id + "\"\nartifacts = []\n",
        canonical + "extra = 1\n", store::serialize_pins(transaction_id, {}) + "#comment\n",
        "schema_version = 1\ntransaction_id = \"wrong\"\nartifacts = []\n",
        "schema_version = 1\ntransaction_id = \"" + transaction_id + "\"\nartifacts = [\"sha256:bad\"]\n",
        "schema_version = 1\ntransaction_id = \"" + transaction_id + "\"\nartifacts = [\"" + id('a').value() + "\",\"" + id('a').value() + "\"]\n",
        "schema_version = 1\ntransaction_id = \"" + transaction_id + "\"\nartifacts = [\"" + id('b').value() + "\",\"" + id('a').value() + "\"]\n",
        std::string(a::max_record_size + 1, 'x')
    };
    for (const auto& document : bad) {
        write(manifest, document); rejects([&] { fixture.gc().collect(); });
        check(fs::exists(fixture.object('a')) && fs::exists(fixture.object('b')), "invalid pin allowed detach");
    }
    write(manifest, canonical);
    fs::permissions(manifest, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);
    rejects([&] { fixture.gc().collect(); }); fs::permissions(manifest, fs::perms::owner_read | fs::perms::owner_write);
    fs::create_hard_link(manifest, directory / "hardlink"); rejects([&] { fixture.gc().collect(); }); fs::remove(directory / "hardlink");
    for (const auto& file : {manifest, lease}) {
        const auto saved = read(file); fs::remove(file); fs::create_symlink(fixture.root / "outside", file);
        rejects([&] { fixture.gc().collect(); }); fs::remove(file); write(file, saved); fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write);
    }
    fs::rename(lease, directory / "saved"); rejects([&] { fixture.gc().collect(); }); fs::rename(directory / "saved", lease);
    write(directory / "unexpected", "x"); rejects([&] { fixture.gc().collect(); }); fs::remove(directory / "unexpected");
    const auto moved = fixture.root / "saved-transaction"; fs::rename(directory, moved); fs::create_directory_symlink(moved, directory);
    rejects([&] { fixture.gc().collect(); }); fs::remove(directory); fs::rename(moved, directory);
    fs::permissions(directory, fs::perms::owner_all | fs::perms::group_exec); rejects([&] { fixture.gc().collect(); }); fs::permissions(directory, fs::perms::owner_all);
    write(lease, "substituted\n"); rejects([&] { fixture.gc().collect(); }); write(lease, transaction_id + "\n");
    if (::geteuid() == 0) {
        check(::chown(manifest.c_str(), 1, -1) == 0, "wrong-owner fixture"); rejects([&] { fixture.gc().collect(); });
        check(::chown(manifest.c_str(), ::geteuid(), -1) == 0, "restore owner");
    }
    write(fixture.generations / "0/generation.toml", "broken = ["); rejects([&] { fixture.gc().collect(); });
    check(fs::exists(fixture.object('a')) && fs::exists(fixture.object('b')), "malformed generation allowed detach");
    const auto binding = fixture.authority().registry() / "authority.toml";
    const auto binding_text = read(binding);
    auto floating_schema = binding_text;
    const auto schema = floating_schema.find("schema_version = 1");
    check(schema != std::string::npos, "binding schema fixture");
    floating_schema.replace(schema, std::string("schema_version = 1").size(), "schema_version = 1.0");
    write(binding, floating_schema);
    rejects([&] { fixture.gc().collect(); });
    write(binding, binding_text);
}

void failure_tests() {
    for (auto point : {Point::RegisterBefore, Point::RegisterBarrier}) {
        Fixture fixture; reset(); fail_point = point;
        try { store::TransactionPins transaction(fixture.authority(), ops()); check(false, "registration fault ignored"); }
        catch (const d::PersistenceError& error) { check(error.outcome_uncertain == (point == Point::RegisterBarrier), "registration uncertainty"); }
        reset(); fixture.payload('b'); fixture.gc().collect();
    }
    for (auto point : {Point::PinBefore, Point::PinBarrier}) {
        Fixture fixture; store::TransactionPins owner(fixture.authority()); owner.add(id('a'));
        reset(); fail_point = point;
        try { owner.add(id('b'), ops()); check(false, "pin fault ignored"); }
        catch (const d::PersistenceError& error) { check(error.outcome_uncertain == (point == Point::PinBarrier), "pin uncertainty"); }
        reset(); rejects([&] { owner.add(id('c')); });
        check(fs::exists(fixture.authority().registry() / owner.id()), "uncertainty retired pins");
        fixture.payload('a'); gc_process(fixture); check(fs::exists(fixture.object('a')), "old pin lost");
    }
    for (auto point : {Point::DetachBefore, Point::DetachSourceBarrier, Point::DetachBothBarriers}) {
        Fixture fixture; { store::TransactionPins owner(fixture.authority()); owner.abandon(); }
        fixture.payload('a'); reset(); fail_point = point;
        try { fixture.gc().collect(ops()); check(false, "detach fault ignored"); }
        catch (const d::PersistenceError& error) { check(error.outcome_uncertain == (point != Point::DetachBefore), "detach uncertainty"); }
        if (point == Point::DetachBefore) check(fs::exists(fixture.object('a') / "nested/data"), "failed rename changed object");
        else {
            check(!fs::exists(fixture.object('a')), "rename was not visible");
            const auto trash = fixture.authority().objects() / ".athena-gc-trash";
            bool complete = false;
            for (const auto& item : fs::directory_iterator(trash)) if (fs::exists(item.path() / std::string(64, 'a') / "nested/data")) complete = true;
            check(complete, "uncertain detach reclaimed payload");
        }
        reset(); fixture.gc().reclaim_trash();
    }
    Fixture fixture; { store::TransactionPins owner(fixture.authority()); owner.abandon(); }
    fixture.payload('a'); write(fixture.root / "sentinel", "keep");
    fs::create_symlink(fixture.root / "sentinel", fixture.object('a') / "nested/link");
    fs::permissions(fixture.object('a') / "nested", fs::perms::owner_read | fs::perms::owner_exec);
    reset(); fail_point = Point::Reclaim; rejects([&] { fixture.gc().collect(ops()); });
    check(!fs::exists(fixture.object('a')), "reclaim failure restored authority");
    reset(); fixture.gc().reclaim_trash(); check(read(fixture.root / "sentinel") == "keep", "reclaim escaped trash");
    check(store::GcTrash::containers(fixture.authority().objects()).empty(), "restart trash cleanup failed");
}

void crash_tests() {
    for (auto point : {Point::RegisterBefore, Point::RegisterAfter, Point::RegisterBarrier,
                       Point::PinBefore, Point::PinAfter, Point::PinBarrier}) {
        Fixture fixture; Pipe ready, gate;
        const auto pid = ::fork(); check(pid >= 0, "crash fork");
        if (!pid) {
            try { reset(); pause_point = point; ready_fd = ready.fd[1]; gate_fd = gate.fd[0];
                store::TransactionPins owner(fixture.authority(), ops()); owner.add(id('a'), ops()); ::_exit(2);
            } catch (...) { ::_exit(3); }
        }
        ready.close_write(); ready.receive(); check(::kill(pid, SIGKILL) == 0, "SIGKILL"); int status; ::waitpid(pid, &status, 0); check(WIFSIGNALED(status), "crash child status");
        fixture.payload('a'); fixture.payload('b'); reset();
        if (point == Point::PinBefore) {
            rejects([&] { fixture.gc().collect(); });
            check(fs::exists(fixture.object('a')) && fs::exists(fixture.object('b')), "incomplete update did not fail closed");
            continue;
        }
        fixture.gc().collect();
        const bool protected_object = point == Point::PinAfter || point == Point::PinBarrier;
        check(fs::exists(fixture.object('a')) == protected_object, "crash pin snapshot");
        check(!fs::exists(fixture.object('b')), "crash leaked unrelated object");
    }
    for (auto point : {Point::DetachBefore, Point::DetachAfter, Point::DetachSourceBarrier, Point::DetachBothBarriers, Point::Reclaim}) {
        Fixture fixture; { store::TransactionPins owner(fixture.authority()); owner.abandon(); }
        fixture.payload('a'); Pipe ready, gate;
        const auto pid = ::fork(); check(pid >= 0, "detach crash fork");
        if (!pid) { try { reset(); pause_point = point; ready_fd = ready.fd[1]; gate_fd = gate.fd[0]; fixture.gc().collect(ops()); ::_exit(2); } catch (...) { ::_exit(3); } }
        ready.close_write(); ready.receive(); check(::kill(pid, SIGKILL) == 0, "kill detach child"); int status; ::waitpid(pid, &status, 0);
        check(fs::exists(fixture.object('a')) == (point == Point::DetachBefore), "detach crash authority visibility");
        reset(); fixture.gc().reclaim_trash(); fixture.gc().collect();
        check(store::GcTrash::containers(fixture.authority().objects()).empty(), "crash reclaim residue");
    }
}

void legacy_guard() {
    Fixture fixture;
    auto legacy = fixture.store / "legacy-1";
    fixture_fs::create_directories(legacy / ".athena"); write(legacy / ".athena/metadata.toml", "owned");
    std::string tid;
    { store::TransactionPins owner(fixture.authority()); tid = owner.id(); gc_process(fixture); check(fs::exists(legacy), "active legacy guard"); }
    check(fixture.gc().collect().empty() && fs::exists(legacy), "stale legacy guard");
    // No stale reconciliation policy in C: use an independent clean authority to verify legacy detach.
    Fixture clean; auto orphan = clean.store / "legacy-1";
    fixture_fs::create_directories(orphan / ".athena"); write(orphan / ".athena/metadata.toml", "owned");
    check(clean.gc().collect() == std::vector<fs::path>{orphan}, "legacy recoverable collection");
    check(store::GcTrash::containers(clean.store).empty(), "legacy trash residue");
}

void concurrency() {
    Fixture fixture; Pipe pinned_ready, release;
    const auto publisher = ::fork(); check(publisher >= 0, "publisher fork");
    if (!publisher) {
        try {
            store::TransactionPins owner(fixture.authority()); owner.add(id('a')); pinned_ready.send(); release.receive();
            // Byte-known fixture publication exercises the exact pin/StoreGc serialization,
            // without claiming scanner eligibility or calling a production bypass.
            store::StoreGcLock lock(fixture.store);
            fixture.payload('a'); auto namespace_fd = d::open_directory(fixture.authority().objects());
            d::directory_barrier(namespace_fd.get()); ::_exit(0);
        } catch (...) { ::_exit(1); }
    }
    pinned_ready.receive(); fixture.payload('b'); fixture.gc().collect(); release.send(); wait_ok(publisher);
    fixture.gc().collect(); check(fs::exists(fixture.object('a')), "publisher GC lost live fixture");
    Pipe start; const auto gc1 = ::fork(); check(gc1 >= 0, "GC fork");
    if (!gc1) { try { start.receive(); fixture.gc().collect(); ::_exit(0); } catch (...) { ::_exit(1); } }
    const auto gc2 = ::fork(); check(gc2 >= 0, "second GC fork");
    if (!gc2) { try { start.receive(); fixture.gc().collect(); ::_exit(0); } catch (...) { ::_exit(1); } }
    fixture.payload('c'); start.send(); start.send(); wait_ok(gc1); wait_ok(gc2);
    check(!fs::exists(fixture.object('c')) && fs::exists(fixture.object('a')), "two GCs lost roots");

    Fixture locked; Pipe ready, gate;
    const auto holder = ::fork(); check(holder >= 0, "state holder fork");
    if (!holder) {
        try { core::FileLock state(core::state_lock_path(locked.generations), core::LockKind::State, core::LockMode::Shared);
            ready.send(); gate.receive(); ::_exit(0); } catch (...) { ::_exit(1); }
    }
    ready.receive();
    // Actual State lock conflicts, rather than a timing-based assertion.
    rejects([&] { core::FileLock state(core::state_lock_path(locked.generations), core::LockKind::State,
                                     core::LockMode::Exclusive, "discard conflict", false); });
    gate.send(); wait_ok(holder);
    auto generation = locked.manager().create({}); locked.manager().discard(generation.id);
}

int scanner_test() {
    Fixture fixture; fs::create_directory(fixture.root / "source"); write(fixture.root / "source/data", "payload");
    core::ArtifactId artifact("sha256:" + std::string(64, 'a'));
    try { artifact = artifact::derive_artifact_id(artifact::scan(fixture.root / "source")); }
    catch (const artifact::Error& error) {
        if (error.code() != artifact::ErrorCode::InspectionFailure || std::string(error.what()).find("hidden trusted.*") == std::string::npos) throw;
        std::cout << "SKIP scanner-dependent M2.5-C publication: " << error.what() << '\n'; return 77;
    }
    {
        store::TransactionPins owner(fixture.authority()); store::ObjectStore objects(fixture.store);
        auto prepared = objects.prepare(fixture.root / "source", artifact);
        check(owner.publish(objects, prepared).result == d::Publication::Published, "protected publication");
        gc_process(fixture); check(fs::exists(objects.object_path(artifact)), "production pin GC protection");
        auto again = objects.prepare(fixture.root / "source", artifact);
        check(owner.publish(objects, again).result == d::Publication::AlreadyExists, "protected existing adoption");
        auto selection = execution_test::plan({execution_test::recipe(fixture.root / "source")}).selections()[0].identity();
        realization::RealizationRecord record{selection, artifact, {}, core::ArtifactOrigin::AmbientSourceBuild};
        auto generation = fixture.manager().create_pinned({}, {record}, {}, owner); owner.handoff(generation.id);
    }
    check(fixture.gc().collect().empty(), "production generation root");
    for (const auto point : {Point::ObjectBefore, Point::ObjectAfter, Point::ObjectBarrier}) {
        Fixture interrupted; fs::create_directory(interrupted.root / "source"); write(interrupted.root / "source/data", "payload");
        Pipe ready, gate;
        const auto pid = ::fork(); check(pid >= 0, "production publication crash fork");
        if (!pid) {
            try {
                reset(); store::TransactionPins owner(interrupted.authority()); store::ObjectStore objects(interrupted.store);
                auto prepared = objects.prepare(interrupted.root / "source", artifact);
                pause_point = point; ready_fd = ready.fd[1]; gate_fd = gate.fd[0];
                owner.publish(objects, prepared, ops()); ::_exit(2);
            } catch (...) { ::_exit(3); }
        }
        ready.close_write(); ready.receive(); check(::kill(pid, SIGKILL) == 0, "production publication SIGKILL");
        int status; ::waitpid(pid, &status, 0); check(WIFSIGNALED(status), "production publication child status");
        reset(); interrupted.gc().collect();
        const auto path = store::ObjectStore(interrupted.store).object_path(artifact);
        check(fs::exists(path) == (point != Point::ObjectBefore), "publication crash pin protection");
    }
    Fixture uncertain; fs::create_directory(uncertain.root / "source"); write(uncertain.root / "source/data", "payload");
    {
        store::TransactionPins owner(uncertain.authority()); store::ObjectStore objects(uncertain.store);
        auto prepared = objects.prepare(uncertain.root / "source", artifact);
        reset(); fail_point = Point::ObjectBarrier;
        try { owner.publish(objects, prepared, ops()); check(false, "publication barrier fault ignored"); }
        catch (const d::PersistenceError& error) { check(error.outcome_uncertain, "publication uncertainty missing"); }
        reset(); rejects([&] { owner.abandon(); });
    }
    uncertain.gc().collect(); check(fs::exists(store::ObjectStore(uncertain.store).object_path(artifact)), "uncertain publication lost pin");
    // Two production publishers converge while a third process runs GC.
    Fixture racing; fs::create_directory(racing.root / "source"); write(racing.root / "source/data", "payload");
    Pipe start; std::vector<pid_t> children;
    for (int role = 0; role < 3; ++role) {
        const auto pid = ::fork(); check(pid >= 0, "production race fork");
        if (!pid) {
            try {
                start.receive();
                if (role == 2) racing.gc().collect();
                else {
                    store::TransactionPins owner(racing.authority()); store::ObjectStore objects(racing.store);
                    auto prepared = objects.prepare(racing.root / "source", artifact); owner.publish(objects, prepared);
                }
                ::_exit(0);
            } catch (...) { ::_exit(1); }
        }
        children.push_back(pid);
    }
    start.send(); start.send(); start.send(); for (auto pid : children) wait_ok(pid);
    racing.gc().collect(); check(fs::exists(store::ObjectStore(racing.store).object_path(artifact)), "production publishers/GC lost artifact");
    return 0;
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "gc-worker") {
            store::GarbageCollector(store::AuthorityContext(argv[2], argv[3])).collect(); return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "scanner") return scanner_test();
        const auto mode = argc > 1 ? std::string(argv[1]) : "pins";
        if (mode == "pins") { pins(); wrong_store_activation(); wrong_generation_authority(); unsafe_generation_root(); legacy_alias_roots_object(); validation(); failure_tests(); legacy_guard(); }
        else if (mode == "crash") crash_tests();
        else if (mode == "concurrency") concurrency();
        else throw std::runtime_error("unknown test mode");
        std::cout << "M2.5-C " << mode << " passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
