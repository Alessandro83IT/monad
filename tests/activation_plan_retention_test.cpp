// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation/plan_retention.hpp"
#include "activation/plan_persistence.hpp"
#include "activation_schema_support.hpp"
#include "test_tmp_root.hpp"
#include "generation/generation.hpp"
#include "store/gc.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/vfs.h>
#include <sys/syscall.h>
#include <linux/capability.h>
#include <linux/magic.h>
#include <cstdlib>
#include <unistd.h>

namespace fs = std::filesystem;
namespace act = athena::activation;
namespace core = athena::core;
namespace gen = athena::generation;
namespace store = athena::store;
using namespace act;
using d1_test::require;
namespace {
void write(const fs::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary); out << bytes; out.close();
    require(!out.fail() && ::chmod(path.c_str(), 0600) == 0, "write fixture");
}
void directory(const fs::path& path) { require(::mkdir(path.c_str(), 0700) == 0, "mkdir fixture"); }
void fail_fixture_setup_for_test() {
    if (std::getenv("ATHENA_TEST_FAIL_D5_FIXTURE_SETUP"))
        throw std::runtime_error("injected D5 fixture-construction failure");
}
std::string read(const fs::path& path) { std::ifstream in(path, std::ios::binary); require(bool(in), "read fixture"); return {std::istreambuf_iterator<char>(in), {}}; }
std::string hex(std::size_t n, int width = 32) { std::ostringstream out; out << std::hex << std::setw(width) << std::setfill('0') << n; return out.str(); }
core::ArtifactId artifact(char c) { return core::ArtifactId(d1_test::digest(c)); }
std::string fdpath(int fd) { return fs::read_symlink("/proc/self/fd/" + std::to_string(fd)).native(); }
template<class F> void rejects(F operation) {
    try { operation(); } catch (const std::exception&) { return; }
    throw std::runtime_error("expected conservative rejection");
}
template<class F> void rejects_eio(F operation) {
    try { operation(); }
    catch (const PlanPersistenceError& e) { require(e.error == EIO && e.outcome == ProjectionOutcome::NotMutated, "D5 primary EIO/outcome preserved"); return; }
    catch (const ProjectionError& e) { require(e.error == EIO && e.outcome == ProjectionOutcome::NotMutated, "D2 primary EIO/outcome preserved"); return; }
    catch (const std::system_error& e) { require(e.code().value() == EIO, "filesystem primary EIO preserved"); return; }
    throw std::runtime_error("expected typed EIO rejection");
}
struct Fixture {
    fs::path base, generations, root, projection;
    explicit Fixture(fs::path existing = {}, fs::path parent = athena::test::temporary_root()) {
        if (!existing.empty()) { base = existing; }
        else { std::string pattern = (parent / "athena-d5-XXXXXX").native(); require(::mkdtemp(pattern.data()), "mkdtemp"); base = pattern; }
        generations = base / "generations"; root = base / "store"; projection = base / "projection";
        owned = existing.empty();
        if (owned) {
          try {
            directory(root); directory(projection);
            fail_fixture_setup_for_test();
            manager().initialize(); for (int n = 0; n < 3; ++n) manager().create({});
            require(gc().collect().empty(), "establish C binding");
            ProjectionState state(generations.native());
            auto p = ProjectionContext::create_binding(binding(), objects(), state);
            ensure_plan_namespace(p, state);
          } catch (...) {
            std::error_code ignored; fs::remove_all(base, ignored);
            throw;
          }
        }
    }
    bool owned;
    ~Fixture() { if (owned) { std::error_code e; fs::remove_all(base, e); } }
    gen::GenerationManager manager() const { return {generations, gen::GenerationManager::GenerationDirectoryTag{}}; }
    store::ObjectStore objects() const { return store::ObjectStore(root); }
    store::GarbageCollector gc() const { return store::GarbageCollector(store::AuthorityContext(root, generations)); }
    ProjectionBinding binding() const { return {d1_test::projection_id, generations.native(), root.native(), projection.native()}; }
    fs::path plans() const { return projection / ".athena-activation/plans"; }
    fs::path container(bool private_pair, std::string id = d1_test::id.value()) const { return (private_pair ? plans() / ".preparations" : plans()) / id; }
    ActivationIntent intent(ActivationId id = d1_test::id) const {
        auto value = d1_test::intent(); value.activation_id = id;
        value.generation_authority = generations.native(); value.store_authority = root.native(); value.projection_root = projection.native();
        value.target_generation = 1;
        value.base = SelectedBase{HistoricalCurrent{2, "2\n"}, gen::GenerationDocumentDigest(d1_test::digest('c')), EmptyOwnership{}};
        return value;
    }
    ActivationPlan plan(ActivationId id = d1_test::id) const {
        return {id, {ArtifactSource{artifact('a')},
            LegacyAliasSource{(root / "alias-1").native(), "literal/../object", artifact('b'), LegacySealDigest(d1_test::digest('c'))},
            LegacyTreeSource{(root / "tree-1/nested").native(), LegacySealDigest(d1_test::digest('d'))}}, {}};
    }
    void publish() const { ProjectionState state(generations.native()); auto p = ProjectionContext::open(binding(), objects()); publish_activation_plan(p, state, intent(), plan()); }
    void pair(bool private_pair, const ActivationIntent& i, const ActivationPlan& p) const {
        const auto path = container(private_pair, i.activation_id.value()); directory(path);
        write(path / "intent.toml", serialize(i)); write(path / "plan.toml", serialize(p));
    }
    PlanReferences scan(ProjectionIo io = {}) const {
        core::FileLock state(core::state_lock_path(generations), core::LockKind::State, core::LockMode::Shared);
        return scan_plan_references(generations, state, root, std::move(io));
    }
    void payload(char c) const { directory(objects().object_path(artifact(c))); write(objects().object_path(artifact(c)) / "data", "mechanical fixture, not scanner-qualified"); }
    void legacy(const char* name) const { directory(root / name); directory(root / name / ".athena"); write(root / name / ".athena/metadata.toml", "legacy ownership marker fixture"); }
    void candidates() const { for (char c : {'a', 'b', 'e'}) payload(c); for (const char* name : {"tree-1", "alias-1", "garbage-1"}) legacy(name); }
    void preserved() const {
        for (char c : {'a', 'b'}) require(fs::exists(objects().object_path(artifact(c))), "referenced artifact preserved");
        require(fs::exists(root / "tree-1") && fs::exists(root / "alias-1"), "nested legacy ancestor and alias preserved");
        require(fs::exists(generations / "1") && fs::exists(generations / "2"), "base and target retained");
        require(read(generations / "current") == "0\n", "D5 does not select");
    }
    void blocked() const {
        rejects([&] { scan(); }); rejects([&] { gc().collect(); }); rejects([&] { manager().discard(3); });
        require(fs::exists(generations / "3") && fs::exists(objects().object_path(artifact('e'))) && fs::exists(root / "garbage-1"), "failure before reclamation/discard");
        preserved();
    }
};
void roots() {
    for (bool private_pair : {false, true}) {
        Fixture f; f.candidates();
        if (private_pair) f.pair(true, f.intent(), f.plan()); else f.publish();
        const auto before = read(f.container(private_pair) / "plan.toml");
        const auto snapshot = f.scan();
        require(snapshot.generations == std::set<std::uint64_t>{1, 2} && snapshot.artifacts == std::set<std::string>{artifact('a').value(), artifact('b').value()}, "exact generation/artifact union");
        require(snapshot.legacy == std::set<std::string>{(f.root / "tree-1").native(), (f.root / "alias-1").native()}, "legacy top-level containment");
        require(snapshot.published == !private_pair && snapshot.preparations == private_pair, "private/public distinction");
        rejects([&] { f.manager().discard(1); }); rejects([&] { f.manager().discard(2); });
        f.manager().discard(3);
        require(f.gc().collect().size() == 2, "only unreferenced artifact and legacy entry detached");
        require(!fs::exists(f.objects().object_path(artifact('e'))) && !fs::exists(f.root / "garbage-1"), "real unreferenced reclamation");
        f.preserved(); require(read(f.container(private_pair) / "plan.toml") == before, "no plan cleanup");
        // A second complete plan with repeated references contributes a set union.
        const ActivationId id(hex(7)); f.pair(!private_pair, f.intent(id), f.plan(id));
        const auto duplicate = f.scan(); require(duplicate.generations == snapshot.generations && duplicate.artifacts == snapshot.artifacts && duplicate.legacy == snapshot.legacy, "cross-plan duplicates unioned");
    }
    Fixture f; auto i = f.intent(); i.target_generation = 999; i.base = AbsentBase{};
    f.pair(true, i, f.plan()); require(f.scan().generations == std::set<std::uint64_t>{999}, "absent generation/source roots retained without execution authorization");
}
void corrupt() {
    for (const auto* kind : {"empty", "one-file", "truncated", "extra", "published-malformed", "wrong-id", "wrong-binding", "outside", "duplicate", "bootstrap", "unsafe-mode", "symlink", "fifo", "hardlink", "oversize", "binding-malformed", "binding-missing-counterpart"}) {
        Fixture f; f.candidates();
        const auto private_path = f.container(true);
        if (std::string(kind) == "empty") directory(private_path);
        else if (std::string(kind) == "one-file") { directory(private_path); write(private_path / "intent.toml", serialize(f.intent())); }
        else if (std::string(kind) == "bootstrap") directory(f.projection / ".athena-activation/.plans-bootstrap");
        else if (std::string(kind) == "binding-malformed") write(f.generations / ".athena-projection-authority.toml", "bad binding\n");
        else if (std::string(kind) == "binding-missing-counterpart") fs::rename(f.projection / ".athena-activation/authority.toml", f.base / "retained-binding");
        else {
            auto i = f.intent(); auto p = f.plan();
            if (std::string(kind) == "wrong-binding") i.store_authority = "/foreign";
            if (std::string(kind) == "outside") p.sources = {LegacyTreeSource{"/foreign/tree", LegacySealDigest(d1_test::digest('a'))}};
            const bool published = std::string(kind) == "published-malformed";
            f.pair(!published, i, p); const auto path = f.container(!published);
            const auto document = path / "plan.toml";
            if (std::string(kind) == "truncated" || published) write(document, "truncated\n");
            else if (std::string(kind) == "extra") write(path / "status.toml", "not D5 cleanup authority");
            else if (std::string(kind) == "wrong-id") { p.activation_id = ActivationId(hex(2)); write(document, serialize(p)); }
            else if (std::string(kind) == "duplicate") f.pair(false, i, p);
            else if (std::string(kind) == "unsafe-mode") require(::chmod(document.c_str(), 0644) == 0, "unsafe mode");
            else if (std::string(kind) == "symlink") { fs::rename(document, f.base / "document"); fs::create_symlink(f.base / "document", document); }
            else if (std::string(kind) == "fifo") { fs::rename(document, f.base / "document"); require(::mkfifo(document.c_str(), 0600) == 0, "FIFO fixture"); }
            else if (std::string(kind) == "hardlink") fs::create_hard_link(document, f.base / "document");
            else if (std::string(kind) == "oversize") { const int fd = ::open(document.c_str(), O_WRONLY); require(fd >= 0 && ::ftruncate(fd, max_document_size + 1) == 0, "sparse oversize"); ::close(fd); }
        }
        f.blocked();
        require(fs::exists(private_path) || std::string(kind).starts_with("binding") || std::string(kind) == "bootstrap" || std::string(kind) == "published-malformed", "residue preserved");
    }
    // D1 duplicate source rejection survives D5's cross-plan set deduplication.
    Fixture f; f.candidates(); f.pair(true, f.intent(), f.plan());
    auto bytes = read(f.container(true) / "plan.toml");
    const auto begin = bytes.find("[[sources]]"), end = bytes.find("[[sources]]", begin + 1);
    require(begin != bytes.npos && end != bytes.npos, "source table fixture");
    bytes.insert(end, bytes.substr(begin, end - begin)); write(f.container(true) / "plan.toml", bytes); f.blocked();
}
void discovery() {
    Fixture f;
    { core::FileLock wrong(core::state_lock_path(f.root), core::LockKind::State); rejects([&] { scan_plan_references(f.generations, wrong); }); }
    { core::FileLock state(core::state_lock_path(f.generations), core::LockKind::State); rejects([&] { scan_plan_references(f.generations, state, "/foreign"); }); }
    fs::rename(f.plans(), f.base / "plans-held-for-test");
    require(f.scan().generations.empty(), "valid binding without plan infrastructure");
    fs::rename(f.generations / ".athena-projection-authority.toml", f.base / "binding-held-for-test");
    require(f.scan().artifacts.empty(), "explicit absent generation witness");
    f.manager().discard(3); // Historical/no registered projection compatibility.
}
void io_faults() {
    Fixture f; f.publish(); f.candidates();
    std::size_t calls = 0;
    auto provider = [&](std::size_t fail_at) {
        ProjectionIo io; auto open = io.open; auto stat = io.stat; auto pread = io.pread; auto readdir = io.readdir;
        io.open = [&, open, fail_at](int fd, const char* name, const open_how& how) { if (++calls == fail_at) { errno = EIO; return -1; } return open(fd, name, how); };
        io.stat = [&, stat, fail_at](int fd, const char* name, int flags, unsigned mask, struct statx* out) { if (++calls == fail_at) { errno = EIO; return -1; } return stat(fd, name, flags, mask, out); };
        io.pread = [&, pread, fail_at](int fd, void* b, size_t n, off_t at) -> ssize_t { if (++calls == fail_at) { errno = EIO; return -1; } return pread(fd, b, n, at); };
        io.readdir = [&, readdir, fail_at](DIR* dir) -> dirent* { if (++calls == fail_at) { errno = EIO; return nullptr; } return readdir(dir); };
        io.fsync = [](int) { errno = EIO; return -1; }; // Success requires no barrier calls.
        return io;
    };
    f.scan(provider(0)); const auto boundaries = calls;
    for (std::size_t at = 1; at <= boundaries; ++at) { calls = 0; rejects_eio([&] { f.scan(provider(at)); }); f.preserved(); }
    ProjectionIo io; auto pread = io.pread; bool interrupted = false;
    io.pread = [&](int fd, void* b, size_t n, off_t at) -> ssize_t { if (!interrupted) { interrupted = true; errno = EINTR; return -1; } return pread(fd, b, std::min<std::size_t>(n, 7), at); };
    require(f.scan(io).artifacts.size() == 2 && interrupted, "EINTR and short read progress");
    for (const auto* variant : {"eof", "count", "fields", "owner", "mount", "enosys"}) {
        ProjectionIo bad; auto stat = bad.stat; auto open = bad.open;
        if (std::string(variant) == "eof") bad.pread = [](int, void*, size_t, off_t) -> ssize_t { return 0; };
        if (std::string(variant) == "count") bad.pread = [](int, void*, size_t n, off_t) -> ssize_t { return n + 1; };
        if (std::string(variant) == "enosys") bad.open = [](int, const char*, const open_how&) { errno = ENOSYS; return -1; };
        if (std::string(variant) == "fields" || std::string(variant) == "owner" || std::string(variant) == "mount") bad.stat = [&, stat, variant](int fd, const char* name, int flags, unsigned mask, struct statx* out) {
            const int rc = stat(fd, name, flags, mask, out);
            if (!rc && std::string(name) == ".athena-projection-authority.toml") {
                if (std::string(variant) == "fields") out->stx_mask &= ~STATX_CTIME;
                if (std::string(variant) == "owner") out->stx_uid = ::geteuid() + 1;
                if (std::string(variant) == "mount") ++out->stx_mnt_id;
            }
            return rc;
        };
        rejects([&] { f.scan(bad); });
    }
    std::cout << "D5: rejected " << boundaries << " read/open/stat/enumeration fault boundaries without mutation\n";
}
void substitutions() {
    for (const auto* kind : {"document", "binding", "insertion"}) {
        Fixture f; f.publish(); ProjectionIo io; auto pread = io.pread; bool changed = false;
        io.pread = [&](int fd, void* b, size_t n, off_t at) -> ssize_t {
            const auto rc = pread(fd, b, n, at);
            if (!changed && !rc && fdpath(fd) == (f.container(false) / "plan.toml").native()) {
                changed = true;
                if (std::string(kind) == "insertion") directory(f.plans() / ".preparations" / hex(9));
                else {
                    const auto path = std::string(kind) == "binding" ? f.generations / ".athena-projection-authority.toml" : f.container(false) / "plan.toml";
                    const auto bytes = read(path); fs::rename(path, f.base / "retired-instance"); write(path, bytes);
                }
            }
            return rc;
        };
        rejects([&] { f.scan(io); }); require(changed, "controlled persistent substitution exercised");
    }
}
void limits() {
    Fixture f;
    const auto i = f.intent(); const ActivationPlan empty{d1_test::id, {}, {}};
    for (std::size_t n = 0; n < 4096; ++n) {
        const auto id = hex(n); const auto path = f.container(true, id); directory(path);
        auto intent = i; intent.activation_id = ActivationId(id); auto plan = empty; plan.activation_id = ActivationId(id);
        write(path / "intent.toml", serialize(intent)); write(path / "plan.toml", serialize(plan));
    }
    auto descriptors = [] { std::size_t count = 0; for (const auto& unused : fs::directory_iterator("/proc/self/fd")) { (void)unused; ++count; } return count; };
    const auto baseline = descriptors(); std::size_t peak = baseline;
    ProjectionIo io; auto open = io.open; auto stat = io.stat;
    io.open = [&](int fd, const char* name, const open_how& how) { const int rc = open(fd, name, how); peak = std::max(peak, descriptors()); return rc; };
    io.stat = [&](int fd, const char* name, int flags, unsigned mask, struct statx* out) { peak = std::max(peak, descriptors()); return stat(fd, name, flags, mask, out); };
    require(f.scan(io).preparations == 4096 && peak - baseline <= 24 && descriptors() == baseline, "exact combined D4 capacity, constant descriptor bound, RAII");
    directory(f.container(true, hex(4096))); rejects([&] { f.scan(); });
    require(fs::is_empty(f.container(true, hex(4096))), "4097th preserved");
    require(descriptors() == baseline, "capacity error releases descriptors");
    std::cout << "D5: 4096 pairs, additional descriptor peak " << peak - baseline << "/24; 4097th rejected\n";
}
void budgets() {
    // Exact occurrence limit, including targets; duplicates across plans still
    // count toward work bounds even though output references are deduplicated.
    {
        Fixture f; ActivationPlan p{d1_test::id, {}, {}};
        for (std::size_t n = 0; n < 65535; ++n) p.sources.push_back(ArtifactSource{core::ArtifactId("sha256:" + hex(n, 64))});
        for (std::size_t n = 0; n < 4; ++n) { const ActivationId id(hex(n)); auto i = f.intent(id); i.base = AbsentBase{}; p.activation_id = id; f.pair(true, i, p); }
        require(f.scan().artifacts.size() == 65535, "262144 reference occurrences accepted and unioned");
        const ActivationId id(hex(5)); auto i = f.intent(id); i.base = AbsentBase{}; f.pair(true, i, ActivationPlan{id, {}, {}});
        rejects([&] { f.scan(); }); require(fs::exists(f.container(true, id.value())), "reference budget preserves valid pair");
    }
    {
        Fixture f; ActivationPlan p{d1_test::id, {}, {}};
        for (std::size_t n = 0; n < 1000; ++n) {
            std::string path = hex(n, 8);
            for (int k = 0; k < 15; ++k) path += "/" + std::string(250, 'x');
            p.operations.push_back({n, std::move(path), EnsureDirectory{}});
        }
        const auto size = serialize(p).size(); require(size < max_document_size && size * 9 > max_retention_bytes, "byte-budget fixture within D1 bounds");
        for (std::size_t n = 0; n < 9; ++n) { const ActivationId id(hex(n)); p.activation_id = id; f.pair(true, f.intent(id), p); }
        rejects([&] { f.scan(); }); require(fs::exists(f.container(true, hex(8))), "64 MiB budget preserves valid pairs");
    }
}
struct Pipe {
    int ends[2]; Pipe() { require(::pipe(ends) == 0, "pipe"); }
    ~Pipe() { for (int fd : ends) if (fd >= 0) ::close(fd); }
    void receive() const { pollfd p{ends[0], POLLIN, 0}; require(::poll(&p, 1, 10000) == 1, "bounded pipe wait"); char c; require(::read(ends[0], &c, 1) == 1, "pipe event"); }
    void send() const { require(::write(ends[1], "x", 1) == 1, "pipe send"); }
};
pid_t spawn(const Fixture& f, const char* mode, int report = -1, int gate = -1) {
    const auto pid = ::fork(); require(pid >= 0, "fork");
    if (!pid) { const auto r = std::to_string(report), g = std::to_string(gate); ::execl("/proc/self/exe", "activation_plan_retention_test", "worker", mode, f.base.c_str(), r.c_str(), g.c_str(), nullptr); ::_exit(127); }
    return pid;
}
void wait_ok(pid_t pid) { int status; require(::waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0, "independent worker success"); }
int report_fd = -1, gate_fd = -1;
void pause_worker() { require(::write(report_fd, "x", 1) == 1, "worker report"); char c; require(::read(gate_fd, &c, 1) == 1, "worker gate"); }
ssize_t partial_write(int fd, const void* bytes, size_t n) {
    if (fdpath(fd).ends_with("/intent.toml")) { const auto rc = ::write(fd, bytes, n / 2); pause_worker(); return rc; }
    return ::write(fd, bytes, n);
}
int gc_mkdir(int fd, const char* name, mode_t mode) {
    if (std::string(name).starts_with(".prepare-") && fdpath(fd).ends_with("/.athena-gc-trash")) pause_worker();
    return ::mkdirat(fd, name, mode);
}
void concurrency() {
    // Publish after GC's first candidate snapshot, before its per-detach snapshot.
    { Fixture f; f.payload('a'); Pipe report, gate; const auto pid = spawn(f, "gc-pause", report.ends[1], gate.ends[0]);
      report.receive(); f.publish(); gate.send(); wait_ok(pid);
      require(fs::exists(f.objects().object_path(artifact('a'))), "GC resnapshot observes newly published root"); }
    // Independent processes: State exclusion protects a complete private pair
    // throughout a consumer observation, and discard subsequently rejects it.
    { Fixture f; f.candidates(); f.pair(true, f.intent(), f.plan()); Pipe report;
      pid_t discard;
      { core::FileLock state(core::state_lock_path(f.generations), core::LockKind::State, core::LockMode::Shared);
        require(scan_plan_references(f.generations, state, f.root).generations.contains(1), "shared consumer observation");
        wait_ok(spawn(f, "probe-state")); discard = spawn(f, "discard", report.ends[1]); report.receive(); }
      wait_ok(discard); f.preserved(); }
    { Fixture f; f.candidates(); f.publish(); const auto a = spawn(f, "gc"), b = spawn(f, "discard"); wait_ok(a); wait_ok(b); f.preserved(); }
}
void kills() {
    for (const auto* stage : {"partial", "complete", "published"}) {
        Fixture f; f.candidates(); Pipe report, gate; const auto pid = spawn(f, stage, report.ends[1], gate.ends[0]); report.receive();
        require(::kill(pid, SIGKILL) == 0, "SIGKILL worker"); int status;
        require(::waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL, "fresh State released on death");
        if (std::string(stage) == "partial") f.blocked();
        else {
            require(f.scan().artifacts.size() == 2, "complete private/public pair protects after death");
            rejects([&] { f.manager().discard(1); }); f.gc().collect(); f.preserved();
            // Fresh context reuses/publishes an exact preparation or recovers the
            // published pair, then D5 observes the same roots without cleanup.
            ProjectionState state(f.generations.native()); auto p = ProjectionContext::open(f.binding(), f.objects());
            if (std::string(stage) == "complete") publish_activation_plan(p, state, f.intent(), f.plan());
            else recover_activation_plan(p, state, {d1_test::id, derive_plan_digest(f.intent(), f.plan())});
        }
    }
}
void worker(int argc, char** argv) {
    require(argc == 6, "worker arguments"); const std::string mode = argv[2]; Fixture f(argv[3]);
    report_fd = std::stoi(argv[4]); gate_fd = std::stoi(argv[5]);
    if (mode == "probe-state") {
        try { core::FileLock state(core::state_lock_path(f.generations), core::LockKind::State, core::LockMode::Exclusive, "probe", false); }
        catch (const std::system_error& e) { require(e.code().value() == EWOULDBLOCK || e.code().value() == EAGAIN, "independent State actually busy"); return; }
        throw std::runtime_error("independent State probe unexpectedly acquired lock");
    }
    if (mode == "discard") { if (report_fd >= 0) require(::write(report_fd, "x", 1) == 1, "discard ready"); rejects([&] { f.manager().discard(1); }); return; }
    if (mode == "gc" || mode == "gc-pause") { auto ops = core::detail::linux_syscalls(); if (mode == "gc-pause") ops.mkdir = gc_mkdir; f.gc().collect(ops); return; }
    ProjectionState state(f.generations.native()); ProjectionIo io;
    if (mode == "partial") io.durable.write = partial_write;
    auto rename = io.rename;
    io.rename = [&, rename](int a, const char* x, int b, const char* y, unsigned flags) {
        const bool target = std::string(y) == d1_test::id.value();
        if (target && mode == "complete") pause_worker();
        const int rc = rename(a, x, b, y, flags);
        if (!rc && target && mode == "published") pause_worker();
        return rc;
    };
    auto p = ProjectionContext::open(f.binding(), f.objects(), io); publish_activation_plan(p, state, f.intent(), f.plan());
}
bool qualification_cleanup_failed = false;
struct QualificationMount {
    Fixture& fixture;
    fs::path target;
    bool active = false;
    bool attach(const char* source, const char* type, unsigned long flags, const char* data = nullptr) {
        if (::mount(source, target.c_str(), type, flags, data) == 0) { active = true; return true; }
        if (errno == EPERM || errno == EACCES || errno == ENODEV || errno == ENOSYS) return false;
        throw std::system_error(errno, std::generic_category(), "D5 VM mount");
    }
    bool cleanup() noexcept {
        if (!active) return true;
        if (::umount2(target.c_str(), 0) == 0) { active = false; return true; }
        qualification_cleanup_failed = true; fixture.owned = false;
        std::cerr << "D5 VM unmount FAILED; preserving " << fixture.base << '\n'; return false;
    }
    ~QualificationMount() { cleanup(); }
};
int privileged(std::string_view which) {
    if (::geteuid() != 0) { std::cout << "SKIP D5 VM: effective root required\n"; return 77; }
    fs::path parent = athena::test::temporary_root();
    if (which == "ext4") {
        const auto* supplied = std::getenv("ATHENA_D5_EXT4_TMPDIR");
        if (!supplied) { std::cout << "SKIP D5 VM ext4: dedicated existing directory required\n"; return 77; }
        parent = supplied;
        struct statfs value{}; require(::statfs(parent.c_str(), &value) == 0 && value.f_type == EXT4_SUPER_MAGIC, "supplied ext-family filesystem");
        // The magic is shared with ext2/3. Require ext4 in mountinfo as well.
        struct statx identity{}; require(::syscall(SYS_statx, AT_FDCWD, parent.c_str(), AT_SYMLINK_NOFOLLOW, STATX_MNT_ID, &identity) == 0 && (identity.stx_mask & STATX_MNT_ID), "ext4 mount identity");
        std::ifstream mounts("/proc/self/mountinfo"); std::string line; bool ext4 = false;
        while (std::getline(mounts, line)) { std::istringstream row(line); std::uint64_t id; row >> id; if (id == identity.stx_mnt_id && line.find(" - ext4 ") != line.npos) ext4 = true; }
        require(ext4 && mounts.eof() && !mounts.bad(), "dedicated directory really on ext4");
    }
    if (which == "bind" || which == "nested") {
        __user_cap_header_struct header{_LINUX_CAPABILITY_VERSION_3, 0}; __user_cap_data_struct data[2]{};
        require(::syscall(SYS_capget, &header, data) == 0, "VM capability query");
        if (!(data[CAP_SYS_ADMIN / 32].effective & (1u << (CAP_SYS_ADMIN % 32)))) { std::cout << "SKIP D5 VM: CAP_SYS_ADMIN required\n"; return 77; }
    }
    Fixture f({}, parent); f.candidates(); f.publish();
    if (which == "owner") {
        const auto path = f.container(false) / "intent.toml";
        require(::chown(path.c_str(), 1, -1) == 0, "real foreign owner"); f.blocked();
        require(::chown(path.c_str(), 0, -1) == 0, "restore fixture owner");
    } else if (which == "bind" || which == "nested") {
        const auto source = f.base / "mount-source"; directory(source);
        write(source / "intent.toml", serialize(f.intent())); write(source / "plan.toml", serialize(f.plan()));
        const auto target = f.container(false); struct statx before{}, after{};
        require(::syscall(SYS_statx, AT_FDCWD, target.c_str(), AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_MNT_ID, &before) == 0, "mount before");
        QualificationMount mount{f, target};
        if (!mount.attach(which == "bind" ? source.c_str() : "tmpfs", which == "bind" ? nullptr : "tmpfs",
                          which == "bind" ? MS_BIND : MS_NODEV | MS_NOSUID, which == "bind" ? nullptr : "mode=0700,size=8m")) {
            std::cout << "SKIP D5 VM mount prerequisites\n"; return 77;
        }
        require(::syscall(SYS_statx, AT_FDCWD, target.c_str(), AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_MNT_ID, &after) == 0 && before.stx_mnt_id != after.stx_mnt_id, "real different mount identity");
        require((before.stx_dev_major == after.stx_dev_major && before.stx_dev_minor == after.stx_dev_minor) == (which == "bind"), "same-device bind versus differing-device nested mount");
        f.blocked(); require(mount.cleanup(), "real mount cleanup must succeed");
    } else require(which == "ext4", "unknown VM qualification case");
    require(f.scan().published == 1, "VM fresh valid D5 scan");
    rejects([&] { f.manager().discard(1); }); require(f.gc().collect().size() == 2, "VM actual GC roots"); f.preserved();
    std::cout << "PASS D5 VM " << which << " (namespace mechanics, no power-loss qualification)\n"; return 0;
}
}
int main(int argc, char** argv) {
    int qualification_status = 0;
    const auto result = d1_test::run([&] {
        const std::string mode = argc > 1 ? argv[1] : "ordinary";
        if (mode == "worker") worker(argc, argv);
        else if (mode == "ordinary") { roots(); corrupt(); discovery(); io_faults(); substitutions(); }
        else if (mode == "limits") limits();
        else if (mode == "budgets") budgets();
        else if (mode == "processes") concurrency();
        else if (mode == "kills") kills();
        else if (mode == "fixture-construction-failure") {
            const auto parent = fs::temp_directory_path() / ("athena-d5-exception-" + std::to_string(::getpid()));
            fs::create_directory(parent);
            require(fs::directory_iterator(parent) == fs::directory_iterator{}, "D5 exception fixture parent initially empty");
            ::setenv("ATHENA_TEST_FAIL_D5_FIXTURE_SETUP", "1", 1);
            try { Fixture f({}, parent); throw std::runtime_error("D5 injected failure was not raised"); }
            catch (const std::runtime_error& e) {
                require(std::string(e.what()) == "injected D5 fixture-construction failure", "D5 expected injected exception");
            }
            ::unsetenv("ATHENA_TEST_FAIL_D5_FIXTURE_SETUP");
            require(fs::directory_iterator(parent) == fs::directory_iterator{}, "D5 constructor failure removed owned temporary directory");
            fs::remove(parent);
        }
        else if (mode.starts_with("privileged-")) qualification_status = privileged(mode.substr(11));
        else throw std::runtime_error("unknown D5 mode");
    });
    return qualification_cleanup_failed ? 1 : result ? result : qualification_status;
}
