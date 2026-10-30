// SPDX-License-Identifier: GPL-3.0-or-later
// Build this branch only as the isolated CTest LD_PRELOAD provider. It exercises
// native locator traversal (which uses the default Syscalls), not a throwing
// ProjectionIo callback. No hook is linked into Athena's production library.
#ifdef ATHENA_D4_LOCATOR_FAULT_LIBRARY
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
namespace {
thread_local bool locator_active = false;
thread_local char locator_path[4096];
thread_local unsigned locator_hits = 0, locator_syncs = 0, locator_closes = 0;
thread_local int locator_failed_fd = -1;
thread_local bool locator_open_fault = false, locator_close_fault = false;
thread_local unsigned locator_stage = 1, locator_secondary_closes = 0;
thread_local int locator_errno = EACCES;
}
extern "C" void athena_d4_locator_arm(const char* path) {
    std::snprintf(locator_path,sizeof(locator_path),"%s",path);
    locator_hits = locator_syncs = locator_closes = 0; locator_failed_fd = -1;
    locator_open_fault = locator_close_fault = false;
    locator_stage = 1; locator_secondary_closes = 0; locator_errno = EACCES; locator_active = true;
}
extern "C" void athena_d4_filesystem_arm(const char* path,unsigned stage,int number,bool secondary) {
    athena_d4_locator_arm(path); locator_open_fault = true;
    locator_stage = stage; locator_errno = number; locator_close_fault = secondary;
}
extern "C" unsigned athena_d4_filesystem_secondary_closes() { return locator_secondary_closes; }
extern "C" void athena_d4_locator_disarm(unsigned* counts,int* failed_fd) {
    locator_active = false;
    counts[0] = locator_hits; counts[1] = locator_syncs; counts[2] = locator_closes;
    *failed_fd = locator_failed_fd;
}
extern "C" int close(int fd) {
    const auto rc = static_cast<int>(::syscall(SYS_close,fd));
    // errno after successful close is not an error channel. Deliberately change
    // it to expose code that incorrectly reads it after exception unwinding.
    if (locator_active && rc == 0) {
        ++locator_closes; errno = EIO;
        if (locator_close_fault && locator_hits) { ++locator_secondary_closes; return -1; } // fd consumed once
    }
    return rc;
}
extern "C" int fsync(int fd) {
    const auto rc = static_cast<int>(::syscall(SYS_fsync,fd));
    if (locator_active && rc == 0) ++locator_syncs;
    return rc;
}
extern "C" int openat(int fd,const char* name,int flags,...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args; va_start(args,flags); mode = va_arg(args,int); va_end(args);
    }
    if (locator_active && locator_open_fault && !locator_hits && locator_syncs >= locator_stage) {
        char link[64], path[4096];
        std::snprintf(link,sizeof(link),"/proc/self/fd/%d",fd);
        const auto n = ::readlink(link,path,sizeof(path)-1);
        if (n >= 0) {
            path[n] = 0; const auto length = static_cast<std::size_t>(n);
            std::snprintf(path+length,sizeof(path)-length,"/%s",name);
            if (!std::strcmp(path,locator_path)) { ++locator_hits; errno = locator_errno; return -1; }
        }
    }
    return static_cast<int>(::syscall(SYS_openat,fd,name,flags,mode));
}
extern "C" int fcntl(int fd,int command,...) {
    unsigned long argument = 0;
    switch (command) {
    case F_GETFD: case F_GETFL: case F_GETOWN: case F_GETSIG:
    case F_GETLEASE: case F_GETPIPE_SZ: case F_GET_SEALS: break;
    case F_DUPFD: case F_DUPFD_CLOEXEC: case F_SETFD: case F_SETFL:
    case F_SETOWN: case F_SETSIG: case F_SETLEASE: case F_SETPIPE_SZ: case F_ADD_SEALS: {
        va_list args; va_start(args,command); argument = va_arg(args,int); va_end(args); break;
    }
    default: {
        va_list args; va_start(args,command);
        argument = reinterpret_cast<unsigned long>(va_arg(args,void*)); va_end(args); break;
    }
    }
    if (locator_active && !locator_open_fault && !locator_hits && locator_syncs == 1 && command == F_SETFD) {
        char link[64], path[4096];
        std::snprintf(link,sizeof(link),"/proc/self/fd/%d",fd);
        const auto n = ::readlink(link,path,sizeof(path));
        if (n >= 0 && static_cast<std::size_t>(n) == std::strlen(locator_path) &&
            !std::memcmp(path,locator_path,static_cast<std::size_t>(n))) {
            ++locator_hits; locator_failed_fd = fd; errno = EACCES; return -1;
        }
    }
    return static_cast<int>(::syscall(SYS_fcntl,fd,command,argument));
}
#else
#include "activation/plan_persistence.hpp"
#include "activation/legacy.hpp"
#include "activation_schema_support.hpp"
#include "test_tmp_root.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <map>
#include <set>
#include <system_error>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/mount.h>
#include <sys/vfs.h>
#include <linux/magic.h>
#include <linux/capability.h>
#include <cstdlib>
#include <sstream>
#include <source_location>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
using namespace athena::activation;
namespace athena::activation {
// Private acquisition seam; no context descriptors or new public persistence API.
struct PlanEvidenceTestAccess {
    static auto publish(ProjectionContext& p, const ProjectionState& s,
                        const ActivationIntent& i, const ActivationPlan& a, plan_detail::OperationEvidence& e) {
        return PlanAccess::publish_with_evidence(p, s, i, a, &e);
    }
    static auto recover(ProjectionContext& p, const ProjectionState& s,
                        const PlanIdentity& expected, plan_detail::OperationEvidence& e) {
        return PlanAccess::recover_with_evidence(p, s, expected, &e);
    }
    static auto acquire(const ProjectionContext& p, const ProjectionState& s,
                        std::optional<ActivationId> target = {}) {
        return PlanAccess::acquire_evidence(p, s, target);
    }
    static void compare(const ProjectionContext& p, const ProjectionState& s, plan_detail::OperationEvidence& e) {
        PlanAccess::compare_foreign(p, s, e);
    }
    static void barrier(const ProjectionContext& p, const ProjectionState& s, plan_detail::OperationEvidence& e,
                        plan_detail::OperationEvidence::ObjectId id) {
        PlanAccess::evidence_barrier(p, s, e, plan_detail::OperationEvidence::Phase::Recovery, id);
    }
    static void transition(const ProjectionContext& p, const ProjectionState& s, plan_detail::OperationEvidence& e,
                           plan_detail::OperationEvidence::TransitionKind kind, plan_detail::OperationEvidence::Phase phase,
                           plan_detail::OperationEvidence::ObjectId id, plan_detail::OperationEvidence::ObjectId parent, std::string name) {
        PlanAccess::evidence_transition(p, s, e, kind, phase, id, parent, std::move(name));
    }
};
}
namespace fs = std::filesystem;
using d1_test::require;
namespace {
using C = PlanPersistenceCause;
using O = ProjectionOutcome;
bool qualification_cleanup_failed = false;
void fail_fixture_setup_for_test() {
    if (std::getenv("ATHENA_TEST_FAIL_D4_FIXTURE_SETUP"))
        throw std::runtime_error("injected D4 fixture-construction failure");
}
std::string fdpath(int fd) { return fs::read_symlink("/proc/self/fd/" + std::to_string(fd)).native(); }
ino_t inode(const fs::path& p) { struct stat s{}; require(::lstat(p.c_str(), &s) == 0, "lstat fixture"); return s.st_ino; }
void write(const fs::path& p, std::string_view b) {
    std::ofstream f(p, std::ios::binary); f << b; f.close(); require(!f.fail(), "fixture write");
    require(::chmod(p.c_str(), 0600) == 0, "fixture file mode");
}
void directory(const fs::path& p) { require(::mkdir(p.c_str(), 0700) == 0, "fixture directory"); }
struct Fixture {
    fs::path base;
    ProjectionBinding binding;
    athena::store::ObjectStore store;
    std::unique_ptr<ProjectionState> state;
    std::map<fs::path, struct stat> protected_files;
    bool preserve_on_cleanup = false;
    static fs::path temporary(const fs::path& parent) {
        auto pattern = (parent / "athena-d4-XXXXXX").native();
        auto p = ::mkdtemp(pattern.data()); require(p, "mkdtemp D4 fixture"); return p;
    }
    explicit Fixture(const fs::path& parent = athena::test::temporary_root()) : base(temporary(parent)), binding{d1_test::projection_id, (base / "generations").native(),
        (base / "store").native(), (base / "projection").native()}, store(base / "store") {
      try {
        for (auto name : {"generations", "store", "projection"}) directory(base / name);
        fail_fixture_setup_for_test();
        state = std::make_unique<ProjectionState>(binding.generation_authority);
        auto p = ProjectionContext::create_binding(binding, store, *state);
        // These unrelated namespaces must survive every tested operation.
        write(base / "generations/current", "foreign-current\n");
        write(base / "projection/payload", "untouched payload");
        write(base / "store/source", "untouched source");
        for (const auto& path : {base / "generations/current", base / "projection/payload", base / "store/source",
                base / "generations/.athena-projection-authority.toml", admin() / "authority.toml"}) {
            struct stat s{}; require(::lstat(path.c_str(), &s) == 0, "protected fixture observation"); protected_files.emplace(path, s);
        }
      } catch (...) {
        state.reset();
        std::error_code ignored; fs::remove_all(base, ignored);
        throw;
      }
    }
    ~Fixture() noexcept {
        state.reset();
        if (!preserve_on_cleanup) {
            std::error_code ec; fs::remove_all(base, ec);
            if (ec) {
                qualification_cleanup_failed = true;
                std::fprintf(stderr, "ERROR: D4 fixture cleanup '%s': %s\n", base.c_str(), ec.message().c_str());
            }
        }
    }
    fs::path admin() const { return base / "projection/.athena-activation"; }
    fs::path plans() const { return admin() / "plans"; }
    fs::path published() const { return plans() / d1_test::id.value(); }
    fs::path prepared() const { return plans() / ".preparations" / d1_test::id.value(); }
    ActivationIntent intent() const { auto i = d1_test::intent(); i.generation_authority = binding.generation_authority;
        i.store_authority = binding.store_authority; i.projection_root = binding.projection_root; return i; }
    auto open(ProjectionIo io = {}) { return ProjectionContext::open(binding, store, std::move(io)); }
    auto publish(ProjectionIo io = {}) { auto p = open(std::move(io)); return publish_activation_plan(p, *state, intent(), d1_test::plan()); }
    PlanIdentity expected() const { return {d1_test::id, derive_plan_digest(intent(), d1_test::plan())}; }
    void pristine(bool injected_status = false) const {
        auto bytes = [](const fs::path& p) { std::ifstream f(p, std::ios::binary); return std::string(std::istreambuf_iterator<char>(f), {}); };
        require(bytes(base / "generations/current") == "foreign-current\n", "current unchanged");
        require(bytes(base / "projection/payload") == "untouched payload", "payload unchanged");
        require(bytes(base / "store/source") == "untouched source", "source unchanged");
        require(bytes(admin() / "authority.toml") == serialize(binding) &&
            bytes(base / "generations/.athena-projection-authority.toml") == serialize(binding), "two-sided binding bytes unchanged");
        for (const auto& [path, before] : protected_files) {
            struct stat after{}; require(::lstat(path.c_str(), &after) == 0, "protected fixture remains");
            require(before.st_dev == after.st_dev && before.st_ino == after.st_ino && before.st_mode == after.st_mode &&
                before.st_uid == after.st_uid && before.st_nlink == after.st_nlink && before.st_size == after.st_size &&
                before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
                before.st_ctim.tv_sec == after.st_ctim.tv_sec && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec,
                "protected instance/permissions/content observations unchanged");
        }
        for (auto name : {"witnesses", "work"}) require(fs::is_empty(admin() / name), "witness/work unchanged");
        require(!fs::exists(admin() / "adoptions"), "adoptions unchanged");
        if (!injected_status) require(!fs::exists(published() / "status.toml"), "no status");
    }
};
template<class F> PlanPersistenceError error(F action, C cause, O outcome,
        std::source_location call_site = std::source_location::current()) {
    try { action(); }
    catch (const PlanPersistenceError& e) {
        require(e.cause == cause, std::string("Wrong D4 cause: ") + e.what());
        require(e.outcome == outcome, std::string("Wrong D4 outcome (expected " + std::to_string(static_cast<int>(outcome)) + ", actual " + std::to_string(static_cast<int>(e.outcome)) + "): ") + e.what()); return e;
    }
    throw std::runtime_error(std::string("Expected D4 rejection at ") + call_site.file_name() + ":" +
        std::to_string(call_site.line()) + " (cause=" + std::to_string(static_cast<int>(cause)) +
        ", outcome=" + std::to_string(static_cast<int>(outcome)) + ")");
}
template<class F> void rejects(F action) { try { action(); } catch (const std::exception&) { return; } throw std::runtime_error("Unsafe state accepted"); }
template<class F> ProjectionError projection_error(F action, ProjectionCause cause, int number = 0) {
    try { action(); }
    catch (const ProjectionError& e) {
        require(e.cause == cause && e.error == number && e.outcome == O::NotMutated,
            std::string("Wrong D2 error contract: ") + e.what()); return e;
    }
    throw std::runtime_error("Expected typed D2 rejection");
}
std::string relative(const Fixture& f, int fd) { auto path = fdpath(fd); require(path.starts_with(f.base.native()), "fixture fd ancestry"); return path.substr(f.base.native().size() + 1); }
const std::vector<std::string> recovery_order{
    "projection/.athena-activation/plans/" + d1_test::id.value() + "/intent.toml",
    "projection/.athena-activation/plans/" + d1_test::id.value() + "/plan.toml",
    "projection/.athena-activation/plans/" + d1_test::id.value(),
    "projection/.athena-activation/plans", "projection/.athena-activation/plans/.preparations",
    "projection/.athena-activation", "projection"};
void bootstrap() {
    Fixture f; std::vector<std::string> order; int renames = 0;
    ProjectionIo io; auto direct = io.rename;
    io.rename = [&](int a, const char* n, int b, const char* m, unsigned flags) {
        require(a == b && inode(fdpath(a)) == inode(fdpath(b)), "same bootstrap parent");
        require(std::string(n) == ".plans-bootstrap" && std::string(m) == "plans" && flags == RENAME_NOREPLACE, "atomic infrastructure rename");
        require(fs::is_directory(f.admin() / ".plans-bootstrap/.preparations") && !fs::exists(f.plans()), "complete private infrastructure before rename");
        ++renames; return direct(a, n, b, m, flags);
    };
    io.fsync = [&](int fd) { order.push_back(relative(f, fd)); return ::fsync(fd); };
    auto p = f.open(io);
    require(ensure_plan_namespace(p, *f.state) == O::MutatedDurably, "fresh bootstrap outcome");
    const std::vector<std::string> expected{"projection/.athena-activation/.plans-bootstrap/.preparations",
        "projection/.athena-activation/.plans-bootstrap", "projection/.athena-activation", "projection",
        "projection/.athena-activation/plans/.preparations", "projection/.athena-activation/plans",
        "projection/.athena-activation", "projection"};
    require(order == expected && renames == 1, "exact bootstrap barriers");
    order.clear(); require(ensure_plan_namespace(p, *f.state) == O::NotMutated, "existing ensure outcome");
    require(order == std::vector<std::string>(expected.begin() + 4, expected.end()), "existing ensure barriers");
    f.pristine();
    f.state.reset();
    for (int state : {0, 1, 2, 3}) {
        Fixture x; directory(x.admin() / ".plans-bootstrap");
        if (state != 0) directory(x.admin() / ".plans-bootstrap/.preparations");
        if (state == 2) { auto q = x.open(); // Manufacture simultaneous safe state for diagnosis.
            directory(x.plans()); directory(x.plans() / ".preparations"); }
        if (state == 3) directory(x.admin() / ".plans-bootstrap/.preparations" / d1_test::id.value());
        auto q = x.open();
        if (state == 1) {
            error([&] { recover_activation_plan(q, *x.state, x.expected()); }, C::MissingPlan, O::NotMutated);
            error([&] { inspect_activation_plan(q, *x.state, d1_test::id); }, C::MissingPlan, O::NotMutated);
        }
        if (state == 1) require(ensure_plan_namespace(q, *x.state) == O::MutatedDurably, "complete bootstrap reuse");
        else {
            error([&] { ensure_plan_namespace(q, *x.state); }, state == 3 ? C::AdministrativeLimit : C::BootstrapConflict, O::NotMutated);
            require(fs::is_directory(x.admin() / ".plans-bootstrap"), "bootstrap residue preserved");
        }
        x.pristine();
    }
}
std::size_t descriptor_count();
void bootstrap_evidence() {
    std::size_t maximum = 0;
    for (unsigned path = 0; path < 3; ++path) for (unsigned bit : {STATX_SIZE, STATX_MTIME, STATX_CTIME})
        for (unsigned point = 0; point < 4; ++point) {
        Fixture f;
        if (path == 1) { directory(f.admin()/".plans-bootstrap"); directory(f.admin()/".plans-bootstrap/.preparations"); }
        if (path == 2) { auto p = f.open(); ensure_plan_namespace(p, *f.state); }
        ProjectionIo io; const auto direct_stat = io.stat; const auto direct_open = io.open;
        bool active = false, acquiring = false; unsigned records = 0, fsyncs = 0, named = 0, held = 0, post_named = 0, omitted = 0; std::size_t peak = 0;
        auto measure = [&] { if (active) peak = std::max(peak, descriptor_count()); };
        io.open = [&](int fd, const char* name, const open_how& how) {
            if (active && std::string(name) == ".athena-projection-authority.toml" && ++records == 2) acquiring = true;
            const int result = direct_open(fd, name, how); measure(); return result;
        };
        io.stat = [&](int fd, const char* leaf, int flags, unsigned mask, struct statx* out) {
            measure(); const int result = direct_stat(fd, leaf, flags, mask, out);
            if (!result && active && acquiring && !omitted) {
                const auto container = path == 2 ? f.plans() : f.admin()/".plans-bootstrap";
                const bool is_named = !(flags & AT_EMPTY_PATH) && fdpath(fd) == container && std::string(leaf) == ".preparations";
                const bool is_held = (flags & AT_EMPTY_PATH) && fdpath(fd) == container/".preparations";
                if (is_named) { ++named; if (fsyncs == 1) ++post_named; } if (is_held) ++held;
                const bool inject = point == 0 ? is_named && named == 2 && !fsyncs :
                    point == 1 ? is_held && held == 1 && !fsyncs :
                    point == 2 ? is_named && fsyncs == 1 && post_named == 2 : is_held && fsyncs == 1;
                if (inject) { require((mask & bit) && (out->stx_mask & bit), "bootstrap mandatory field requested/returned"); out->stx_mask &= ~bit; ++omitted; }
            }
            return result;
        };
        io.fsync = [&](int fd) { ++fsyncs; return ::fsync(fd); };
        auto p = f.open(io); const auto baseline = descriptor_count(); active = true;
        const auto failure = error([&] { ensure_plan_namespace(p, *f.state); }, C::UnsafeEntry,
            path == 0 || point >= 2 ? O::OutcomeUncertain : O::NotMutated);
        require(omitted == 1 && failure.error == 0 && fsyncs == (point < 2 ? 0u : 1u) && descriptor_count() == baseline && peak-baseline <= 15,
            "bootstrap single omitted field: exact cause/errno/ledger/fsyncs and fd release");
        maximum = std::max(maximum, peak-baseline); active = false;
        require(!fs::exists(f.admin()/".plans-bootstrap") || !fs::exists(f.plans()), "bootstrap preserves one exclusive namespace");
        require(fs::is_directory(path == 2 ? f.plans()/".preparations" : f.admin()/".plans-bootstrap/.preparations"), "complete bootstrap residue retained");
        const unsigned prior = fsyncs;
        require(ensure_plan_namespace(p, *f.state) == (path == 2 ? O::NotMutated : O::MutatedDurably), "new invocation validates current namespace and retries");
        require(fsyncs-prior == (path == 2 ? 4u : 8u) && descriptor_count() == baseline, "retry completes exact fresh barriers without fd leaks");
        f.pristine();
    }
    for (unsigned failure_point = 0; failure_point < 2; ++failure_point) {
        Fixture f; ProjectionIo io;
        if (!failure_point) io.mkdir = [](int, const char*, mode_t) { errno = EIO; return -1; };
        else io.fsync = [](int) { errno = EIO; return -1; };
        auto p = f.open(io); const auto baseline = descriptor_count();
        const auto failure = error([&] { ensure_plan_namespace(p, *f.state); }, failure_point ? C::Durability : C::Io,
            failure_point ? O::OutcomeUncertain : O::NotMutated);
        require(failure.error == EIO && descriptor_count() == baseline && fs::exists(f.admin()/".plans-bootstrap") == bool(failure_point), "bootstrap fault preserves errno, ledger and actual residue");
        f.pristine();
    }
    std::cout << "R2-B2: 36 bootstrap mask cases, fresh/resumed/existing retries, typed ledger faults; fd peak " << maximum << "/15\n";
}
void mechanics() {
    Fixture f; auto p = f.open(); auto i = f.intent(); auto a = d1_test::plan();
    auto result = publish_activation_plan(p, *f.state, i, a);
    require(result.publication == PlanPublication::Published && result.outcome == O::MutatedDurably, "fresh publish");
    require(result.document.intent == i && result.document.plan == a && result.document.digest == f.expected().digest, "D1 documents/digest unchanged");
    require(fs::is_directory(f.published()) && !fs::exists(f.prepared()), "one pair consumed preparation");
    auto identity = inode(f.published());
    result = publish_activation_plan(p, *f.state, i, a);
    require(result.publication == PlanPublication::AlreadyPublished && result.outcome == O::NotMutated && inode(f.published()) == identity, "exact retry");
    std::vector<std::string> order; ProjectionIo io;
    io.fsync = [&](int fd) { order.push_back(relative(f, fd)); return ::fsync(fd); };
    io.mkdir = [](int, const char*, mode_t) { errno = EIO; return -1; };
    io.rename = [](int, const char*, int, const char*, unsigned) { errno = EIO; return -1; };
    io.durable.write = [](int, const void*, size_t) -> ssize_t { errno = EIO; return -1; };
    auto observed = f.open(io);
    auto document = inspect_activation_plan(observed, *f.state, d1_test::id);
    require(order.empty() && document.digest == f.expected().digest, "inspection no barriers/mutation");
    result = recover_activation_plan(observed, *f.state, f.expected());
    require(result.outcome == O::NotMutated && order == recovery_order, "seven current-instance recovery barriers");
    order.clear(); auto wrong = f.expected(); wrong.digest = PlanDigest(d1_test::digest('c'));
    error([&] { recover_activation_plan(observed, *f.state, wrong); }, C::IdentityMismatch, O::NotMutated);
    require(order.empty(), "expected digest checked before barriers");
    auto changed = i; ++changed.target_generation;
    error([&] { publish_activation_plan(p, *f.state, changed, a); }, C::PublicationConflict, O::NotMutated);
    auto foreign = i; foreign.store_authority += "-other";
    error([&] { publish_activation_plan(p, *f.state, foreign, a); }, C::AuthorityMismatch, O::NotMutated);
    f.pristine();
}
void residues() {
    for (int kind = 0; kind < 10; ++kind) {
        Fixture f; auto p = f.open(); ensure_plan_namespace(p, *f.state); directory(f.prepared());
        if (kind >= 1) write(f.prepared() / "intent.toml", serialize(f.intent()));
        if (kind >= 2) write(f.prepared() / "plan.toml", kind == 2 ? "malformed" : serialize(d1_test::plan()));
        if (kind == 4) write(f.prepared() / "extra", "extra");
        if (kind == 5) { auto different = f.intent(); ++different.target_generation; write(f.prepared() / "intent.toml", serialize(different)); }
        if (kind == 6) { auto foreign = f.intent(); foreign.store_authority += "-other"; write(f.prepared() / "intent.toml", serialize(foreign)); }
        if (kind == 7) { auto other = d1_test::plan(); other.activation_id = d1_test::old_id;
            std::get<CreateLink>(other.operations[1].state).new_witness.owner = d1_test::old_id;
            write(f.prepared() / "plan.toml", serialize(other)); }
        if (kind == 8) { auto other = d1_test::plan(); other.sources[0] = ArtifactSource{athena::core::ArtifactId(d1_test::digest('b'))};
            write(f.prepared() / "plan.toml", serialize(other)); }
        if (kind == 9) write(f.prepared() / "intent.toml", "");
        error([&] { recover_activation_plan(p, *f.state, f.expected()); }, C::MissingPlan, O::NotMutated);
        if (kind == 3) {
            auto result = publish_activation_plan(p, *f.state, f.intent(), d1_test::plan());
            require(result.publication == PlanPublication::Published && result.outcome == O::MutatedDurably, "exact preparation reused");
        } else {
            if (kind == 4) {
                try { publish_activation_plan(p, *f.state, f.intent(), d1_test::plan()); throw std::runtime_error("extra entry accepted"); }
                catch (const PlanPersistenceError& e) {
                    require((e.cause == C::AdministrativeLimit || e.cause == C::PreparationConflict) && e.outcome == O::NotMutated,
                        "unknown/third entry rejects at the first encountered violation");
                }
            } else error([&] { publish_activation_plan(p, *f.state, f.intent(), d1_test::plan()); }, C::PreparationConflict, O::NotMutated);
            require(fs::is_directory(f.prepared()) && !fs::exists(f.published()), "incomplete/conflicting private pair preserved");
        }
        f.pristine();
    }
    { Fixture f; auto p = f.open(); directory(f.plans());
        error([&] { ensure_plan_namespace(p, *f.state); }, C::NamespaceIncomplete, O::NotMutated);
        require(fs::is_empty(f.plans()), "no public infrastructure repair"); }
    { Fixture f; auto p = f.open();
        auto i = f.intent(); i.target_generation = UINT64_MAX;
        const auto e = error([&] { publish_activation_plan(p, *f.state, i, d1_test::plan()); }, C::InvalidDocument, O::NotMutated);
        require(bool(e.underlying), "D1 input diagnostic retained");
        try { std::rethrow_exception(e.underlying); }
        catch (const SchemaError& original) { require(original.code() == SchemaErrorCode::Bounds, "D1 bounds category retained"); }
        require(!fs::exists(f.plans()) && !fs::exists(f.admin() / ".plans-bootstrap"), "D1 validation precedes bootstrap"); }
}
void identity_errors() {
    for (int kind = 0; kind < 3; ++kind) {
        Fixture f; f.publish(); auto i = f.intent(); auto a = d1_test::plan();
        if (kind < 2) {
            a.activation_id = d1_test::old_id;
            std::get<CreateLink>(a.operations[1].state).new_witness.owner = d1_test::old_id;
            if (kind == 0) i.activation_id = d1_test::old_id;
        } else i.store_authority += "-foreign";
        write(f.published() / "intent.toml", serialize(i)); write(f.published() / "plan.toml", serialize(a));
        int barriers = 0; ProjectionIo io; io.fsync = [&](int fd) { ++barriers; return ::fsync(fd); }; auto p = f.open(io);
        const auto e = error([&] { recover_activation_plan(p, *f.state, f.expected()); },
            kind == 0 ? C::IdentityMismatch : kind == 1 ? C::InvalidDocument : C::AuthorityMismatch, O::NotMutated);
        require(barriers == 0, "document identity/binding/combined validation before recovery barriers");
        if (kind == 1) {
            require(bool(e.underlying), "D1 combined identity diagnostic retained");
            try { std::rethrow_exception(e.underlying); }
            catch (const SchemaError& original) { require(original.code() == SchemaErrorCode::Reference, "D1 combined identity category retained"); }
        }
        f.pristine();
    }
}
void validation_scope() {
    Fixture f; f.publish(); write(f.published() / "plan.toml", "malformed foreign bytes");
    ProjectionIo io; io.pread = [](int, void*, size_t, off_t) -> ssize_t { errno = EIO; return -1; };
    auto p = f.open(io); p.revalidate(); // No D4 document reads during global validation.
    p.create_symlink(*f.state, "managed", "literal", {d1_test::old_id, 0});
    p.validate_owned_symlink("managed", "literal", {d1_test::old_id, 0});
    auto plain = f.open();
    error([&] { inspect_activation_plan(plain, *f.state, d1_test::id); }, C::InvalidDocument, O::NotMutated);
    // A different D4 target also does not parse the malformed pair.
    auto i = f.intent(); auto a = d1_test::plan(); i.activation_id = d1_test::old_id; a.activation_id = d1_test::old_id;
    for (auto& op : a.operations) if (auto* link = std::get_if<CreateLink>(&op.state)) link->new_witness.owner = d1_test::old_id;
    auto result = publish_activation_plan(plain, *f.state, i, a);
    require(result.publication == PlanPublication::Published, "unrelated malformed documents remain opaque");
    require(::chmod((f.published() / "intent.toml").c_str(), 0644) == 0, "unsafe mode fixture");
    projection_error([&] { plain.revalidate(); }, ProjectionCause::ProjectionConflict);
}
void adoption_coexistence() {
    Fixture f;
    directory(f.admin() / "adoptions"); directory(f.admin() / "adoptions/.preparations");
    const auto adopted = f.admin() / "adoptions" / d1_test::adoption_id.value();
    directory(adopted); directory(adopted / "witnesses");
    AdoptionRecord record{d1_test::adoption_id, f.binding, {1, "1\n"},
        athena::generation::GenerationDocumentDigest(d1_test::digest('a')), {}, {}, {}};
    const auto bytes = serialize(record); write(adopted / "adoption.toml", bytes);
    const auto before = inode(adopted / "adoption.toml");
    f.publish(); write(f.published() / "plan.toml", "malformed unrelated pair");
    ProjectionIo io; io.pread = [](int, void*, size_t, off_t) -> ssize_t { errno = EIO; return -1; };
    auto p = f.open(io); p.revalidate(); LegacyAccess::namespace_shape(p);
    require(inode(adopted / "adoption.toml") == before && fs::is_empty(adopted / "witnesses") &&
        fs::is_empty(f.admin() / "adoptions/.preparations"), "D3 adoption namespace preserved alongside D4");
    std::ifstream input(adopted / "adoption.toml", std::ios::binary);
    require(std::string(std::istreambuf_iterator<char>(input), {}) == bytes, "adoption document unchanged");
}
void security() {
    for (int kind = 0; kind < 9; ++kind) {
        Fixture f; f.publish(); auto file = f.published() / "plan.toml";
        if (kind < 4 || kind == 8) fs::remove(file);
        if (kind == 0) fs::create_symlink(f.base / "store/source", file);
        if (kind == 1) require(::mkfifo(file.c_str(), 0600) == 0, "fifo fixture");
        if (kind == 2) directory(file);
        if (kind == 3) { write(file, serialize(d1_test::plan())); fs::create_hard_link(file, f.base / "second-link"); }
        if (kind == 4) require(::chmod(file.c_str(), 0644) == 0, "mode fixture");
        if (kind == 5) require(::chmod(f.plans().c_str(), 01700) == 0, "special mode fixture");
        if (kind == 6) write(f.published() / "unknown", "foreign");
        if (kind == 7) fs::rename(f.published(), f.plans() / std::string(32, 'G'));
        if (kind == 8) {
            // The gate concerns the real filesystem type, not a listening
            // endpoint. This fixture needs no network service or bind permission.
            require(::mknod(file.c_str(), S_IFSOCK | 0600, 0) == 0, "socket inode fixture");
        }
        rejects([&] { f.open(); }); f.pristine();
    }
    for (int fault = 0; fault < 6; ++fault) {
        Fixture f; f.publish(); ProjectionIo io; const auto direct = io.stat;
        io.stat = [&, direct](int fd, const char* name, int flags, unsigned mask, struct statx* out) {
            const int rc = direct(fd, name, flags, mask, out);
            if (!rc && fdpath(fd).find("/plans") != std::string::npos) {
                if (fault == 0) ++out->stx_uid;
                if (fault == 1) ++out->stx_mnt_id;
                if (fault == 2) out->stx_mask &= ~STATX_MNT_ID;
                if (fault == 3) out->stx_mask &= ~STATX_MTIME;
                if (fault == 4) out->stx_mask &= ~STATX_SIZE;
                if (fault == 5) out->stx_mask &= ~STATX_CTIME;
            } return rc;
        }; rejects([&] { f.open(io); }); f.pristine();
    }
    { Fixture f; ProjectionIo io; io.open = [](int, const char*, const open_how&) { errno = ENOSYS; return -1; }; rejects([&] { f.open(io); }); }
    { Fixture f; auto p = f.open(); fs::rename(f.base / "generations", f.base / "old-generations"); directory(f.base / "generations");
        error([&] { ensure_plan_namespace(p, *f.state); }, C::AuthorityMismatch, O::NotMutated); }
    { Fixture f; auto p = f.open(); ProjectionIo io; io.durable.fstatfs = [](int, struct statfs* out) { out->f_type = 0; return 0; };
        rejects([&] { f.open(io); });
        require(!fs::exists(f.plans()), "unsupported filesystem no mutation"); }
}
void error_boundaries() {
    for (int kind = 0; kind < 3; ++kind) {
        const bool enumeration = kind == 1, mount = kind == 2;
        Fixture f; f.publish(); bool active = false;
        ProjectionIo io; const auto direct = io.readdir; const auto stat = io.stat;
        io.readdir = [&](DIR* dir) -> struct dirent* {
            if (active && enumeration) { errno = EIO; return nullptr; }
            return direct(dir);
        };
        io.stat = [&, stat](int fd, const char* name, int flags, unsigned mask, struct statx* out) {
            const auto rc = stat(fd, name, flags, mask, out);
            if (!rc && active && mount && fdpath(fd).starts_with(f.plans().native())) ++out->stx_mnt_id;
            return rc;
        };
        auto p = f.open(io);
        if (!enumeration && !mount) require(::chmod((f.published() / "plan.toml").c_str(), 0644) == 0, "unsafe D4 document fixture");
        active = true;
        const auto cause = enumeration ? ProjectionCause::IoFailure : mount ? ProjectionCause::MountCrossing : ProjectionCause::ProjectionConflict;
        const auto number = enumeration ? EIO : 0;
        projection_error([&] { f.open(io); }, cause, number);
        projection_error([&] { p.revalidate(); }, cause, number);
        projection_error([&] { p.create_symlink(*f.state, "managed", "literal", {d1_test::id, 1}); }, cause, number);
        error([&] { recover_activation_plan(p, *f.state, f.expected()); }, enumeration ? C::Io : C::UnsafeEntry, O::NotMutated);
        auto legacy_error = [&](auto action, LegacyCause expected) {
            try { action(); }
            catch (const LegacyError& e) {
                require(e.cause == expected && e.error == number && e.outcome == O::NotMutated,
                    "D3 preserves cause, errno and outcome at D2 validation boundary");
                require(bool(e.underlying), "D3 preserves typed D2 diagnostic");
                try { std::rethrow_exception(e.underlying); }
                catch (const ProjectionError& original) {
                    require(original.cause == cause && original.error == number && original.outcome == O::NotMutated,
                        "underlying D3 error is the correct ProjectionError");
                }
                return;
            }
            throw std::runtime_error("Expected typed D3 rejection");
        };
        legacy_error([&] { seal_legacy(p, *f.state, 1, (f.base / "store/source").native()); }, LegacyCause::UnsafeSource);
        legacy_error([&] { verify_legacy_seal(p, *f.state, 1,
            LegacyTreeSource{(f.base / "store/source").native(), LegacySealDigest(d1_test::digest('a'))}); }, LegacyCause::UnsafeSource);
        legacy_error([&] { adopt_legacy_projection(p, *f.state, d1_test::adoption_id, {}); }, LegacyCause::ProjectionConflict);
        require(!fs::exists(f.base / "projection/managed"), "D2 rejection occurs before managed mutation");
        f.pristine();
    }
}
std::size_t descriptor_count() {
    DIR* dir = ::opendir("/proc/self/fd"); require(dir, "fd counter");
    std::size_t n = 0; while (auto* entry = ::readdir(dir))
        if (std::strcmp(entry->d_name, ".") && std::strcmp(entry->d_name, "..")) ++n;
    require(::closedir(dir) == 0, "fd counter close"); return n - 1;
}
void evidence_foundation() {
    using E = plan_detail::OperationEvidence;
    auto object = [](const E& e, std::string_view name) {
        for (std::size_t n = 0; n < e.objects.size(); ++n) if (e.objects[n].name == name) return n;
        throw std::runtime_error("missing evidence object");
    };
    { Fixture f; f.publish(); ProjectionIo io; auto direct_open = io.open; auto direct_stat = io.stat;
        std::size_t peak = 0; bool active = false;
        auto measure = [&] { if (active) peak = std::max(peak, descriptor_count()); };
        io.open = [&](int fd, const char* name, const open_how& how) { const int raw = direct_open(fd,name,how); measure(); return raw; };
        io.stat = [&](int fd, const char* name, int flags, unsigned mask, struct statx* out) { measure(); return direct_stat(fd,name,flags,mask,out); };
        auto p = f.open(io); const auto baseline = descriptor_count(); active = true;
        {
            auto e = PlanEvidenceTestAccess::acquire(p, *f.state, d1_test::id);
            require(e.target && e.bindings.size() == 2 && e.pins.size() == E::max_pinned_fds, "target and both binding pins");
            require(e.target->intent_bytes == serialize(f.intent()) && e.target->plan_bytes == serialize(d1_test::plan()) &&
                e.target->document.digest == f.expected().digest, "canonical target evidence");
            for (auto& b : e.bindings) require(b.canonical_bytes == serialize(f.binding), "canonical named binding evidence");
            e.target->independent_expected = f.expected();
            auto plans = object(e, "plans"), prep = object(e, ".preparations"), admin = object(e, ".athena-activation");
            require(e.objects[plans].parent == admin && e.objects[prep].parent == plans &&
                e.objects[e.target->container].parent == plans, "retained named relations");
            auto original = e.objects[e.target->intent].metadata; auto changed = original; ++changed.stx_mtime.tv_nsec;
            require(e.matches(e.target->intent, original) && !e.matches(e.target->intent, changed), "full observation comparator");
            ++changed.stx_mnt_id; require(!E::same_identity(original, changed), "mount belongs to identity");
            const auto batch = e.inventories.front(); require(e.same_inventory(batch, batch), "inventory retained");
            auto changed_batch = batch; ++changed_batch.after.stx_ctime.tv_nsec;
            require(!e.same_inventory(batch, changed_batch), "inventory change comparator");
            PlanEvidenceTestAccess::barrier(p, *f.state, e, e.target->intent);
            require(e.barriers.size() == 1 && e.barriers[0].instance.stx_ino == original.stx_ino, "real barrier instance association");
            require(descriptor_count() == baseline + E::max_pinned_fds && peak-baseline <= E::max_acquisition_fds, "only bounded pins survive target acquisition");
        }
        require(descriptor_count() == baseline, "success evidence releases pins");
        struct IntentionalUnwind : std::runtime_error { IntentionalUnwind() : std::runtime_error("unwind") {} };
        bool acquired = false, launched = false, caught = false;
        try {
            auto e = PlanEvidenceTestAccess::acquire(p, *f.state, d1_test::id);
            require(e.target && e.pins.size() == E::max_pinned_fds && descriptor_count() == baseline + E::max_pinned_fds,
                "unwind acquisition completed with owned descriptors");
            acquired = true; launched = true; throw IntentionalUnwind{};
        } catch (const IntentionalUnwind& e) { caught = std::string(e.what()) == "unwind"; }
        require(acquired && launched && caught, "only intentional unwind satisfies exception test");
        require(descriptor_count() == baseline, "exception evidence releases pins"); f.pristine();
    }
    { Fixture f; directory(f.admin() / ".plans-bootstrap"); directory(f.admin() / ".plans-bootstrap/.preparations");
        auto p = f.open(); auto e = PlanEvidenceTestAccess::acquire(p, *f.state);
        auto b = object(e, ".plans-bootstrap"), admin = object(e, ".athena-activation");
        const auto instance = e.objects[b].metadata;
        require(::rename((f.admin()/".plans-bootstrap").c_str(), f.plans().c_str()) == 0, "real bootstrap transition");
        PlanEvidenceTestAccess::transition(p, *f.state, e, E::TransitionKind::BootstrapPublication, E::Phase::Bootstrap, b, admin, "plans");
        require(e.objects[b].name == "plans" && e.transitions.front().old_name == ".plans-bootstrap" &&
            E::same_identity(instance, e.objects[b].metadata), "bootstrap transition preserves instance and history");
        bool rejected = false; try { e.record_transition(E::TransitionKind::BootstrapPublication, E::Phase::Bootstrap, b, admin, "plans", instance); }
        catch (const std::logic_error&) { rejected = true; } require(rejected && e.transitions.size() == 1, "unauthorized repeated transition rejects");
        f.pristine();
    }
    { Fixture f; auto p = f.open(); ensure_plan_namespace(p, *f.state); directory(f.prepared());
        write(f.prepared()/"intent.toml", serialize(f.intent())); write(f.prepared()/"plan.toml", serialize(d1_test::plan()));
        auto e = PlanEvidenceTestAccess::acquire(p, *f.state, d1_test::id); auto plans = object(e, "plans");
        auto c = e.target->container; const auto initial = e.objects[c];
        require(::rename(f.prepared().c_str(), f.published().c_str()) == 0, "real plan transition");
        PlanEvidenceTestAccess::transition(p, *f.state, e, E::TransitionKind::PlanPublication, E::Phase::Publication, c, plans, d1_test::id.value());
        require(e.objects[c].parent == plans && e.transitions.front().old_parent == *initial.parent &&
            e.objects[e.target->intent].parent == c, "plan transition preserves children and old relation"); f.pristine();
    }
    { Fixture f; f.publish(); ProjectionIo io; const auto direct = io.open; bool fail = false, observed_pins = false; std::size_t baseline = 0;
        io.open = [&](int fd, const char* name, const open_how& how) {
            if (fail && descriptor_count() >= baseline + 8 && std::string(name) == "." && fdpath(fd) == f.plans()) { observed_pins = true; errno = EMFILE; return -1; }
            return direct(fd, name, how);
        };
        auto p = f.open(io); baseline = descriptor_count(); fail = true;
        const auto e = error([&] { PlanEvidenceTestAccess::acquire(p, *f.state); }, C::Io, O::NotMutated);
        require(observed_pins && e.error == EMFILE && descriptor_count() == baseline, "acquisition failure releases pins and preserves errno");
        f.pristine();
    }
    { Fixture f; E saved;
        { auto p = f.open(); ensure_plan_namespace(p, *f.state); directory(f.prepared());
            saved = PlanEvidenceTestAccess::acquire(p, *f.state);
            require(saved.foreign.size() == 1 && !saved.foreign[0].published && !saved.foreign[0].interior,
                "private interior remains opaque in evidence");
        }
        for (auto& pin : saved.pins) { struct stat value{}; require(::fstat(pin.get(), &value) == 0, "owned evidence outlives acquiring context"); }
        require(saved.foreign[0].container < saved.objects.size(), "foreign value lifetime after context destruction");
        f.pristine();
    }
    std::size_t small_peak = 0, full_peak = 0, target_peak = 0;
    for (const auto [count, with_target] : {std::pair<std::size_t, bool>{1, false}, {4096, false}, {4096, true}}) {
        Fixture f; auto p = f.open(); ensure_plan_namespace(p, *f.state);
        if (with_target) f.publish();
        for (std::size_t n = 0; n < count - with_target; ++n) {
            char name[33]; std::snprintf(name, sizeof(name), "%032zx", n);
            auto dir = f.plans()/name; directory(dir);
            // Safe shape, deliberately invalid bytes: foreign acquisition MUST NOT parse.
            write(dir/"intent.toml", "not canonical"); write(dir/"plan.toml", "not canonical");
        }
        ProjectionIo io; const auto direct_open = io.open; const auto direct_stat = io.stat; const auto direct_read = io.pread; std::size_t reads = 0;
        io.pread = [&](int fd, void* buffer, size_t size, off_t offset) -> ssize_t { ++reads; return direct_read(fd, buffer, size, offset); };
        bool active = false; std::size_t peak = 0;
        auto measure = [&] { if (active) peak = std::max(peak, descriptor_count()); };
        io.open = [&](int fd, const char* name, const open_how& how) { const int result = direct_open(fd,name,how); measure(); return result; };
        io.stat = [&](int fd, const char* name, int flags, unsigned mask, struct statx* out) { measure(); return direct_stat(fd,name,flags,mask,out); };
        auto q = f.open(io); const auto baseline = descriptor_count(); active = true;
        {
            auto e = PlanEvidenceTestAccess::acquire(q, *f.state, with_target ? std::optional<ActivationId>{d1_test::id} : std::nullopt);
            require(e.foreign.size() == count - with_target && e.pins.size() == (with_target ? 11 : 8) &&
                e.target.has_value() == with_target, "foreign observations without per-container pins/bytes");
            const auto acquisition_reads = reads;
            require(with_target || reads == 0, "foreign acquisition never reads document contents");
            for (auto& foreign : e.foreign) {
                require(foreign.published && foreign.interior && foreign.interior->entries.size() == 2 &&
                    !e.objects[foreign.container].pin, "foreign structure survives enumeration descriptor lifetime");
                for (auto& entry : foreign.interior->entries) require(!e.objects[entry.object].pin, "foreign file values own no descriptors");
            }
            PlanEvidenceTestAccess::compare(q, *f.state, e);
            require(reads == acquisition_reads && e.foreign.front().rechecked &&
                e.same_inventory(*e.foreign.front().interior, *e.foreign.front().rechecked), "reopened foreign evidence without parsing");
            require(peak - baseline <= E::max_acquisition_fds, "fixed acquisition/reopening fd bound at capacity");
        }
        require(descriptor_count() == baseline, "capacity evidence releases descriptors");
        const auto acquisition_peak = peak; peak = baseline;
        require(ensure_plan_namespace(q, *f.state) == O::NotMutated && descriptor_count() == baseline && peak-baseline <= E::max_acquisition_fds,
            "integrated bootstrap fd budget at full combined occupancy and no leaked pins");
        std::cout << "R2-B2 bootstrap fd peak " << peak-baseline << "/15 at " << count << " containers (target=" << with_target << ")\n";
        peak = std::max(acquisition_peak, peak);
        if (with_target) {
            const auto inspected = inspect_activation_plan(q, *f.state, d1_test::id);
            require(inspected.digest == f.expected().digest && descriptor_count() == baseline && peak-baseline <= E::max_acquisition_fds,
                "integrated inspection fd bound at 4096 combined containers");

        }
        if (with_target) target_peak = peak-baseline;
        else if (count == 1) small_peak = peak-baseline; else full_peak = peak-baseline;
        active = false; f.pristine();
    }
    require(small_peak == full_peak, "fd peak independent of container count");
    std::cout << "R1 evidence: identity/binding/target/inventory/transitions/lifetime; fd peaks "
              << small_peak << "/" << full_peak << "/" << target_peak << " for 1/4096/4096-with-target containers, bound " << E::max_acquisition_fds << "\n";
}
void evidence_structural() {
    using E = plan_detail::OperationEvidence;
    auto find = [](const E& e, std::string_view name) {
        for (std::size_t n = 0; n < e.objects.size(); ++n) if (e.objects[n].name == name) return n;
        throw std::runtime_error("missing structural evidence");
    };
    { Fixture f; f.publish(); auto p = f.open();
        auto a = PlanEvidenceTestAccess::acquire(p, *f.state, d1_test::id);
        auto b = PlanEvidenceTestAccess::acquire(p, *f.state, d1_test::id);
        require(!a.same_inventory(a.inventories.front(), b.inventories.front()), "numeric IDs cannot cross invocation scopes");
        const auto id = a.target->intent; const auto original = a.objects[id].metadata;
        const auto size = a.objects.size();
        require(a.add(a.objects[id].parent, "intent.toml", original) == id && a.objects.size() == size, "repeated observation preserves local ID");
        auto replaced = original; ++replaced.stx_ino;
        bool rejected = false; try { a.add(a.objects[id].parent, "intent.toml", replaced); }
        catch (const std::logic_error&) { rejected = true; }
        require(rejected && a.matches(id, original) && a.objects.size() == size, "new observation cannot overwrite anchor");
        auto altered = a.inventories.front(); ++altered.entries.front().object;
        require(!a.same_inventory(a.inventories.front(), altered), "inventory entry IDs stay anchored within scope");
        auto batch = a.inventories.front(); E moved = std::move(a);
        require(moved.same_inventory(batch, moved.inventories.front()), "scope and IDs survive move"); f.pristine();
    }
    for (int perturbation : {0, 1, 2, 3}) {
        const bool private_container = perturbation == 3;
        std::cout << "D4 foreign substitution perturbation " << perturbation << '\n' << std::flush;
        Fixture f; f.publish(); const std::string name(32, '0');
        const auto path = (private_container ? f.plans()/".preparations" : f.plans())/name;
        directory(path);
        if (!private_container) { write(path/"intent.toml", "invalid but foreign"); write(path/"plan.toml", "invalid but foreign"); }
        auto p = f.open(); auto e = PlanEvidenceTestAccess::acquire(p, *f.state, d1_test::id);
        const auto id = e.foreign.front().container; const auto anchor = e.objects[id].metadata;
        const auto baseline = descriptor_count();
        PlanEvidenceTestAccess::compare(p, *f.state, e);
        require(e.matches(id, anchor) && descriptor_count() == baseline, "foreign comparison preserves anchor and releases scratch fds");
        if (private_container) {
            // Contents of private foreign residue remain deliberately opaque.
            write(path/"opaque", "private residue");
            PlanEvidenceTestAccess::compare(p, *f.state, e);
            require(e.matches(id, anchor) && descriptor_count() == baseline,
                "opaque private residue changes do not replace the retained container anchor");
            require(::rename(path.c_str(), (f.base/"retired-private-foreign").c_str()) == 0,
                "retain original private foreign container inode");
            directory(path);
        } else if (perturbation == 1) {
            const auto file = path / "plan.toml";
            struct stat before{};
            require(::stat(file.c_str(), &before) == 0,
                    "stat foreign plan before rewrite");

            write(file, "invalid but foreign"); // Preserve inode, size and bytes.

            const timespec times[2] = {
                {0, UTIME_OMIT},
                {before.st_mtim.tv_sec + 1, before.st_mtim.tv_nsec}
            };
            require(::utimensat(AT_FDCWD, file.c_str(), times, 0) == 0,
                    "force deterministic foreign plan mtime change");
        } else if (perturbation == 2) {
            require(::rename(path.c_str(), (f.base/"retired-foreign").c_str()) == 0, "retain original foreign container inode");
            directory(path); write(path/"intent.toml", "invalid but foreign"); write(path/"plan.toml", "invalid but foreign");
        } else {
            require(::rename((path/"plan.toml").c_str(), (path/"retired").c_str()) == 0, "retain original foreign inode");
            write(path/"plan.toml", "invalid but foreign"); fs::remove(path/"retired");
        }
        const auto rejected = error([&] { PlanEvidenceTestAccess::compare(p, *f.state, e); }, C::Substituted, O::NotMutated);
        require(rejected.error == 0 && e.matches(id, anchor) && descriptor_count() == baseline, "observed foreign change cannot replace original evidence");
        auto fresh = PlanEvidenceTestAccess::acquire(p, *f.state, d1_test::id);
        PlanEvidenceTestAccess::compare(p, *f.state, fresh); f.pristine();
    }
    { Fixture f; f.publish(); const auto foreign = f.plans()/std::string(32, '0'); directory(foreign);
        write(foreign/"intent.toml", "foreign"); write(foreign/"plan.toml", "foreign");
        ProjectionIo io; const auto direct = io.open; bool active = false;
        io.open = [&](int fd, const char* name, const open_how& how) {
            if (active && std::string(name) == std::string(32, '0')) { errno = EMFILE; return -1; }
            return direct(fd, name, how);
        };
        auto p = f.open(io); auto e = PlanEvidenceTestAccess::acquire(p, *f.state, d1_test::id);
        const auto baseline = descriptor_count(); active = true;
        const auto rejected = error([&] { PlanEvidenceTestAccess::compare(p, *f.state, e); }, C::Io, O::NotMutated);
        require(rejected.error == EMFILE && descriptor_count() == baseline && !e.foreign.front().rechecked,
            "reopening EMFILE preserves owned anchors, errno and fd budget");
        active = false; PlanEvidenceTestAccess::compare(p, *f.state, e); f.pristine();
    }
    for (int failure : {0, 1, 2}) {
        Fixture f; f.publish(); ProjectionIo io; bool active = false; unsigned synced = 0;
        const auto direct = io.fsync;
        io.fsync = [&](int fd) {
            if (active) {
                ++synced;
                if (failure == 1) { errno = EIO; return -1; }
                const int result = direct(fd);
                if (failure == 2) {
                    // D4 model P: retain the same inode, size and bytes,
                    // but force an observable metadata change after fsync.
                    const auto file = f.published() / "plan.toml";
                    struct stat before{};
                    require(::stat(file.c_str(), &before) == 0,
                        "stat plan before post-fsync perturbation");
                    write(file, serialize(d1_test::plan()));
                    const timespec times[2] = {
                        {0, UTIME_OMIT},
                        {before.st_mtim.tv_sec + 1, before.st_mtim.tv_nsec}
                    };
                    require(::utimensat(AT_FDCWD, file.c_str(), times, 0) == 0,
                        "force deterministic post-fsync plan mtime change");
                }
                return result;
            }
            return direct(fd);
        };
        auto p = f.open(io); auto e = PlanEvidenceTestAccess::acquire(p, *f.state, d1_test::id);
        active = true;
        if (failure == 0) {
            PlanEvidenceTestAccess::barrier(p, *f.state, e, e.target->plan);
            require(synced == 1 && e.barriers.size() == 1 && E::same_identity(e.barriers.front().instance, e.objects[e.target->plan].metadata),
                "only real verified successful fsync records barrier");
        } else {
            const auto rejected = error([&] { PlanEvidenceTestAccess::barrier(p, *f.state, e, e.target->plan); },
                failure == 1 ? C::Durability : C::Substituted, O::OutcomeUncertain);
            require(synced == 1 && e.barriers.empty() && rejected.error == (failure == 1 ? EIO : 0),
                "failed fsync or post-fsync validation cannot record barrier");
        }
        active = false; f.pristine();
    }
    { Fixture f; f.publish(); auto p = f.open(); auto e = PlanEvidenceTestAccess::acquire(p, *f.state, d1_test::id);
        const auto container = e.target->container, plans = find(e, "plans"); const auto original = e.objects[container].metadata;
        const auto rejected = error([&] { PlanEvidenceTestAccess::transition(p, *f.state, e,
            E::TransitionKind::BootstrapPublication, E::Phase::Bootstrap, container, plans, d1_test::id.value()); }, C::Io, O::NotMutated);
        require(rejected.error == 0 && e.transitions.empty() && e.matches(container, original), "invalid transition cannot alter anchor"); f.pristine();
    }
}
void evidence_statx_fields() {
    using E = plan_detail::OperationEvidence;
    unsigned cases = 0;
    for (unsigned missing : {STATX_SIZE, STATX_MTIME, STATX_CTIME})
        for (bool named : {true, false}) for (bool after : {false, true}) {
            Fixture f; f.publish(); ProjectionIo io; const auto direct = io.stat;
            bool active = false; unsigned fsyncs = 0, omissions = 0, named_observations = 0; int file_fd = -1, parent_fd = -1;
            io.stat = [&](int fd, const char* leaf, int flags, unsigned mask, struct statx* out) {
                const int result = direct(fd, leaf, flags, mask, out);
                const bool target = named ? fd == parent_fd && std::string(leaf) == "plan.toml" && !(flags & AT_EMPTY_PATH) :
                    fd == file_fd && std::string(leaf).empty() && (flags & AT_EMPTY_PATH);
                if (!result && active && target && (after ? fsyncs != 0 : fsyncs == 0)) {
                    require((mask & missing) != 0, "mandatory statx field actually requested");
                    out->stx_mask &= ~missing; ++omissions; // Keep values unchanged: the exact audit defect.
                }
                return result;
            };
            io.fsync = [&](int fd) { ++fsyncs; return ::fsync(fd); };
            auto p = f.open(io); auto e = PlanEvidenceTestAccess::acquire(p, *f.state, d1_test::id);
            file_fd = e.pins[*e.objects[e.target->plan].pin].get();
            parent_fd = e.pins[*e.objects[e.target->container].pin].get();
            const auto anchor = e.objects[e.target->plan].metadata;
            const auto baseline = descriptor_count(); active = true;
            const auto rejected = error([&] { PlanEvidenceTestAccess::barrier(p, *f.state, e, e.target->plan); }, C::UnsafeEntry, O::OutcomeUncertain);
            active = false;
            require(omissions != 0 && rejected.error == 0 && fsyncs == (after ? 1u : 0u) && e.barriers.empty(),
                "missing statx field rejects with exact errno/outcome/fsync count and no certificate");
            require(descriptor_count() == baseline && e.matches(e.target->plan, anchor), "incomplete observation preserves descriptors and original anchor");
            f.pristine(); ++cases;
        }
    require(cases == 12, "complete SIZE/MTIME/CTIME named/held before/after matrix");
    // Value-only consumers and foreign acquisition must also reject incomplete evidence.
    { Fixture f; f.publish(); auto p = f.open(); auto e = PlanEvidenceTestAccess::acquire(p, *f.state, d1_test::id);
        auto invalid = e.objects[e.target->plan].metadata; invalid.stx_mask &= ~STATX_SIZE;
        const auto size = e.objects.size();
        auto rejected = error([&] { e.add(e.objects[e.target->plan].parent, "plan.toml", invalid); }, C::UnsafeEntry, O::NotMutated);
        require(rejected.error == 0 && e.objects.size() == size, "cannot store incomplete anchor");
        rejected = error([&] { e.matches(e.target->plan, invalid); }, C::UnsafeEntry, O::NotMutated);
        require(rejected.error == 0, "cannot compare unavailable field values"); f.pristine();
    }
    std::cout << "R2-A.1: 12 missing-field cases; UnsafeEntry/errno=0/OutcomeUncertain; fsyncs before=0 after=1, recorded=0\n";
}
void inspection_evidence() {
    for (const auto mode : {"valid", "generation-binding", "projection-binding", "intent-change", "plan-change",
                           "identical-file", "identical-plans", "foreign-insertion", "foreign-file", "emfile"}) {
        Fixture f; f.publish(); const auto foreign = f.plans()/std::string(32, '0'); directory(foreign);
        write(foreign/"intent.toml", "invalid foreign bytes"); write(foreign/"plan.toml", "invalid foreign bytes");
        ProjectionIo io; const auto direct_read = io.pread; const auto direct_open = io.open;
        bool active = false, changed = false, concluding = false; unsigned reads = 0, fsyncs = 0; std::size_t peak = 0;
        const std::string scenario(mode);
        io.fsync = [&](int fd) { ++fsyncs; return ::fsync(fd); };
        const auto direct_stat = io.stat;
        auto measure = [&] { if (active) peak = std::max(peak, descriptor_count()); };
        io.stat = [&](int fd, const char* name, int flags, unsigned mask, struct statx* out) { measure(); return direct_stat(fd,name,flags,mask,out); };
        io.open = [&](int fd, const char* name, const open_how& how) {
            if (active && concluding && scenario == "emfile" && std::string(name) == std::string(32, '0')) {
                changed = true; errno = EMFILE; return -1;
            }
            const int result = direct_open(fd,name,how); measure(); return result;
        };
        io.pread = [&](int fd, void* bytes, size_t size, off_t offset) -> ssize_t {
            const auto path = fdpath(fd);
            require(path == (f.published()/"intent.toml") || path == (f.published()/"plan.toml") ||
                path == (f.admin()/"authority.toml") || path == (f.base/"generations/.athena-projection-authority.toml") ||
                path.starts_with((f.base/"retired-plans").native()) || path == (f.base/"retired-file"), "inspection never reads foreign documents");
            if (active && offset == 0 && path == (f.published()/"plan.toml") && ++reads == 2) {
                concluding = true;
                if (scenario == "generation-binding" || scenario == "projection-binding") {
                    auto b = f.binding; b.projection_id = ProjectionId(std::string(32, '0'));
                    write(scenario == "generation-binding" ? f.base/"generations/.athena-projection-authority.toml" : f.admin()/"authority.toml", serialize(b)); changed = true;
                } else if (scenario == "intent-change" || scenario == "plan-change") {
                    const bool intent = scenario == "intent-change";
                    write(f.published()/(intent ? "intent.toml" : "plan.toml"), (intent ? serialize(f.intent()) : serialize(d1_test::plan())) + "\n"); changed = true;
                } else if (scenario == "identical-file") {
                    require(::rename((f.published()/"plan.toml").c_str(), (f.base/"retired-file").c_str()) == 0, "retain identical predecessor");
                    write(f.published()/"plan.toml", serialize(d1_test::plan())); changed = true;
                } else if (scenario == "identical-plans") {
                    require(::rename(f.plans().c_str(), (f.base/"retired-plans").c_str()) == 0, "retain plans predecessor");
                    fs::copy(f.base/"retired-plans", f.plans(), fs::copy_options::recursive); changed = true;
                } else if (scenario == "foreign-insertion") { write(foreign/"status.toml", "injected"); changed = true; }
                else if (scenario == "foreign-file") {
                    const auto file = foreign / "plan.toml";
                    struct stat before{};
                    require(::stat(file.c_str(), &before) == 0,
                            "stat foreign file before inspection mutation");

                    write(file, "changed foreign bytes");

                    const timespec times[2] = {
                        {0, UTIME_OMIT},
                        {before.st_mtim.tv_sec + 1, before.st_mtim.tv_nsec}
                    };
                    require(::utimensat(AT_FDCWD, file.c_str(), times, 0) == 0,
                            "force foreign inspection mtime change");
                    changed = true;
                }
            }
            return direct_read(fd,bytes,size,offset);
        };
        auto p = f.open(io); const auto baseline = descriptor_count(); active = true;
        if (scenario == "valid") {
            const auto document = inspect_activation_plan(p, *f.state, d1_test::id);
            require(document.digest == f.expected().digest && !changed, "canonical inspection from shared evidence");
        } else {
            const auto rejected = error([&] { inspect_activation_plan(p, *f.state, d1_test::id); },
                scenario == "emfile" ? C::Io : scenario.ends_with("binding") ? C::AuthorityMismatch : C::Substituted, O::NotMutated);
            require(changed && rejected.error == (scenario == "emfile" ? EMFILE : 0), "controlled inspection rejection has exact cause/errno/outcome");
        }
        active = false;
        require(fsyncs == 0 && descriptor_count() == baseline && peak-baseline <= plan_detail::OperationEvidence::max_acquisition_fds,
            "inspection has no barriers, leaks or unbounded fd ownership");
        if (scenario.ends_with("binding")) {
            const auto record = scenario == "generation-binding" ? f.base/"generations/.athena-projection-authority.toml" : f.admin()/"authority.toml";
            write(record, serialize(f.binding)); require(::lstat(record.c_str(), &f.protected_files.at(record)) == 0, "fixture-only binding restoration");
        }
        if (scenario == "foreign-insertion") require(fs::exists(foreign/"status.toml"), "foreign injected residue not cleaned");
        if (scenario == "identical-plans") {
            auto q = f.open(); require(inspect_activation_plan(q, *f.state, d1_test::id).digest == f.expected().digest, "fresh inspection accepts current identical namespace");
        }
        f.pristine();
    }
    for (unsigned bit : {STATX_SIZE, STATX_MTIME, STATX_CTIME}) for (bool named : {false, true}) {
        Fixture f; f.publish(); ProjectionIo io; const auto direct_stat = io.stat; const auto direct_read = io.pread;
        bool active = false, concluding = false; unsigned reads = 0, fsyncs = 0, omissions = 0;
        io.pread = [&](int fd, void* data, size_t n, off_t offset) -> ssize_t {
            if (active && offset == 0 && fdpath(fd) == (f.published()/"plan.toml") && ++reads == 2) concluding = true;
            return direct_read(fd,data,n,offset);
        };
        io.stat = [&](int fd, const char* leaf, int flags, unsigned mask, struct statx* out) {
            const int result = direct_stat(fd,leaf,flags,mask,out);
            const bool target = named ? !(flags & AT_EMPTY_PATH) && fdpath(fd) == f.published() && std::string(leaf) == "plan.toml" :
                (flags & AT_EMPTY_PATH) && fdpath(fd) == (f.published()/"plan.toml");
            if (!result && concluding && target) { require((mask & bit) != 0, "inspection requests mandatory field"); out->stx_mask &= ~bit; ++omissions; }
            return result;
        };
        io.fsync = [&](int fd) { ++fsyncs; return ::fsync(fd); };
        auto p = f.open(io); const auto baseline = descriptor_count(); active = true;
        const auto rejected = error([&] { inspect_activation_plan(p, *f.state, d1_test::id); }, C::UnsafeEntry, O::NotMutated);
        require(omissions && rejected.error == 0 && fsyncs == 0 && descriptor_count() == baseline, "inspection missing named/held statx fields reject without mutation or fd leak");
        active = false; f.pristine();
    }
    for (unsigned bit : {STATX_SIZE, STATX_MTIME, STATX_CTIME})
        for (unsigned stage = 0; stage < 4; ++stage) {
        Fixture f; f.publish(); const auto foreign = f.plans()/std::string(32, '0'); directory(foreign);
        write(foreign/"intent.toml", "opaque foreign intent"); write(foreign/"plan.toml", "opaque foreign plan");
        for (const auto& path : {foreign, foreign/"intent.toml", foreign/"plan.toml"}) {
            struct stat before{}; require(::lstat(path.c_str(), &before) == 0, "foreign initial metadata");
            f.protected_files.emplace(path, before);
        }
        ProjectionIo io; const auto direct_stat = io.stat; const auto direct_open = io.open;
        const auto direct_read = io.pread;
        bool active = false, concluding = false, reopened = false;
        unsigned reads = 0, named = 0, held = 0, omissions = 0, fsyncs = 0;
        std::size_t peak = 0;
        auto measure = [&] { if (active) peak = std::max(peak, descriptor_count()); };
        io.pread = [&](int fd, void* bytes, size_t n, off_t offset) -> ssize_t {
            require(fdpath(fd) != foreign/"intent.toml" && fdpath(fd) != foreign/"plan.toml", "foreign comparison never reads documents");
            if (active && offset == 0 && fdpath(fd) == f.published()/"plan.toml" && ++reads == 2) concluding = true;
            return direct_read(fd, bytes, n, offset);
        };
        io.open = [&](int fd, const char* leaf, const open_how& how) {
            // The concluding binding read follows the namespace batch and precedes foreign reopens.
            if (concluding && fdpath(fd) == f.admin() && std::string(leaf) == "authority.toml") reopened = true;
            const int result = direct_open(fd, leaf, how); measure(); return result;
        };
        io.stat = [&](int fd, const char* leaf, int flags, unsigned mask, struct statx* out) {
            measure(); const int result = direct_stat(fd, leaf, flags, mask, out);
            if (!result && reopened && !omissions) {
                const bool is_named = !(flags & AT_EMPTY_PATH) && fdpath(fd) == f.plans() && std::string(leaf) == std::string(32, '0');
                const bool is_held = (flags & AT_EMPTY_PATH) && fdpath(fd) == foreign;
                if (is_named) ++named;
                if (is_held) ++held;
                // Named observations 1/3/5 are existence probes. 2 is before open,
                // 4 belongs to its first check, 6 to the check after interior comparison.
                const bool omit = stage == 0 ? is_named && named == 2 :
                    stage == 1 ? is_held && held == 1 :
                    stage == 2 ? is_named && named == 6 : is_held && named == 6;
                if (omit) { require((mask & bit) && (out->stx_mask & bit), "foreign required field requested and returned"); out->stx_mask &= ~bit; ++omissions; }
            }
            return result;
        };
        io.fsync = [&](int fd) { ++fsyncs; return ::fsync(fd); };
        auto p = f.open(io); const auto baseline = descriptor_count(); active = true;
        const auto rejected = error([&] { inspect_activation_plan(p, *f.state, d1_test::id); }, C::UnsafeEntry, O::NotMutated);
        require(omissions == 1 && rejected.error == 0 && fsyncs == 0 && descriptor_count() == baseline && peak-baseline <= 15,
            "single foreign mask omission rejects with exact error, no barriers/leak, bounded descriptors");
        active = false;
        require(fs::is_directory(foreign) && inode(foreign/"intent.toml") && inode(foreign/"plan.toml"), "foreign objects retained");
        auto bytes = [](const fs::path& path) { std::ifstream file(path); return std::string(std::istreambuf_iterator<char>(file), {}); };
        require(bytes(foreign/"intent.toml") == "opaque foreign intent" && bytes(foreign/"plan.toml") == "opaque foreign plan" &&
            !fs::exists(foreign/"status.toml"), "foreign content and inventory unchanged");
        require(inspect_activation_plan(p, *f.state, d1_test::id).digest == f.expected().digest && fsyncs == 0 && descriptor_count() == baseline,
            "fresh valid inspection succeeds without mutation or leaks");
        f.pristine();
    }
    std::cout << "R2-B1.1: 12 single foreign statx omissions; UnsafeEntry/0/NotMutated, zero fsync, no leaks\n";
    std::cout << "R2-B1 inspection: 10 scenarios and 6 missing-field cases; no fsync, exact typed outcomes, preserved foreign state\n";
}
// N: a separate capacity shard keeps the ordinary test's existing timeout
// without dropping its full-occupancy publication checks or sampling fd peaks.
void publication_capacity() {
    using E = plan_detail::OperationEvidence;
    Fixture f; f.publish();
    for (std::size_t n = 0; n < 4095; ++n) {
        char name[33]; std::snprintf(name, sizeof(name), "%032zx", n);
        const auto container = f.plans()/name; directory(container);
        write(container/"intent.toml", "not canonical"); write(container/"plan.toml", "not canonical");
    }
    ProjectionIo io; const auto open = io.open; const auto stat = io.stat; const auto read = io.pread;
    bool active = false; std::size_t peak = 0, foreign_reads = 0;
    auto measure = [&] { if (active) peak = std::max(peak, descriptor_count()); };
    io.open = [&](int fd, const char* name, const open_how& how) { const auto rc = open(fd,name,how); measure(); return rc; };
    io.stat = [&](int fd, const char* name, int flags, unsigned mask, struct statx* out) { measure(); return stat(fd,name,flags,mask,out); };
    io.pread = [&](int fd, void* buffer, size_t size, off_t offset) -> ssize_t {
        if (active && fs::path(fdpath(fd)).parent_path() != f.published()) ++foreign_reads;
        return read(fd,buffer,size,offset);
    };
    auto p = f.open(io); const auto baseline = descriptor_count(); active = true;
    { E e; const auto result = PlanEvidenceTestAccess::publish(p, *f.state, f.intent(), d1_test::plan(), e);
      require(result.publication == PlanPublication::AlreadyPublished && result.outcome == O::NotMutated &&
          e.pins.size() == 11 && e.barriers.size() == 11 && e.foreign.size() == 4096 &&
          peak-baseline <= E::max_acquisition_fds && descriptor_count() == baseline+11 && foreign_reads == 0,
          "full-capacity publication conserves every original observation, never parses foreign bytes, fixed fd budget"); }
    require(descriptor_count() == baseline, "full-capacity publication releases all 11 pins");
    active = false; f.pristine();
    std::cout << "R2-B3: 4096 containers, publication pins 11, additional fd peak " << peak-baseline << "/15, baseline " << baseline << ", observed process peak " << peak << "\n";
}
// N: real publication runner; production-conforming I/O or explicit fsync /
// rename failures. Evidence is moved out only after that runner exits.
void publication_evidence() {
    using E = plan_detail::OperationEvidence;
    std::size_t maximum = 0;
    for (int scenario = 0; scenario < 5; ++scenario) {
        const unsigned bootstrap_count = scenario < 2 ? 8 : 4;
        const bool reused = scenario == 3, published = scenario == 4;
        const unsigned total = bootstrap_count + (published ? 7 : reused ? 12 : 14);
        for (unsigned failure = 0; failure <= total; ++failure) {
            Fixture f;
            if (scenario == 1) { directory(f.admin()/".plans-bootstrap"); directory(f.admin()/".plans-bootstrap/.preparations"); }
            if (scenario >= 2) { auto p = f.open(); ensure_plan_namespace(p, *f.state); }
            if (reused) { directory(f.prepared()); write(f.prepared()/"intent.toml", serialize(f.intent())); write(f.prepared()/"plan.toml", serialize(d1_test::plan())); }
            if (published) f.publish();
            ProjectionIo io; const auto direct_sync = io.fsync; const auto direct_open = io.open; const auto direct_stat = io.stat;
            unsigned fsyncs = 0; std::vector<struct statx> instances; std::vector<std::string> paths;
            bool active = false; std::size_t peak = 0;
            auto measure = [&] { if (active) peak = std::max(peak, descriptor_count()); };
            io.open = [&](int fd, const char* name, const open_how& how) { auto rc = direct_open(fd, name, how); measure(); return rc; };
            io.stat = [&](int fd, const char* name, int flags, unsigned mask, struct statx* out) { measure(); return direct_stat(fd, name, flags, mask, out); };
            io.fsync = [&](int fd) {
                measure(); ++fsyncs;
                if (fsyncs == failure) { errno = EIO; return -1; }
                struct statx value{}; require(::statx(fd, "", AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS|STATX_MNT_ID, &value) == 0, "barrier oracle instance");
                auto rc = direct_sync(fd); if (!rc) { instances.push_back(value); paths.push_back(fdpath(fd)); } return rc;
            };
            auto p = f.open(io); const auto baseline = descriptor_count(); active = true;
            E e;
            if (failure) {
                auto rejected = error([&] { PlanEvidenceTestAccess::publish(p, *f.state, f.intent(), d1_test::plan(), e); }, C::Durability, O::OutcomeUncertain);
                require(rejected.error == EIO && fsyncs == failure && e.barriers.size() == failure-1, "failed fsync cannot enter actual publication registry");
            } else {
                const auto result = PlanEvidenceTestAccess::publish(p, *f.state, f.intent(), d1_test::plan(), e);
                require(result.publication == (published ? PlanPublication::AlreadyPublished : PlanPublication::Published) &&
                    result.outcome == (published ? O::NotMutated : O::MutatedDurably), "publication disposition and cumulative outcome");
                require(fsyncs == total && e.barriers.size() == total && e.pins.size() == 11 && e.target, "one evidence owner retains all publication pins/barriers");
                require(e.transitions.size() == (scenario < 2 ? 1u : 0u) + !published, "actual own renames recorded exactly once");
                require(e.inventories.front().scope == e.scope && std::all_of(e.inventories.begin(), e.inventories.end(), [&](const auto& batch) { return batch.scope == e.scope; }), "one acquisition scope across bootstrap and publication");
                if (scenario < 2) {
                    const auto& t = e.transitions.front();
                    require(t.kind == E::TransitionKind::BootstrapPublication && t.old_name == ".plans-bootstrap" && t.new_name == "plans" &&
                        E::same_identity(e.objects[t.object].metadata, t.instance), "bootstrap origin survives publication");
                    require(std::any_of(e.inventories.begin(), e.inventories.end(), [&](const auto& batch) {
                        return std::any_of(batch.entries.begin(), batch.entries.end(), [&](const auto& entry) { return entry.object == t.object && entry.observation.name == ".plans-bootstrap"; });
                    }), "pre-rename bootstrap inventory conserved");
                }
            }
            require(e.pins.size() <= 11 && peak-baseline <= 15 && descriptor_count() == baseline+e.pins.size(), "publication fixed fd budget and no parallel owners");
            maximum = std::max(maximum, peak-baseline);
            require(e.barriers.size() == instances.size(), "every successful verified fsync has its registry entry");
            std::vector<std::string> ordered_paths;
            auto append_namespace = [&](const fs::path& parent) {
                for (const auto& path : {parent/".preparations", parent, f.admin(), f.base/"projection"}) ordered_paths.push_back(path.native());
            };
            if (scenario < 2) append_namespace(f.admin()/".plans-bootstrap");
            append_namespace(f.plans());
            const auto container_path = published ? f.published() : f.prepared();
            if (!published && !reused) { ordered_paths.push_back((container_path/"intent.toml").native()); ordered_paths.push_back((container_path/"plan.toml").native()); }
            for (const auto& path : {container_path/"intent.toml", container_path/"plan.toml", container_path,
                    published ? f.plans() : f.plans()/".preparations", published ? f.plans()/".preparations" : f.plans(), f.admin(), f.base/"projection"}) ordered_paths.push_back(path.native());
            if (!published) for (const auto& path : {f.published(), f.plans(), f.plans()/".preparations", f.admin(), f.base/"projection"}) ordered_paths.push_back(path.native());
            require(ordered_paths.size() == total && paths.size() <= ordered_paths.size() &&
                std::equal(paths.begin(), paths.end(), ordered_paths.begin()), "real fsync order equals normative bootstrap/preparation/publication/retry order");
            for (std::size_t n = 0; n < e.barriers.size(); ++n) {
                const auto& b = e.barriers[n];
                require(E::same_identity(b.instance, instances[n]) && E::same_identity(b.instance, e.objects.at(b.object).metadata), "barrier belongs to actual synchronized instance and original anchor");
                require(e.objects.at(b.object).pin && E::same_identity(b.instance, [&] { struct statx v{}; require(::statx(e.pins.at(*e.objects[b.object].pin).get(), "", AT_EMPTY_PATH, STATX_BASIC_STATS|STATX_MNT_ID, &v) == 0, "live barrier pin"); return v; }()), "no stale barrier pin");
                const auto expected_stage = n < bootstrap_count ? E::Phase::Bootstrap : published ? E::Phase::Recovery :
                    n < total-5 ? E::Phase::Preparation : E::Phase::Publication;
                require(b.phase == expected_stage, "barrier phase preserved through bootstrap helper return");
            }
            const unsigned done = failure ? failure-1 : total;
            const bool renamed = !published && done >= total-5;
            require(!published || fs::exists(f.published()), "retry retains publication");
            if (!published && done >= bootstrap_count+2 && !reused) require(fs::exists((renamed ? f.published() : f.prepared())/"plan.toml"), "complete preparation/publication residue retained");
            if (renamed) require(!fs::exists(f.prepared()), "rename consumes only own source");
            e = E{}; require(descriptor_count() == baseline, "exported evidence releases all pins after success/exception");
            active = false; f.pristine();
        }
    }
    std::cout << "R2-B3: 5 publication paths, every fsync failure, actual instance/phase registry; fd peak " << maximum << "/15, pins 11\n";
}
void publication_registry_faults() {
    using E = plan_detail::OperationEvidence;
    // P: remove one returned mandatory statx bit immediately after a real
    // successful fsync. The prescribed post-barrier observation exposes it.
    for (const unsigned missing : {STATX_SIZE, STATX_MTIME, STATX_CTIME})
    for (int stage = 0; stage < 4; ++stage)
    for (bool named : {false, true}) {
        Fixture f; auto setup = f.open(); ensure_plan_namespace(setup, *f.state);
        if (stage == 3) f.publish();
        else { directory(f.prepared()); write(f.prepared()/"intent.toml", serialize(f.intent())); write(f.prepared()/"plan.toml", serialize(d1_test::plan())); }
        const unsigned goal = stage == 0 ? 1 : stage == 2 ? 12 : 5;
        const auto object_path = stage == 0 ? f.plans()/".preparations" : stage == 2 ? f.published() :
            (stage == 3 ? f.published() : f.prepared())/"intent.toml";
        ProjectionIo io; const auto sync = io.fsync; const auto stat = io.stat;
        unsigned fsyncs = 0, omissions = 0, named_observations = 0;
        io.fsync = [&](int fd) { ++fsyncs; return sync(fd); };
        io.stat = [&](int fd, const char* name, int flags, unsigned mask, struct statx* out) {
            const auto rc = stat(fd, name, flags, mask, out);
            const auto path = (flags & AT_EMPTY_PATH) ? fs::path(fdpath(fd)) : fs::path(fdpath(fd))/name;
            if (!rc && fsyncs == goal && !omissions && path == object_path && named == !(flags & AT_EMPTY_PATH)) {
                // exists() consumes only success/ENOENT, not any statx fields.
                // Omit from the following actual named metadata observation.
                if (!named || ++named_observations == 2) { out->stx_mask &= ~missing; ++omissions; }
            }
            return rc;
        };
        auto p = f.open(io); const auto baseline = descriptor_count(); E e;
        auto rejected = error([&] { PlanEvidenceTestAccess::publish(p, *f.state, f.intent(), d1_test::plan(), e); }, C::UnsafeEntry, O::OutcomeUncertain);
        require(rejected.error == 0 && omissions == 1 && fsyncs == goal && e.barriers.size() == goal-1,
            "incomplete post-fsync observation cannot record an actual invocation barrier");
        require(e.pins.size() <= 11 && descriptor_count() == baseline+e.pins.size(), "exception has single pin owner");
        if (stage == 2) require(fs::exists(f.published()) && !fs::exists(f.prepared()) && e.transitions.size() == 1, "post-rename mask failure retains transition and publication");
        e = E{}; require(descriptor_count() == baseline, "mask error releases exported evidence"); f.pristine();
    }
    // N: before/after-effect rename error. After-effect EIO must not invent a
    // verified transition or any post-rename barrier; actual residue is retained.
    for (bool bootstrap : {false, true}) for (bool after_effect : {false, true}) {
        Fixture f; ProjectionIo io; const auto rename = io.rename;
        unsigned renames = 0;
        io.rename = [&](int source, const char* old_name, int destination, const char* new_name, unsigned flags) {
            ++renames;
            if (renames == (bootstrap ? 1u : 2u)) {
                if (after_effect) require(rename(source, old_name, destination, new_name, flags) == 0, "real rename before injected EIO");
                errno = EIO; return -1;
            }
            return rename(source, old_name, destination, new_name, flags);
        };
        auto p = f.open(io); const auto baseline = descriptor_count(); E e;
        auto rejected = error([&] { PlanEvidenceTestAccess::publish(p, *f.state, f.intent(), d1_test::plan(), e); }, C::Io, O::OutcomeUncertain);
        require(rejected.error == EIO && e.barriers.size() == (bootstrap ? 4u : 17u) && e.transitions.size() == (bootstrap ? 0u : 1u),
            "rename wrapper error cannot manufacture transition/post-rename barriers");
        if (bootstrap) require(fs::exists(f.plans()) == after_effect && fs::exists(f.admin()/".plans-bootstrap") == !after_effect, "bootstrap rename error residue");
        else require(fs::exists(f.published()) == after_effect && fs::exists(f.prepared()) == !after_effect, "publication rename error residue");
        e = E{}; require(descriptor_count() == baseline, "rename exception releases evidence"); f.pristine();
        const auto retry = f.publish();
        require(retry.publication == (!bootstrap && after_effect ? PlanPublication::AlreadyPublished : PlanPublication::Published) &&
            retry.outcome == (!bootstrap && after_effect ? O::NotMutated : O::MutatedDurably), "fresh invocation resynchronizes real rename residue with exact outcome");
    }
    // N: later exclusive mkdir failure must retain durable bootstrap evidence.
    { Fixture f; ProjectionIo io; const auto mkdir = io.mkdir;
      io.mkdir = [&](int fd, const char* name, mode_t mode) { if (name == d1_test::id.value()) { errno = ENOSPC; return -1; } return mkdir(fd, name, mode); };
      auto p = f.open(io); const auto baseline = descriptor_count(); E e;
      const auto rejected = error([&] { PlanEvidenceTestAccess::publish(p, *f.state, f.intent(), d1_test::plan(), e); }, C::Io, O::MutatedDurably);
      require(rejected.error == ENOSPC && e.barriers.size() == 8 && e.transitions.size() == 1 && !fs::exists(f.prepared()), "ledger preserves completed bootstrap before later mutation");
      e = E{}; require(descriptor_count() == baseline, "durable bootstrap error releases evidence"); f.pristine(); }
    // N: descriptor exhaustion is never a reason to bypass enumeration.
    for (int scenario = 0; scenario < 3; ++scenario) {
        Fixture f;
        if (scenario == 1) { auto q = f.open(); ensure_plan_namespace(q, *f.state); directory(f.prepared()); write(f.prepared()/"intent.toml", serialize(f.intent())); write(f.prepared()/"plan.toml", serialize(d1_test::plan())); }
        if (scenario == 2) f.publish();
        ProjectionIo io; const auto open = io.open; const auto sync = io.fsync; unsigned failures = 0, fsyncs = 0, promoted_opens = 0; bool active = false;
        io.fsync = [&](int fd) { ++fsyncs; return sync(fd); };
        io.open = [&](int fd, const char* name, const open_how& how) {
            if (active && scenario == 2 && fsyncs == 4 && name == d1_test::id.value() && fdpath(fd) == f.plans()) ++promoted_opens;
            if (active && (scenario != 2 || promoted_opens == 2) && std::string(name) == "." && fdpath(fd) == (scenario == 2 ? f.published() : f.prepared()) && !failures) {
                ++failures; errno = EMFILE; return -1;
            }
            return open(fd, name, how);
        };
        auto p = f.open(io); const auto baseline = descriptor_count(); E e;
        active = true;
        const auto rejected = error([&] { PlanEvidenceTestAccess::publish(p, *f.state, f.intent(), d1_test::plan(), e); }, C::Io,
            scenario == 0 ? O::OutcomeUncertain : O::NotMutated);
        require(rejected.error == EMFILE && failures == 1 && e.barriers.size() == (scenario == 0 ? 10u : 4u) &&
            descriptor_count() == baseline+e.pins.size() && e.pins.size() <= 11, "publication EMFILE preserves errno/ledger/registry without fd leak: scenario=" + std::to_string(scenario) +
            " barriers=" + std::to_string(e.barriers.size()) + " pins=" + std::to_string(e.pins.size()));
        require(fs::exists((scenario == 2 ? f.published() : f.prepared())/"plan.toml"), "EMFILE retains complete target residue");
        e = E{}; require(descriptor_count() == baseline, "EMFILE exported evidence destroyed safely"); f.pristine();
    }
    std::cout << "R2-B3: 24 mandatory-field/registry regressions, 4 rename error transitions, durable-bootstrap ledger, 3 EMFILE cases\n";
}
// N/P: the real standalone recovery runner exports evidence only after exit.
void recovery_evidence() {
    using E = plan_detail::OperationEvidence;
    for (unsigned failure = 0; failure <= 7; ++failure) {
        Fixture f; f.publish();
        ProjectionIo io; auto sync = io.fsync; auto open = io.open; auto stat = io.stat;
        bool active = false; std::size_t peak = 0;
        auto measure = [&] { if (active) peak = std::max(peak,descriptor_count()); };
        io.open = [&](int fd,const char* leaf,const open_how& how) { auto rc = open(fd,leaf,how); measure(); return rc; };
        io.stat = [&](int fd,const char* leaf,int flags,unsigned mask,struct statx* out) { measure(); return stat(fd,leaf,flags,mask,out); };
        std::vector<struct statx> actual; std::vector<std::string> paths; unsigned calls = 0;
        io.mkdir = [](int, const char*, mode_t) { throw std::runtime_error("recovery mkdir"); return -1; };
        io.rename = [](int, const char*, int, const char*, unsigned) { throw std::runtime_error("recovery rename"); return -1; };
        io.durable.write = [](int, const void*, size_t) -> ssize_t { throw std::runtime_error("recovery write"); };
        io.fsync = [&](int fd) {
            if (++calls == failure) { errno = EIO; return -1; }
            struct statx v{}; require(::statx(fd, "", AT_EMPTY_PATH, STATX_BASIC_STATS|STATX_MNT_ID, &v) == 0, "recovery oracle stat");
            auto rc = sync(fd); if (!rc) { actual.push_back(v); paths.push_back(relative(f,fd)); } return rc;
        };
        auto p = f.open(io); const auto baseline = descriptor_count(); E e; active = true;
        if (failure) {
            auto rejected = error([&] { PlanEvidenceTestAccess::recover(p, *f.state, f.expected(), e); }, C::Durability, O::OutcomeUncertain);
            require(rejected.error == EIO && calls == failure && e.barriers.size() == failure-1, "recovery partial registry");
        } else {
            auto result = PlanEvidenceTestAccess::recover(p, *f.state, f.expected(), e);
            require(result.publication == PlanPublication::AlreadyPublished && result.outcome == O::NotMutated && calls == 7, "recovery disposition");
        }
        require(e.target && e.target->independent_expected && e.target->independent_expected->digest == f.expected().digest &&
            e.pins.size() == 11 && e.transitions.empty() && e.inventories.size() == 4, "one recovery owner and original batches");
        require(std::all_of(e.inventories.begin(),e.inventories.end(),[&](const auto& b) { return b.phase == E::Phase::Recovery && b.scope == e.scope; }), "recovery batch phase/scope");
        require(actual.size() == e.barriers.size() && std::equal(paths.begin(),paths.end(),recovery_order.begin()), "recovery real order");
        const auto root = *e.find({},f.binding.projection_root), admin = *e.find(root,".athena-activation");
        const auto plans = *e.find(admin,"plans"), prep = *e.find(plans,".preparations");
        const std::vector<E::ObjectId> order{e.target->intent,e.target->plan,e.target->container,plans,prep,admin,root};
        for (std::size_t n = 0; n < actual.size(); ++n) {
            const auto& b = e.barriers[n];
            require(b.phase == E::Phase::Recovery && b.object == order[n] && E::same_identity(actual[n],b.instance) &&
                E::same_identity(b.instance,e.objects[b.object].metadata), "recovery registry actual inode/object/phase");
        }
        require(peak-baseline <= 15,"recovery success/fsync-failure fixed fd bound");
        active = false;
        auto moved = std::move(e); require(descriptor_count() == baseline+11 && moved.inventories.front().scope == moved.scope, "move after success/error retains pins and provenance");
        e = std::move(moved); e = E{}; require(descriptor_count() == baseline, "recovery RAII after move");
        auto fresh = f.open();
        require(recover_activation_plan(fresh, *f.state, f.expected()).outcome == O::NotMutated, "fresh idempotent retry after interruption");
        f.pristine();
    }
    // Missing public target never adopts private documents, including malformed ones.
    for (int state = 0; state < 9; ++state) {
        Fixture f;
        if (state) { auto q = f.open(); ensure_plan_namespace(q,*f.state); }
        if (state == 2 || state == 3) { directory(f.prepared()); write(f.prepared()/"intent.toml",state == 2 ? "opaque partial" : serialize(f.intent()));
            if (state == 3) write(f.prepared()/"plan.toml",serialize(d1_test::plan())); }
        if (state >= 4) f.publish();
        ProjectionIo io; const auto read = io.pread;
        io.pread = [&](int fd,void* b,size_t n,off_t at) -> ssize_t {
            require(fs::path(fdpath(fd)).parent_path() != f.prepared(),"recovery leaves private document bytes opaque");
            return read(fd,b,n,at);
        };
        auto p = f.open(io);
        if (state == 5) { directory(f.admin()/".plans-bootstrap"); directory(f.admin()/".plans-bootstrap/.preparations"); }
        if (state == 6) directory(f.prepared());
        if (state == 7) require(fs::remove(f.published()/"plan.toml"),"incomplete publication fixture");
        if (state == 8) require(::chmod((f.published()/"intent.toml").c_str(),0644) == 0,"unsafe publication fixture");
        auto expected = f.expected(); if (state == 4) expected.digest = PlanDigest(d1_test::digest('c'));
        E e; const auto baseline = descriptor_count();
        auto rejected = error([&] { PlanEvidenceTestAccess::recover(p,*f.state,expected,e); },
            state < 4 ? C::MissingPlan : state == 4 ? C::IdentityMismatch : state == 5 ? C::BootstrapConflict : state == 6 ? C::PreparationConflict : state == 7 ? C::NamespaceIncomplete : C::UnsafeEntry, O::NotMutated);
        require(rejected.error == 0 && e.barriers.empty() && e.transitions.empty(), "recovery state classification before barriers");
        e = E{}; require(descriptor_count() == baseline,"state rejection RAII"); f.pristine();
    }
    // All required bits, named and held, exposed before and after the first sync.
    for (unsigned bit : {STATX_TYPE,STATX_MODE,STATX_UID,STATX_INO,STATX_NLINK,STATX_MNT_ID,STATX_SIZE,STATX_MTIME,STATX_CTIME})
    for (bool named : {false,true}) for (bool after : {false,true}) for (bool directory_target : {false,true}) {
        Fixture f; f.publish(); ProjectionIo io; auto stat = io.stat; auto read = io.pread; auto sync = io.fsync;
        bool reading = false; unsigned calls = 0, omissions = 0, named_checks = 0;
        const auto target_path = directory_target ? f.plans()/".preparations" : f.published()/"intent.toml";
        io.pread = [&](int fd,void* b,size_t n,off_t at) -> ssize_t { if (fdpath(fd) == f.published()/"intent.toml") reading = true; return read(fd,b,n,at); };
        io.fsync = [&](int fd) { ++calls; return sync(fd); };
        io.stat = [&](int fd,const char* leaf,int flags,unsigned mask,struct statx* out) {
            auto rc = stat(fd,leaf,flags,mask,out);
            const auto path = flags & AT_EMPTY_PATH ? fs::path(fdpath(fd)) : fs::path(fdpath(fd))/leaf;
            if (!rc && !omissions && reading && calls == unsigned(after) && path == target_path && named == !(flags&AT_EMPTY_PATH)) {
                if (!named || ++named_checks == 2) { out->stx_mask &= ~bit; ++omissions; }
            }
            return rc;
        };
        auto p = f.open(io); const auto baseline = descriptor_count(); E e;
        error([&] { PlanEvidenceTestAccess::recover(p,*f.state,f.expected(),e); }, C::UnsafeEntry, after ? O::OutcomeUncertain : O::NotMutated);
        require(omissions == 1 && calls == unsigned(after) && e.barriers.empty(),"recovery incomplete mask has no barrier certificate");
        require(std::all_of(e.inventories.begin(),e.inventories.end(),[](const auto& b) { return b.phase == E::Phase::Recovery; }),"partial acquisition retains Recovery inventory phase");
        e = E{}; require(descriptor_count() == baseline,"mask RAII"); f.pristine();
    }
    // EMFILE in acquisition and after the final sync's concluding enumeration.
    for (bool late : {false,true}) {
        Fixture f; f.publish(); ProjectionIo io; auto open = io.open; auto sync = io.fsync;
        bool active = false; unsigned calls = 0, failures = 0; std::size_t baseline = 0;
        io.fsync = [&](int fd) { ++calls; return sync(fd); };
        io.open = [&](int fd,const char* leaf,const open_how& how) {
            if (active && !failures && calls == (late ? 7u : 0u) && (late || descriptor_count() >= baseline+8) && std::string(leaf) == "." && fdpath(fd) == f.plans()) {
                ++failures; errno = EMFILE; return -1;
            }
            return open(fd,leaf,how);
        };
        auto p = f.open(io); baseline = descriptor_count(); E e; active = true;
        auto rejected = error([&] { PlanEvidenceTestAccess::recover(p,*f.state,f.expected(),e); }, C::Io,late ? O::OutcomeUncertain : O::NotMutated);
        require(rejected.error == EMFILE && failures == 1 && e.barriers.size() == (late ? 7u : 0u),"recovery EMFILE registry/outcome");
        require(std::all_of(e.inventories.begin(),e.inventories.end(),[](const auto& b) { return b.phase == E::Phase::Recovery; }),"EMFILE preserves acquired inventory phase");
        e = E{}; require(descriptor_count() == baseline,"EMFILE RAII"); f.pristine();
    }
    std::cout << "R2-B4: seven recovery fsync faults, nine states, 72 file/directory statx omissions, two EMFILE cases; move/RAII/registry verified\n";
}
void recovery_substitutions() {
    using E = plan_detail::OperationEvidence;
    for (const auto target : {"generation", "store", "root", "generation-binding", "admin-binding", "foreign", "foreign-intent", "foreign-plan"}) {
        Fixture f; f.publish(); const auto foreign = f.plans()/std::string(32,'0');
        directory(foreign); write(foreign/"intent.toml","opaque intent"); write(foreign/"plan.toml","opaque plan");
        fs::path path;
        const std::string kind(target);
        if (kind == "generation") path = f.base/"generations";
        else if (kind == "store") path = f.base/"store";
        else if (kind == "root") path = f.base/"projection";
        else if (kind == "generation-binding") path = f.base/"generations/.athena-projection-authority.toml";
        else if (kind == "admin-binding") path = f.admin()/"authority.toml";
        else if (kind == "foreign") path = foreign;
        else path = foreign/(kind == "foreign-intent" ? "intent.toml" : "plan.toml");
        const auto original = inode(path); const auto retired = f.base/"retired-recovery";
        ProjectionIo io; auto sync = io.fsync; auto read = io.pread; bool changed = false; unsigned calls = 0;
        io.pread = [&](int fd,void* b,size_t n,off_t at) -> ssize_t {
            require(fs::path(fdpath(fd)).parent_path() != foreign,"recovery never reads foreign substitution bytes"); return read(fd,b,n,at);
        };
        io.fsync = [&](int fd) {
            if (++calls == 1) {
                fs::rename(path,retired); fs::copy(retired,path,fs::copy_options::recursive);
                require(inode(path) != original,"replacement has distinct instance"); changed = true;
                // Fixture injection replaces protected objects; observe the injected
                // instances, then prove the recovery never changes them again.
                for (auto& [protected_path, observation] : f.protected_files)
                    require(::lstat(protected_path.c_str(),&observation) == 0,"injected protected observation");
            }
            return sync(fd);
        };
        auto p = f.open(io); const auto baseline = descriptor_count(); E e;
        const auto rejected = error([&] { PlanEvidenceTestAccess::recover(p,*f.state,f.expected(),e); },
            kind.ends_with("binding") ? C::AuthorityMismatch : C::Substituted,O::OutcomeUncertain);
        require(rejected.error == 0 && changed && inode(retired) == original && e.transitions.empty(),"substitution retains original and injected residue");
        const auto object = std::find_if(e.objects.begin(),e.objects.end(),[&](const auto& o) { return o.metadata.stx_ino == original; });
        require(object != e.objects.end(),"recovery original anchor survives rejection");
        for (const auto& b : e.barriers) require(E::same_identity(b.instance,e.objects[b.object].metadata),"substitution cannot borrow retired barrier");
        e = E{}; require(descriptor_count() == baseline,"substitution RAII"); f.pristine();
    }
    std::cout << "R2-B4: eight authority/binding/foreign substitutions; original anchors, opaque bytes and residue retained\n";
}
// Function-pointer syscall injection is scoped to the actual invocation.
struct CloexecFailure {
    std::string path;
    bool active = false;
    unsigned syncs = 0, after = 0, hits = 0;
    int failed_fd = -1, number = EIO;
    static thread_local CloexecFailure* current;
    CloexecFailure() { require(!current,"no nested CLOEXEC injection"); current = this; }
    ~CloexecFailure() { current = nullptr; }
    static int inject(int fd) {
        auto* s = current;
        if (s && s->active && s->syncs >= s->after && !s->hits) {
            char link[64], path[4096];
            std::snprintf(link,sizeof(link),"/proc/self/fd/%d",fd);
            const auto n = ::readlink(link,path,sizeof(path));
            if (n >= 0 && static_cast<std::size_t>(n) == s->path.size() &&
                !std::memcmp(path,s->path.data(),static_cast<std::size_t>(n))) {
                ++s->hits; s->failed_fd = fd; errno = s->number; return -1;
            }
        }
        return ::fcntl(fd,F_SETFD,FD_CLOEXEC);
    }
};
thread_local CloexecFailure* CloexecFailure::current = nullptr;
void recovery_cloexec_errors() {
    using E = plan_detail::OperationEvidence;
    for (const auto [late, number] : {std::pair{false,EIO},std::pair{true,EIO},std::pair{false,EACCES}}) {
        Fixture f; f.publish(); CloexecFailure injection;
        injection.path = (late ? f.admin()/"authority.toml" : f.base/"generations").native();
        injection.after = late ? 1 : 0; injection.number = number;
        ProjectionIo io; io.durable.cloexec = CloexecFailure::inject;
        std::vector<std::string> synced; auto sync = io.fsync;
        io.fsync = [&](int fd) { const auto rc = sync(fd); if (!rc) { ++injection.syncs; synced.push_back(relative(f,fd)); } return rc; };
        auto p = f.open(io); const auto baseline = descriptor_count(); E e; injection.active = true;
        const auto rejected = error([&] { PlanEvidenceTestAccess::recover(p,*f.state,f.expected(),e); },
            C::Io,late ? O::OutcomeUncertain : O::NotMutated);
        injection.active = false;
        require(injection.hits == 1 && rejected.error == number,"actual CLOEXEC failure preserves structured errno");
        require(::fcntl(injection.failed_fd,F_GETFD) == -1 && errno == EBADF,"failed adoption closes its raw descriptor");
        require(rejected.underlying != nullptr,"typed adoption diagnostic retained");
        try { std::rethrow_exception(rejected.underlying); }
        catch (const std::system_error& x) {
            require(x.code() == std::error_code(number,std::generic_category()),"saved errno in system_error");
        }
        require(injection.syncs == (late ? 1u : 0u) && e.barriers.empty(),"no barrier registered after failed post-fsync validation");
        require(!late || synced == std::vector<std::string>{recovery_order.front()},"only intent fsync succeeded");
        require(e.pins.size() == (late ? 11u : 0u) && descriptor_count() == baseline+e.pins.size(),"exception retains only owned descriptors");
        e = E{}; require(descriptor_count() == baseline,"CLOEXEC error releases all ownership");
        synced.clear(); injection.syncs = 0;
        const auto result = PlanEvidenceTestAccess::recover(p,*f.state,f.expected(),e);
        require(result.publication == PlanPublication::AlreadyPublished && result.outcome == O::NotMutated &&
            e.barriers.size() == 7 && synced == recovery_order,"clean retry completes seven ordered recovery barriers");
        e = E{}; require(descriptor_count() == baseline,"retry releases all descriptors"); f.pristine();
        std::cout << "R2-B4.1 CLOEXEC " << (late ? "post-fsync1" : "authority") << ": Cause=Io errno=" << number
            << " Outcome=" << (late ? "OutcomeUncertain" : "NotMutated") << " registry=0 injection=1 no leak; retry=7\n";
    }
    { Fixture f; f.publish(); ProjectionIo io; auto open = io.open; bool active = false; unsigned hits = 0;
      io.open = [&](int fd,const char* leaf,const open_how& how) {
          if (active && !hits && std::string_view(leaf) == "intent.toml" && fdpath(fd) == f.published()) {
              ++hits; errno = ENOSPC; return -1;
          }
          return open(fd,leaf,how);
      };
      auto p = f.open(io); const auto baseline = descriptor_count(); active = true; E e;
      const auto rejected = error([&] { PlanEvidenceTestAccess::recover(p,*f.state,f.expected(),e); },C::Io,O::NotMutated);
      require(hits == 1 && rejected.error == ENOSPC && e.barriers.empty(),"non-CLOEXEC open errno and classification unchanged");
      e = E{}; require(descriptor_count() == baseline,"non-CLOEXEC no leak"); active = false;
      auto wrong = f.expected(); wrong.digest = PlanDigest(d1_test::digest('c')); errno = EIO;
      const auto mismatch = error([&] { recover_activation_plan(p,*f.state,wrong); },C::IdentityMismatch,O::NotMutated);
      require(mismatch.error == 0 && descriptor_count() == baseline,"semantic error never borrows stale errno"); f.pristine();
      std::cout << "R2-B4.1 non-CLOEXEC: Io/ENOSPC/NotMutated; IdentityMismatch/0/NotMutated; no leak\n";
    }
}
void locator_cloexec_error() {
    using E = plan_detail::OperationEvidence;
    auto arm = reinterpret_cast<void(*)(const char*)>(::dlsym(RTLD_DEFAULT,"athena_d4_locator_arm"));
    auto disarm = reinterpret_cast<void(*)(unsigned*,int*)>(::dlsym(RTLD_DEFAULT,"athena_d4_locator_disarm"));
    require(arm && disarm,"locator test requires its CTest-only provider");
    Fixture f; f.publish(); auto p = f.open(); const auto baseline = descriptor_count();
    const auto before = inode(f.published()); E e; unsigned counts[3]{}; int failed_fd = -1;
    struct Disarm {
        decltype(disarm) release; unsigned* counts; int* fd;
        ~Disarm() { release(counts,fd); }
    };
    PlanPersistenceError rejected = [&] {
        Disarm guard{disarm,counts,&failed_fd}; arm(f.binding.generation_authority.c_str());
        return error([&] { PlanEvidenceTestAccess::recover(p,*f.state,f.expected(),e); },C::Io,O::OutcomeUncertain);
    }();
    require(counts[0] == 1 && counts[1] == 1 && counts[2] != 0 && rejected.error == EACCES,
        "actual default-provider locator CLOEXEC errno survives successful closes changing errno");
    require(::fcntl(failed_fd,F_GETFD) == -1 && errno == EBADF,"failed locator adoption descriptor closed");
    require(e.barriers.empty() && e.pins.size() == 11 && descriptor_count() == baseline+11,
        "no invalid first-barrier certificate or parallel ownership");
    require(rejected.underlying != nullptr,"locator keeps typed diagnostic");
    try { std::rethrow_exception(rejected.underlying); }
    catch (const std::system_error& x) { require(x.code() == std::error_code(EACCES,std::generic_category()),"original locator error code"); }
    e = E{}; require(descriptor_count() == baseline && inode(f.published()) == before,"locator exception RAII and namespace unchanged");
    const auto result = PlanEvidenceTestAccess::recover(p,*f.state,f.expected(),e);
    require(result.publication == PlanPublication::AlreadyPublished && result.outcome == O::NotMutated && e.barriers.size() == 7,
        "fresh recovery after locator failure completes its real barriers");
    e = E{}; require(descriptor_count() == baseline,"locator retry no leak"); f.pristine();
    std::cout << "R3 locator CLOEXEC: injection=1, successful fsync=1, changing closes=" << counts[2]
        << ", Io/EACCES/OutcomeUncertain, registry=0, no leak; retry=7\n";
}
// Function-pointer filesystem seam: real failed fstatfs, before acquisition/arm.
int filesystem_error_number = 0;
unsigned filesystem_query_calls = 0;
int filesystem_query(int fd,struct statfs* out) {
    ++filesystem_query_calls;
    if (filesystem_error_number) { errno = filesystem_error_number; return -1; }
    return ::fstatfs(fd,out);
}
std::vector<std::string> residue(const Fixture&,bool);
void filesystem_error_integrity() {
    using E = plan_detail::OperationEvidence;
    auto arm = reinterpret_cast<void(*)(const char*,unsigned,int,bool)>(::dlsym(RTLD_DEFAULT,"athena_d4_filesystem_arm"));
    auto disarm = reinterpret_cast<void(*)(unsigned*,int*)>(::dlsym(RTLD_DEFAULT,"athena_d4_locator_disarm"));
    auto secondary = reinterpret_cast<unsigned(*)()>(::dlsym(RTLD_DEFAULT,"athena_d4_filesystem_secondary_closes"));
    require(arm && disarm && secondary,"filesystem error test requires isolated native provider");
    // Every D4 public entry uses the same preflight, before any mutation/arm.
    for (int number : {EIO,EBADF,EACCES}) for (int api = 0; api < 4; ++api) {
        Fixture f; f.publish(); ProjectionIo io; io.durable.fstatfs = filesystem_query;
        auto p = f.open(io); const auto baseline = descriptor_count(); const auto before = residue(f,true);
        filesystem_query_calls = 0; filesystem_error_number = number;
        struct Reset { ~Reset() { filesystem_error_number = 0; } } reset;
        E e;
        const auto rejected = error([&] {
            if (api == 0) inspect_activation_plan(p,*f.state,f.intent().activation_id);
            else if (api == 1) ensure_plan_namespace(p,*f.state);
            else if (api == 2) PlanEvidenceTestAccess::publish(p,*f.state,f.intent(),d1_test::plan(),e);
            else PlanEvidenceTestAccess::recover(p,*f.state,f.expected(),e);
        },C::Io,O::NotMutated);
        require(rejected.error == number && filesystem_query_calls == 1 && e.barriers.empty() && e.pins.empty(),
            "fstatfs original errno, unarmed ledger and no certificate");
        require(rejected.underlying != nullptr,"fstatfs typed primary diagnostic");
        try { std::rethrow_exception(rejected.underlying); }
        catch (const std::system_error& x) { require(x.code() == std::error_code(number,std::generic_category()),"fstatfs typed code"); }
        filesystem_error_number = 0;
        require(descriptor_count() == baseline && residue(f,true) == before,"fstatfs no leak/mutation");
        const auto result = PlanEvidenceTestAccess::recover(p,*f.state,f.expected(),e);
        require(result.outcome == O::NotMutated && result.publication == PlanPublication::AlreadyPublished && e.barriers.size() == 7,
            "fstatfs retry seven valid barriers");
        e = E{}; require(descriptor_count() == baseline,"fstatfs retry RAII"); f.pristine();
        std::cout << "R3-G fstatfs api=" << api << " Io/" << number << "/NotMutated registry=0; retry=7\n";
    }
    // Native locator traversal before arm, and after fsync 1/3/7. Error after
    // fsync n invalidates that certificate, retaining the earlier n-1 only.
    for (unsigned stage : {0u,1u,3u,7u}) for (int number : {EIO,EACCES,EBADF,ENOENT,ENOTDIR,ELOOP}) {
        Fixture f; f.publish(); auto p = f.open(); const auto baseline = descriptor_count(); const auto before = residue(f,true);
        E e; unsigned counts[3]{}; int ignored;
        struct Disarm { decltype(disarm) release; unsigned* counts; int* fd; ~Disarm() { release(counts,fd); } };
        const auto cause = stage && (number == ENOENT || number == ENOTDIR || number == ELOOP) ? C::Substituted : C::Io;
        const auto rejected = [&] {
            Disarm guard{disarm,counts,&ignored};
            arm(f.binding.generation_authority.c_str(),stage,number,true);
            return error([&] { PlanEvidenceTestAccess::recover(p,*f.state,f.expected(),e); },cause,
                stage ? O::OutcomeUncertain : O::NotMutated);
        }();
        require(counts[0] == 1 && counts[1] == stage && counts[2] != 0 && secondary() != 0 && rejected.error == number,
            "actual openat primary errno survives secondary failing close");
        require(rejected.underlying != nullptr,"locator typed primary diagnostic");
        try { std::rethrow_exception(rejected.underlying); }
        catch (const std::system_error& x) { require(x.code() == std::error_code(number,std::generic_category()),"locator primary exception retained"); }
        require(e.barriers.size() == (stage ? stage-1 : 0),"locator preserves completed registry, not failed validation");
        for (std::size_t n = 0; n < e.barriers.size(); ++n) {
            const auto& b = e.barriers[n];
            require(b.phase == E::Phase::Recovery && e.objects.at(b.object).name ==
                (n == 0 ? "intent.toml" : n == 1 ? "plan.toml" : n == 2 ? f.intent().activation_id.value() : n == 3 ? "plans" : n == 4 ? ".preparations" : ".athena-activation"),
                "locator real ordered Recovery prefix");
        }
        e = E{}; require(descriptor_count() == baseline && residue(f,true) == before,"locator unwind no leak/mutation");
        const auto result = PlanEvidenceTestAccess::recover(p,*f.state,f.expected(),e);
        require(result.outcome == O::NotMutated && result.publication == PlanPublication::AlreadyPublished && e.barriers.size() == 7,"locator fresh retry");
        e = E{}; require(descriptor_count() == baseline,"locator retry RAII"); f.pristine();
        std::cout << "R3-G openat after=" << stage << " Cause=" << static_cast<int>(rejected.cause) << " errno=" << rejected.error
            << " Outcome=" << static_cast<int>(rejected.outcome) << " registry=" << (stage ? stage-1 : 0) << " secondary close failures=" << secondary() << " primary retained; retry=7\n";
    }
    // system_error remains catch-compatible with runtime_error, while logical
    // errors and unsupported types carry no invented syscall errno.
    bool caught = false;
    try { athena::core::detail::stat_fd(-1); }
    catch (const std::runtime_error& x) {
        const auto* typed = dynamic_cast<const std::system_error*>(&x);
        require(typed && typed->code().value() == EBADF,"filesystem runtime_error caller compatibility"); caught = true;
    }
    require(caught,"actual fstat EBADF");
    std::cout << "R3-G fstatfs after arm: NOT APPLICABLE (preflight only); 12 query failures + 24 native openat/unwind failures\n";
}
void recovery_capacity() {
    using E = plan_detail::OperationEvidence;
    for (bool target_first : {false,true}) {
        Fixture f;
        { auto p = f.open(); ensure_plan_namespace(p,*f.state); }
        for (std::size_t n = 0; n < 4095; ++n) {
            char name[33]; std::snprintf(name,sizeof(name),"%032zx",n);
            directory(f.plans()/name); write(f.plans()/name/"intent.toml","opaque intent"); write(f.plans()/name/"plan.toml","opaque plan");
        }
        f.publish(); // Target created after the foreign containers.
        ProjectionIo io; auto open = io.open; auto stat = io.stat; auto read = io.pread;
        bool active = false; std::size_t peak = 0, reads = 0;
        // Readdir order is unspecified. Replay the real entries in both orders to
        // make the retained-target plus foreign scratch peak independent of the FS.
        struct Enumeration { std::vector<struct dirent> entries; std::size_t next = 0; int error = 0; };
        std::map<DIR*,Enumeration> enumerations; auto readdir = io.readdir;
        io.readdir = [&](DIR* dir) -> struct dirent* {
            if (!active || fdpath(::dirfd(dir)) != f.plans()) return readdir(dir);
            auto [it, fresh] = enumerations.try_emplace(dir);
            auto& batch = it->second;
            if (fresh) {
                for (;;) {
                    errno = 0; auto* entry = readdir(dir);
                    if (!entry) { batch.error = errno; break; }
                    // Native records have variable d_reclen; copy the fields
                    // and name without reading sizeof(dirent) past the record.
                    batch.entries.emplace_back(); auto& copy = batch.entries.back();
                    copy.d_ino = entry->d_ino; copy.d_off = entry->d_off;
                    copy.d_reclen = sizeof(copy); copy.d_type = entry->d_type;
                    std::strncpy(copy.d_name,entry->d_name,sizeof(copy.d_name)-1);
                }
                std::stable_sort(batch.entries.begin(),batch.entries.end(),[&](const auto& a,const auto& b) {
                    const bool at = std::string_view(a.d_name) == d1_test::id.value();
                    const bool bt = std::string_view(b.d_name) == d1_test::id.value();
                    return target_first ? at > bt : at < bt;
                });
            }
            if (batch.next != batch.entries.size()) { errno = 0; return &batch.entries[batch.next++]; }
            const int saved = batch.error; enumerations.erase(it); errno = saved; return nullptr;
        };
        auto measure = [&] { if (active) peak = std::max(peak,descriptor_count()); };
        io.open = [&](int fd,const char* leaf,const open_how& how) { auto rc = open(fd,leaf,how); measure(); return rc; };
        io.stat = [&](int fd,const char* leaf,int flags,unsigned mask,struct statx* out) { measure(); return stat(fd,leaf,flags,mask,out); };
        io.pread = [&](int fd,void* b,size_t n,off_t at) -> ssize_t { if (active && fs::path(fdpath(fd)).parent_path() != f.published()) ++reads; return read(fd,b,n,at); };
        auto p = f.open(io); const auto baseline = descriptor_count(); active = true;
        { E e; const auto result = PlanEvidenceTestAccess::recover(p,*f.state,f.expected(),e);
          require(result.outcome == O::NotMutated && result.publication == PlanPublication::AlreadyPublished && e.pins.size() == 11 &&
              e.barriers.size() == 7 && e.foreign.size() == 4095 && peak-baseline <= 15 && !reads,"full recovery capacity/foreign opacity/fd bound");
          require(!target_first || peak-baseline == 15,"worst-order acquisition reaches exactly 15/15 fd");
          require(descriptor_count() == baseline+11,"capacity retains exactly 11 descriptors"); }
        require(descriptor_count() == baseline,"capacity recovery RAII");
        const auto extra = f.plans()/".preparations"/std::string(32,'e'); directory(extra);
        error([&] { recover_activation_plan(p,*f.state,f.expected()); },C::AdministrativeLimit,O::NotMutated);
        require(fs::is_empty(extra) && descriptor_count() == baseline,"4097th preserved, no leak");
        active = false; f.pristine();
        std::cout << "R2-B4.1: 4096 containers, target acquired " << (target_first ? "before" : "after")
            << " foreign containers, recovery pins 11, additional fd peak " << peak-baseline << "/15, foreign reads " << reads << "; 4097th rejected, no leak/current change\n";
    }
}
void ordinary() { recovery_evidence(); recovery_substitutions(); publication_evidence(); publication_registry_faults(); evidence_foundation(); evidence_structural(); evidence_statx_fields(); inspection_evidence(); bootstrap(); bootstrap_evidence(); mechanics(); residues(); identity_errors(); validation_scope(); adoption_coexistence(); security(); error_boundaries(); }
void barrier_faults() {
    std::vector<std::string> successful;
    { Fixture f; ProjectionIo io; io.fsync = [&](int fd) { successful.push_back(relative(f, fd)); return ::fsync(fd); };
        f.publish(io); f.pristine(); }
    require(successful.size() == 22, "fresh path inventory: 8 bootstrap + 2 write + 7 preparation + 5 publication barriers");
    for (std::size_t at = 0; at < successful.size(); ++at) {
        Fixture f;
        for (int retry = 0; retry < 2; ++retry) {
            // Repeat the same fault on a fresh fixture for initial attempt, then
            // recovery/reuse against the actual residue with fresh context/fds.
            ProjectionIo io; std::size_t calls = 0;
            if (!retry) io.fsync = [&](int fd) { if (calls++ == at) { errno = EIO; return -1; } return ::fsync(fd); };
            else io.fsync = [](int) { errno = EIO; return -1; };
            auto p = f.open(io);
            const auto e = error([&] { publish_activation_plan(p, *f.state, f.intent(), d1_test::plan()); }, C::Durability, O::OutcomeUncertain);
            require(e.error == EIO, "fsync errno preserved"); f.pristine();
        }
        auto clean = f.open();
        if (fs::exists(f.published()) || !fs::exists(f.prepared()) || fs::exists(f.prepared() / "plan.toml")) {
            const auto result = publish_activation_plan(clean, *f.state, f.intent(), d1_test::plan());
            require(result.document.digest == f.expected().digest, "fresh retry current-instance durability");
        } else {
            error([&] { publish_activation_plan(clean, *f.state, f.intent(), d1_test::plan()); }, C::PreparationConflict, O::NotMutated);
            require(fs::exists(f.prepared() / "intent.toml") && !fs::exists(f.prepared() / "plan.toml"), "partial pair never completed");
        }
    }
    for (int at = 0; at < 7; ++at) {
        Fixture f; f.publish(); const auto before = inode(f.published());
        for (int retry = 0; retry < 2; ++retry) {
            ProjectionIo io; int calls = 0;
            io.fsync = [&](int fd) { if (calls++ == at) { errno = EIO; return -1; } return ::fsync(fd); };
            auto p = f.open(io);
            error([&] { recover_activation_plan(p, *f.state, f.expected()); }, C::Durability, O::OutcomeUncertain);
            require(inode(f.published()) == before, "recovery no namespace mutation"); f.pristine();
        }
        auto fresh = f.open(); require(recover_activation_plan(fresh, *f.state, f.expected()).outcome == O::NotMutated, "fresh recovery restores durability");
    }
}
ssize_t partial_write(int fd, const void* b, size_t n) {
    if (::lseek(fd, 0, SEEK_CUR) != 0) { errno = ENOSPC; return -1; }
    return ::write(fd, b, std::min<std::size_t>(n, 7));
}
ssize_t interrupted_short_write(int fd, const void* b, size_t n) {
    // Per-fd flags provide one EINTR without any mutable global callback state.
    const int flags = ::fcntl(fd, F_GETFL);
    if (!(flags & O_APPEND)) {
        if (::fcntl(fd, F_SETFL, flags | O_APPEND) < 0) return -1;
        errno = EINTR; return -1;
    }
    return ::write(fd, b, std::min<std::size_t>(n, 11));
}
void io_faults() {
    for (int at = 1; at <= 3; ++at) for (bool after : {false, true}) {
        Fixture f; ProjectionIo io; const auto direct = io.mkdir; int calls = 0;
        io.mkdir = [&](int parent, const char* name, mode_t mode) {
            if (++calls == at) { if (after) require(direct(parent, name, mode) == 0, "real mkdir before failure"); errno = ENOSPC; return -1; }
            return direct(parent, name, mode);
        }; auto p = f.open(io);
        const auto expected = after || at == 2 ? O::OutcomeUncertain : at == 3 ? O::MutatedDurably : O::NotMutated;
        error([&] { publish_activation_plan(p, *f.state, f.intent(), d1_test::plan()); }, C::Io, expected);
        f.pristine();
    }
    for (int at = 1; at <= 2; ++at) for (bool after : {false, true}) {
        Fixture f; ProjectionIo io; auto direct = io.rename; int calls = 0;
        io.rename = [&](int a, const char* n, int b, const char* m, unsigned flags) {
            if (++calls == at) { if (after) require(direct(a, n, b, m, flags) == 0, "real rename before failure"); errno = EIO; return -1; }
            return direct(a, n, b, m, flags);
        }; auto p = f.open(io);
        error([&] { publish_activation_plan(p, *f.state, f.intent(), d1_test::plan()); }, C::Io, O::OutcomeUncertain);
        require(fs::exists(after ? (at == 1 ? f.plans() : f.published()) :
            (at == 1 ? f.admin() / ".plans-bootstrap" : f.prepared())), "exact failed-rename residue");
        f.pristine();
    }
    for (int at = 1; at <= 2; ++at) for (int failure : {EEXIST, EINVAL, EOPNOTSUPP, ENOSYS}) {
        Fixture f; ProjectionIo io; const auto direct = io.rename; int calls = 0;
        io.rename = [&](int a, const char* n, int b, const char* m, unsigned flags) {
            if (++calls == at) { errno = failure; return -1; } return direct(a, n, b, m, flags);
        }; auto p = f.open(io);
        error([&] { publish_activation_plan(p, *f.state, f.intent(), d1_test::plan()); }, failure == EEXIST ?
            (at == 1 ? C::BootstrapConflict : C::PublicationConflict) : C::UnsupportedFilesystem, O::OutcomeUncertain);
        f.pristine();
    }
    for (int which = 0; which < 4; ++which) {
        Fixture f; ProjectionIo io;
        if (which == 0) io.durable.write = partial_write;
        if (which == 1) io.durable.write = [](int, const void*, size_t) -> ssize_t { return 0; };
        if (which == 2) io.durable.write = [](int, const void*, size_t) -> ssize_t { errno = EIO; return -1; };
        if (which == 3) io.durable.write = [](int, const void*, size_t n) -> ssize_t { return n + 1; };
        auto p = f.open(io); const auto e = error([&] { publish_activation_plan(p, *f.state, f.intent(), d1_test::plan()); }, C::Io, O::OutcomeUncertain);
        if (which == 0) require(e.error == ENOSPC && fs::file_size(f.prepared() / "intent.toml") == 7, "partial private write retained");
        require(!fs::exists(f.prepared() / "plan.toml"), "no continuation after write failure"); f.pristine();
    }
    { Fixture f; ProjectionIo io; io.durable.write = interrupted_short_write; f.publish(io); f.pristine(); }
    { Fixture f; ProjectionIo io;
        io.durable.write = [](int fd, const void* b, size_t n) -> ssize_t {
            if (fdpath(fd).ends_with("/plan.toml")) { errno = ENOSPC; return -1; } return ::write(fd, b, n);
        }; auto p = f.open(io);
        error([&] { publish_activation_plan(p, *f.state, f.intent(), d1_test::plan()); }, C::Io, O::OutcomeUncertain);
        require(fs::file_size(f.prepared() / "plan.toml") == 0 && fs::file_size(f.prepared() / "intent.toml") > 0,
            "failure writing second document preserves both private files"); f.pristine(); }
    // Fail both document-exclusive opens, and all directory-open categories
    // after a real bootstrap mutation. No hidden repair or cleanup.
    for (auto target : {".plans-bootstrap", ".preparations", "intent.toml", "plan.toml"}) {
        Fixture f; ProjectionIo io; const auto direct = io.open; bool armed = false;
        io.open = [&](int fd, const char* name, const open_how& how) {
            if (std::string(name) == target && fs::exists(f.admin() / ".plans-bootstrap")) { armed = true; errno = EIO; return -1; }
            if ((how.flags & O_CREAT) && std::string(name) == target) { armed = true; errno = EIO; return -1; }
            return direct(fd, name, how);
        }; auto p = f.open(io);
        error([&] { publish_activation_plan(p, *f.state, f.intent(), d1_test::plan()); }, C::Io, O::OutcomeUncertain);
        require(armed, "open category exercised"); f.pristine();
    }
    { Fixture f; ProjectionIo io; const auto direct = io.stat;
        io.stat = [&](int fd, const char* name, int flags, unsigned mask, struct statx* out) {
            if (fs::exists(f.admin() / ".plans-bootstrap") && std::string(name) == ".plans-bootstrap") { errno = EIO; return -1; }
            return direct(fd, name, flags, mask, out);
        }; auto p = f.open(io);
        error([&] { ensure_plan_namespace(p, *f.state); }, C::Io, O::OutcomeUncertain); f.pristine(); }
    for (int kind = 0; kind < 5; ++kind) {
        Fixture f; f.publish(); ProjectionIo io; auto direct = io.pread; int calls = 0;
        io.pread = [&](int fd, void* buf, size_t size, off_t offset) -> ssize_t {
            if (++calls == 1 && kind == 0) { errno = EINTR; return -1; }
            if (kind == 1) { errno = EIO; return -1; }
            if (kind == 2) return size + 1;
            if (kind == 3) return 0;
            return direct(fd, buf, kind == 4 ? std::min<std::size_t>(size, 7) : size, offset);
        }; auto p = f.open(io);
        if (kind == 0 || kind == 4) require(inspect_activation_plan(p, *f.state, d1_test::id).digest == f.expected().digest, "EINTR/short reads retry");
        else error([&] { inspect_activation_plan(p, *f.state, d1_test::id); }, kind == 3 ? C::Substituted : C::Io, O::NotMutated);
    }
    { Fixture f; auto p = f.open(); ProjectionIo io; io.readdir = [](DIR*) -> struct dirent* { errno = EIO; return nullptr; };
        projection_error([&] { f.open(io); }, ProjectionCause::IoFailure, EIO); f.pristine(); }
}
void substitutions() {
    for (auto target : {"intent.toml", "plan.toml", "container", "plans", "preparations", "admin"}) for (int at = 1; at <= 7; ++at) {
        Fixture f; f.publish(); ProjectionIo io; bool changed = false; ino_t retired = 0; int calls = 0;
        io.fsync = [&](int fd) {
            if (++calls == at) {
                changed = true; fs::path path;
                if (std::string(target) == "intent.toml" || std::string(target) == "plan.toml") path = f.published() / target;
                if (std::string(target) == "container") path = f.published();
                if (std::string(target) == "plans") path = f.plans();
                if (std::string(target) == "preparations") path = f.plans() / ".preparations";
                if (std::string(target) == "admin") path = f.admin();
                retired = inode(path); fs::rename(path, f.base / "retired");
                fs::copy(f.base / "retired", path, fs::copy_options::recursive);
                require(inode(path) != retired, "fresh identical instance");
                if (std::string(target) == "admin") {
                    const auto binding_path = f.admin() / "authority.toml";
                    require(inode(f.base / "retired/authority.toml") == f.protected_files.at(binding_path).st_ino,
                        "injected admin replacement preserves retired binding");
                    require(::lstat(binding_path.c_str(), &f.protected_files.at(binding_path)) == 0,
                        "observe injection's replacement binding before D4 resumes");
                }
            }
            return ::fsync(fd);
        }; auto p = f.open(io);
        error([&] { recover_activation_plan(p, *f.state, f.expected()); }, C::Substituted, O::OutcomeUncertain);
        if (std::string(target) != "admin") {
            std::set<ino_t> synchronized; ProjectionIo fresh;
            fresh.fsync = [&](int fd) { struct stat s{}; require(::fstat(fd, &s) == 0, "current fsync identity"); synchronized.insert(s.st_ino); return ::fsync(fd); };
            auto q = f.open(fresh); recover_activation_plan(q, *f.state, f.expected());
            require(!synchronized.contains(retired), "retired inode cannot certify new instance");
        }
        f.pristine();
    }
    // Replacement during read is rejected before any barrier.
    for (auto target : {"intent.toml", "plan.toml"}) { Fixture f; f.publish(); ProjectionIo io; auto direct = io.pread; bool changed = false;
        io.pread = [&](int fd, void* b, size_t n, off_t o) {
            if (!changed && fdpath(fd).ends_with(std::string("/") + target)) {
                changed = true; fs::rename(f.published() / target, f.base / "retired");
                write(f.published() / target, std::string(target) == "intent.toml" ? serialize(f.intent()) : serialize(d1_test::plan()));
            }
            return direct(fd, b, n, o);
        }; auto p = f.open(io); error([&] { inspect_activation_plan(p, *f.state, d1_test::id); }, C::Substituted, O::NotMutated); }
    for (bool after : {false, true}) for (auto target : {"bootstrap", "preparations", "plan-container", "intent.toml", "plan.toml"}) {
        Fixture f; ProjectionIo io; auto direct = io.rename; bool changed = false;
        io.rename = [&](int a, const char* n, int b, const char* m, unsigned flags) {
            const bool infrastructure = std::string(n) == ".plans-bootstrap";
            const bool bootstrap_target = std::string(target) == "bootstrap" || std::string(target) == "preparations";
            auto replace = [&] {
                fs::path path = bootstrap_target ? (after ? f.plans() : f.admin() / ".plans-bootstrap") : (after ? f.published() : f.prepared());
                if (std::string(target) == "preparations") path /= ".preparations";
                if (std::string(target) == "intent.toml" || std::string(target) == "plan.toml") path /= target;
                fs::rename(path, f.base / "retired"); fs::copy(f.base / "retired", path, fs::copy_options::recursive); changed = true;
            };
            if (infrastructure == bootstrap_target && !after) replace();
            const int rc = direct(a, n, b, m, flags);
            if (infrastructure == bootstrap_target && after) replace();
            return rc;
        }; auto p = f.open(io);
        error([&] { publish_activation_plan(p, *f.state, f.intent(), d1_test::plan()); }, C::Substituted, O::OutcomeUncertain);
        require(changed, "replacement at bootstrap/plan rename boundary"); f.pristine();
    }
    { Fixture f; f.publish(); ProjectionIo io; auto direct = io.stat; int barriers = 0; bool changed = false;
        io.fsync = [&](int fd) { ++barriers; return ::fsync(fd); };
        io.stat = [&](int fd, const char* name, int flags, unsigned mask, struct statx* out) {
            const int rc = direct(fd, name, flags, mask, out);
            if (!changed && barriers == 7 && std::string(name) == "intent.toml") {
                changed = true; fs::rename(f.published() / "intent.toml", f.base / "retired"); write(f.published() / "intent.toml", serialize(f.intent()));
            } return rc;
        }; auto p = f.open(io); error([&] { recover_activation_plan(p, *f.state, f.expected()); }, C::Substituted, O::OutcomeUncertain);
        require(changed, "post-barrier final validation replacement"); f.pristine(); }
}
// A namespace oracle independent of the production outcome ledger. Record all
// D4 names, metadata and bytes; normalize only the fixture-specific intent bytes.
std::vector<std::string> residue(const Fixture& f, bool instances = false) {
    std::vector<std::string> result;
    for (const auto& root : {f.admin() / ".plans-bootstrap", f.plans()}) {
        if (!fs::exists(root)) continue;
        auto observe = [&](const fs::path& path) {
            struct stat info{}; require(::lstat(path.c_str(), &info) == 0, "residue metadata");
            std::string value = fs::relative(path, f.admin()).native() + ":" +
                std::to_string(info.st_mode) + ":" + std::to_string(info.st_uid) + ":";
            if (instances) value += std::to_string(info.st_dev) + ":" + std::to_string(info.st_ino) + ":" +
                std::to_string(info.st_nlink) + ":" + std::to_string(info.st_mtim.tv_sec) + ":" +
                std::to_string(info.st_mtim.tv_nsec) + ":" + std::to_string(info.st_ctim.tv_sec) + ":" +
                std::to_string(info.st_ctim.tv_nsec) + ":";
            if (S_ISREG(info.st_mode)) {
                std::ifstream file(path, std::ios::binary);
                std::string bytes(std::istreambuf_iterator<char>(file), {});
                require(!file.bad(), "residue document read");
                value += std::to_string(info.st_nlink) + ":" + std::to_string(info.st_size) + ":";
                value += bytes == serialize(f.intent()) ? "<expected-intent>" : bytes;
            }
            result.push_back(std::move(value));
        };
        observe(root);
        for (const auto& entry : fs::recursive_directory_iterator(root)) observe(entry.path());
    }
    std::sort(result.begin(), result.end()); return result;
}
struct BoundaryPoint {
    std::string kind, path;
    std::vector<std::string> residue;
    C cause;
};
struct BoundaryIo {
    const Fixture& fixture;
    explicit BoundaryIo(const Fixture& f) : fixture(f) {}
    bool active = false;
    std::size_t position = 0, fail_at = 0;
    bool failed = false;
    std::vector<BoundaryPoint> inventory;
    std::vector<std::string> failed_residue;
    bool fault(std::string_view kind, int fd, std::string_view name = {}) {
        if (!active) return false;
        const int saved_errno = errno;
        ++position;
        if (!fail_at) {
            auto path = fdpath(fd);
            if (!name.empty() && name != ".") path += "/" + std::string(name);
            path = fs::path(path).lexically_normal().lexically_relative(fixture.admin()).native();
            const auto cause = kind == "fsync" ? C::Durability :
                kind == "open" && (name == "authority.toml" || name == ".athena-projection-authority.toml") ?
                    C::AuthorityMismatch : C::Io;
            inventory.push_back({std::string(kind), std::move(path), residue(fixture), cause});
        }
        if (fail_at == position) {
            failed = true; failed_residue = residue(fixture, true); errno = EIO; return true;
        }
        errno = saved_errno;
        return false;
    }
    ProjectionIo io() {
        ProjectionIo result;
        auto open = result.open; auto stat = result.stat; auto mkdir = result.mkdir;
        auto rename = result.rename; auto pread = result.pread; auto readdir = result.readdir;
        result.open = [this, open](int fd, const char* name, const open_how& how) { return fault("open", fd, name) ? -1 : open(fd, name, how); };
        result.stat = [this, stat](int fd, const char* name, int flags, unsigned mask, struct statx* out) {
            return fault("stat", fd, name) ? -1 : stat(fd, name, flags, mask, out);
        };
        result.mkdir = [this, mkdir](int fd, const char* name, mode_t mode) { return fault("mkdir", fd, name) ? -1 : mkdir(fd, name, mode); };
        result.rename = [this, rename](int a, const char* n, int b, const char* m, unsigned flags) {
            return fault("rename", a, n) ? -1 : rename(a, n, b, m, flags);
        };
        result.pread = [this, pread](int fd, void* b, size_t n, off_t offset) -> ssize_t { return fault("pread", fd) ? -1 : pread(fd, b, n, offset); };
        result.readdir = [this, readdir](DIR* dir) -> struct dirent* { return fault("readdir", ::dirfd(dir)) ? nullptr : readdir(dir); };
        result.fsync = [this](int fd) { return fault("fsync", fd) ? -1 : ::fsync(fd); };
        return result;
    }
};
std::vector<O> boundary_outcomes(const std::vector<BoundaryPoint>& points, std::string_view path) {
    auto match = [&](std::size_t at, const std::vector<std::pair<std::string, std::string>>& sequence) {
        if (at + sequence.size() > points.size()) return false;
        for (std::size_t n = 0; n < sequence.size(); ++n)
            if (points[at + n].kind != sequence[n].first || points[at + n].path != sequence[n].second) return false;
        return true;
    };
    if (path != "recovery") {
        // R2-B3 no longer reopens/owns a second Namespace after ensure. Its
        // last source-absence observation precedes the first target operation.
        // Derive these checkpoints from I/O, never from the production ledger.
        const auto bootstrap_barriers = path == "fresh" || path == "bootstrap" ? 8u : 4u;
        unsigned completed_barriers = 0;
        std::size_t bootstrap_complete = points.size();
        for (std::size_t n = 0; n < points.size(); ++n) {
            if (points[n].kind == "fsync") ++completed_barriers;
            if (completed_barriers == bootstrap_barriers && points[n].kind == "stat" && points[n].path == ".plans-bootstrap")
                bootstrap_complete = n + 1;
        }
        require(bootstrap_complete < points.size(), "shared evidence bootstrap conclusion identified");
        const std::vector<std::pair<std::string, std::string>> binding_check{
            {"stat", "../../generations"}, {"stat", "../../generations"},
            {"stat", "../../generations"}, {"stat", "../../generations"},
            {"stat", "../../generations/.athena-projection-authority.toml"}};
        std::vector<std::size_t> bootstrap_checks, target_checks;
        auto first_mkdir = points.size(), private_mkdir = points.size();
        for (std::size_t n = 0; n < points.size(); ++n) {
            if (match(n, binding_check)) (n < bootstrap_complete ? bootstrap_checks : target_checks).push_back(n);
            if (points[n].kind == "mkdir") {
                first_mkdir = std::min(first_mkdir, n);
                if (n >= bootstrap_complete) private_mkdir = n;
            }
        }
        require(bootstrap_checks.size() >= 2 && target_checks.size() >= 3, "shared evidence unarmed/armed checks identified");
        // Bootstrap: one unarmed conclusion, then the first armed validator.
        // Target: initial and closing named checks, then the armed barrier check.
        const auto bootstrap_armed = bootstrap_checks[1], target_armed = target_checks[2];
        std::vector<O> result;
        for (std::size_t n = 0; n < points.size(); ++n) {
            auto expected = O::NotMutated;
            if (n < bootstrap_complete) {
                if (n >= bootstrap_armed || (path == "fresh" && n > first_mkdir)) expected = O::OutcomeUncertain;
            } else {
                if (path == "fresh" || path == "bootstrap") expected = O::MutatedDurably;
                if (n > private_mkdir || n >= target_armed) expected = O::OutcomeUncertain;
            }
            result.push_back(expected);
        }
        return result;
    }
    // The two unarmed named checks surround the initial conclusion. The
    // third begins the first barrier validator, after uncertainty was armed.
    // Identify this from State/binding I/O, independently of the production ledger.
    const std::vector<std::pair<std::string, std::string>> binding_check{
        {"stat", "../../generations"}, {"stat", "../../generations"},
        {"stat", "../../generations"}, {"stat", "../../generations"},
        {"stat", "../../generations/.athena-projection-authority.toml"}};
    std::vector<std::size_t> checks;
    for (std::size_t n = 0; n < points.size(); ++n) if (match(n, binding_check)) checks.push_back(n);
    require(checks.size() >= 3, "recovery unarmed/armed evidence checkpoints identified");
    const auto armed = checks[2];
    const auto first_sync = std::find_if(points.begin(), points.end(), [](const auto& point) { return point.kind == "fsync"; });
    require(first_sync != points.end() && armed < static_cast<std::size_t>(first_sync-points.begin()), "recovery arms before first fsync");
    std::vector<O> result(points.size(), O::NotMutated);
    std::fill(result.begin()+armed, result.end(), O::OutcomeUncertain);
    return result;
}
void boundaries(std::string_view only = {}, std::size_t first = 1, std::size_t last = static_cast<std::size_t>(-1)) {
    for (const std::string path : {"fresh", "bootstrap", "reused", "published", "recovery"}) {
        if (!only.empty() && only != path && !(only == "published" && path == "recovery") && !(only == "recovery-targeted" && path == "recovery")) continue;
        auto prepare = [&](Fixture& f) {
            if (path == "bootstrap") { directory(f.admin() / ".plans-bootstrap"); directory(f.admin() / ".plans-bootstrap/.preparations"); }
            if (path == "reused" || path == "published" || path == "recovery") {
                directory(f.plans()); directory(f.plans() / ".preparations");
                const auto container = path == "reused" ? f.prepared() : f.published();
                directory(container); write(container / "intent.toml", serialize(f.intent()));
                write(container / "plan.toml", serialize(d1_test::plan()));
            }
        };
        std::vector<BoundaryPoint> points;
        { Fixture f; prepare(f); BoundaryIo seams{f}; auto p = f.open(seams.io()); seams.active = true;
            if (path == "recovery") recover_activation_plan(p, *f.state, f.expected());
            else publish_activation_plan(p, *f.state, f.intent(), d1_test::plan());
            points = std::move(seams.inventory); f.pristine(); }
        const auto outcomes = boundary_outcomes(points, path);
        const auto end = std::min(last, points.size());
        require(first >= 1 && first <= end, "valid nonempty boundary shard");
        std::cout << path << ": inventory=" << points.size() << ", checking inclusive interval " << first << ".." << end << std::endl;
        std::size_t checked = 0;
        for (std::size_t at = first - 1; at < end; ++at) {
            if (only == "recovery-targeted") {
                const auto armed = std::find(outcomes.begin(), outcomes.end(), O::OutcomeUncertain)-outcomes.begin();
                const bool near_arm = at+1 == static_cast<std::size_t>(armed) || at == static_cast<std::size_t>(armed);
                const bool near_sync = points[at].kind == "fsync" || (at && points[at-1].kind == "fsync");
                if (at != 0 && at+1 != end && !near_arm && !near_sync) continue;
            }
            ++checked;
            Fixture f; prepare(f); BoundaryIo seams{f}; seams.fail_at = at + 1; auto p = f.open(seams.io()); seams.active = true;
            try {
                const auto failure = error([&] {
                    if (path == "recovery") recover_activation_plan(p, *f.state, f.expected());
                    else publish_activation_plan(p, *f.state, f.intent(), d1_test::plan());
                }, points[at].cause, outcomes[at]);
                require(seams.failed && failure.error == EIO, "exact fault position and errno");
                require(residue(f) == points[at].residue, "D4 residual names, metadata and bytes match successful pre-fault inventory");
                require(residue(f, true) == seams.failed_residue, "failure preserves every pre-fault D4 inode and document");
                f.pristine();
            } catch (const std::exception& e) {
                throw std::runtime_error(path + " boundary " + std::to_string(at + 1) + " " + points[at].kind +
                    " " + points[at].path + ": " + e.what());
            }
        }
        std::cout << path << ": interval " << first << ".." << end << " of " << points.size()
            << ": " << checked << " selected faults; cause/errno/outcome/residue/protected state verified" << std::endl;
    }
}
void final_read_substitutions() {
    for (const auto phase : {"recovery", "preparation", "publication"})
        for (const auto target : {"container", "plans", "preparations", "admin"}) {
            Fixture f;
            if (std::string(phase) == "recovery") f.publish();
            if (std::string(phase) == "preparation") {
                directory(f.plans()); directory(f.plans() / ".preparations"); directory(f.prepared());
                write(f.prepared() / "intent.toml", serialize(f.intent()));
                write(f.prepared() / "plan.toml", serialize(d1_test::plan()));
            }
            const int final_barrier = std::string(phase) == "recovery" ? 7 : std::string(phase) == "preparation" ? 11 : 22;
            ProjectionIo io; const auto direct = io.pread;
            int barriers = 0, reads = 0; bool replaced = false;
            fs::path replacement; struct stat retired{};
            io.fsync = [&](int fd) { ++barriers; return ::fsync(fd); };
            io.pread = [&](int fd, void* bytes, size_t size, off_t offset) -> ssize_t {
                // Exact audit reproduction: second plan read after the seventh
                // recovery fsync is the explicit final reread, after ancestry checks.
                if (barriers == final_barrier && offset == 0 && fdpath(fd).ends_with("/plan.toml") && ++reads == 2) {
                    replacement = std::string(target) == "plans" ? f.plans() :
                        std::string(target) == "preparations" ? f.plans() / ".preparations" :
                        std::string(target) == "admin" ? f.admin() :
                        std::string(phase) == "preparation" ? f.prepared() : f.published();
                    require(::lstat(replacement.c_str(), &retired) == 0, "retired final-read identity");
                    fs::rename(replacement, f.base / "retired-final");
                    fs::copy(f.base / "retired-final", replacement, fs::copy_options::recursive);
                    require(inode(replacement) != retired.st_ino, "identical final-read replacement has new inode");
                    replaced = true;
                    if (std::string(target) == "admin")
                        require(::lstat((f.admin() / "authority.toml").c_str(),
                            &f.protected_files.at(f.admin() / "authority.toml")) == 0, "observe injected binding replacement");
                }
                return direct(fd, bytes, size, offset);
            };
            auto p = f.open(io);
            const auto failure = error([&] {
                if (std::string(phase) == "recovery") recover_activation_plan(p, *f.state, f.expected());
                else publish_activation_plan(p, *f.state, f.intent(), d1_test::plan());
            }, C::Substituted, O::OutcomeUncertain);
            require(replaced && barriers == final_barrier && failure.error == 0, "final reread rejects without durable success");
            require(fs::exists(std::string(phase) == "preparation" ? f.prepared() : f.published()), "replacement residue preserved");
            f.pristine();
            std::set<std::pair<dev_t, ino_t>> synchronized;
            ProjectionIo fresh;
            fresh.fsync = [&](int fd) {
                struct stat current{}; require(::fstat(fd, &current) == 0, "fresh barrier identity");
                synchronized.emplace(current.st_dev, current.st_ino); return ::fsync(fd);
            };
            auto q = f.open(fresh);
            if (std::string(phase) == "preparation") {
                require(publish_activation_plan(q, *f.state, f.intent(), d1_test::plan()).outcome == O::MutatedDurably,
                    "fresh publication synchronizes replaced preparation");
                if (std::string(target) == "container") replacement = f.published();
            } else require(recover_activation_plan(q, *f.state, f.expected()).outcome == O::NotMutated,
                "fresh recovery synchronizes current namespace");
            struct stat current{}; require(::lstat(replacement.c_str(), &current) == 0, "current replacement remains");
            require(synchronized.contains({current.st_dev, current.st_ino}) &&
                !synchronized.contains({retired.st_dev, retired.st_ino}), "barriers belong to current, never retired instance");
            f.pristine();
        }
    std::cout << "12 identical final-read substitutions rejected; fresh current-instance barriers verified (including plans during final plan.toml read after fsync 7)\n";
}
void late_bootstrap_substitutions() {
    for (const auto mode : {"fresh", "resumed", "existing"})
        for (const auto target : {"plans", "preparations", "admin"}) {
            Fixture f;
            if (std::string(mode) == "resumed") {
                directory(f.admin() / ".plans-bootstrap");
                directory(f.admin() / ".plans-bootstrap/.preparations");
            } else if (std::string(mode) == "existing") { auto q = f.open(); ensure_plan_namespace(q, *f.state); }
            const int last = std::string(mode) == "existing" ? 4 : 8;
            ProjectionIo io; auto direct = io.open;
            int barriers = 0, enumerations = 0; bool replaced = false;
            fs::path replacement; struct stat retired{};
            io.fsync = [&](int fd) { ++barriers; return ::fsync(fd); };
            io.open = [&](int fd, const char* name, const open_how& how) {
                // Inject in the one concluding admin enumeration after the last
                // barrier's named/retained checks; the shared evidence must reject.
                if (barriers == last && std::string(name) == "." && fdpath(fd) == f.admin() && ++enumerations == 1) {
                    replacement = std::string(target) == "plans" ? f.plans() :
                        std::string(target) == "preparations" ? f.plans() / ".preparations" : f.admin();
                    require(::lstat(replacement.c_str(), &retired) == 0, "late bootstrap retired inode");
                    fs::rename(replacement, f.base / "retired-bootstrap");
                    fs::copy(f.base / "retired-bootstrap", replacement, fs::copy_options::recursive);
                    replaced = true;
                    if (std::string(target) == "admin") require(::lstat((f.admin() / "authority.toml").c_str(),
                        &f.protected_files.at(f.admin() / "authority.toml")) == 0, "injected admin binding observation");
                }
                return direct(fd, name, how);
            };
            auto p = f.open(io);
            auto e = error([&] { ensure_plan_namespace(p, *f.state); }, C::Substituted, O::OutcomeUncertain);
            require(replaced && barriers == last && e.error == 0, "late bootstrap rejected at final check");
            require(fs::exists(f.plans() / ".preparations") && !fs::exists(f.admin() / ".plans-bootstrap"), "complete residue preserved");
            std::set<std::pair<dev_t, ino_t>> synced;
            ProjectionIo fresh; fresh.fsync = [&](int fd) { struct stat s{}; require(::fstat(fd, &s) == 0, "fresh bootstrap identity");
                synced.emplace(s.st_dev, s.st_ino); return ::fsync(fd); };
            auto q = f.open(fresh); require(ensure_plan_namespace(q, *f.state) == O::NotMutated, "fresh ensure succeeds");
            struct stat current{}; require(::lstat(replacement.c_str(), &current) == 0, "current bootstrap inode");
            require(synced.contains({current.st_dev, current.st_ino}) && !synced.contains({retired.st_dev, retired.st_ino}), "current bootstrap barriers only");
            f.pristine();
        }
    std::cout << "9 late bootstrap substitutions rejected; fresh current-inode barriers verified\n";
}
void late_binding_changes(bool enumeration = false) {
    for (const auto phase : {"recovery", "retry", "reuse", "publication", "inspection"})
        for (bool generation : {false, true}) {
            Fixture f;
            if (std::string(phase) == "recovery" || std::string(phase) == "retry" || std::string(phase) == "inspection") f.publish();
            if (std::string(phase) == "reuse") {
                directory(f.plans()); directory(f.plans() / ".preparations"); directory(f.prepared());
                write(f.prepared() / "intent.toml", serialize(f.intent())); write(f.prepared() / "plan.toml", serialize(d1_test::plan()));
            }
            const int last = std::string(phase) == "recovery" ? 7 :
                std::string(phase) == "retry" ? 11 : std::string(phase) == "reuse" ? (enumeration ? 16 : 11) :
                std::string(phase) == "inspection" ? 0 : 22;
            const auto record = generation ? f.base / "generations/.athena-projection-authority.toml" : f.admin() / "authority.toml";
            struct stat before{}; require(::lstat(record.c_str(), &before) == 0, "binding original inode");
            ProjectionIo io; auto direct = io.pread; auto open = io.open;
            int barriers = 0, reads = 0; bool changed = false, armed = false;
            auto change = [&] {
                auto binding = f.binding; binding.projection_id = ProjectionId(std::string(32, '0'));
                std::ofstream stream(record, std::ios::binary); stream << serialize(binding); stream.close();
                require(!stream.fail(), "late in-place binding write"); changed = true;
            };
            io.fsync = [&](int fd) { ++barriers; return ::fsync(fd); };
            io.pread = [&](int fd, void* bytes, size_t size, off_t offset) -> ssize_t {
                if (barriers == last && offset == 0 && fdpath(fd).ends_with("/plan.toml") && ++reads == 2) {
                    if (enumeration) armed = true;
                    else change();
                }
                return direct(fd, bytes, size, offset);
            };
            io.open = [&](int fd, const char* name, const open_how& how) {
                if (armed && !changed && std::string(name) == "." && fdpath(fd) == f.admin()) change();
                return open(fd, name, how);
            };
            auto p = f.open(io);
            auto e = error([&] {
                if (std::string(phase) == "inspection") inspect_activation_plan(p, *f.state, d1_test::id);
                else if (std::string(phase) == "recovery") recover_activation_plan(p, *f.state, f.expected());
                else publish_activation_plan(p, *f.state, f.intent(), d1_test::plan());
            }, C::AuthorityMismatch, std::string(phase) == "inspection" ? O::NotMutated : O::OutcomeUncertain);
            struct stat after{}; require(::lstat(record.c_str(), &after) == 0, "binding changed inode");
            require(changed && barriers == last && e.error == 0 && before.st_ino == after.st_ino &&
                before.st_dev == after.st_dev && before.st_mode == after.st_mode && before.st_uid == after.st_uid, "in-place binding rejection");
            projection_error([&] { f.open(); }, ProjectionCause::BindingConflict);
            require(fs::exists(std::string(phase) == "reuse" && !enumeration ? f.prepared() : f.published()), "binding conflict residue preserved");
            // Restore only the injected fixture change, then verify unrelated
            // observations and fresh complete barriers; production never repairs it.
            write(record, serialize(f.binding)); require(::lstat(record.c_str(), &f.protected_files.at(record)) == 0, "observe fixture restoration");
            auto q = f.open();
            if (std::string(phase) == "reuse" && !enumeration) require(publish_activation_plan(q, *f.state, f.intent(), d1_test::plan()).outcome == O::MutatedDurably, "fresh reuse after fixture restoration");
            else require(recover_activation_plan(q, *f.state, f.expected()).outcome == O::NotMutated, "fresh recovery after fixture restoration");
            f.pristine();
        }
    std::cout << "10 in-place binding changes during " << (enumeration ? "final enumeration" : "final read")
        << " rejected with AuthorityMismatch, errno=0 and exact outcome\n";
}
void bootstrap_binding_changes() {
    for (const auto mode : {"fresh", "resumed", "existing"}) for (bool generation : {false, true}) {
        Fixture f;
        if (std::string(mode) == "resumed") { directory(f.admin() / ".plans-bootstrap"); directory(f.admin() / ".plans-bootstrap/.preparations"); }
        if (std::string(mode) == "existing") { auto q = f.open(); ensure_plan_namespace(q, *f.state); }
        const auto record = generation ? f.base / "generations/.athena-projection-authority.toml" : f.admin() / "authority.toml";
        const int last = std::string(mode) == "existing" ? 4 : 8;
        const auto before = f.protected_files.at(record);
        ProjectionIo io; auto open = io.open; int barriers = 0, opens = 0; bool changed = false;
        io.fsync = [&](int fd) { ++barriers; return ::fsync(fd); };
        io.open = [&](int fd, const char* name, const open_how& how) {
            if (barriers == last && std::string(name) == "." && fdpath(fd) == f.admin() && ++opens == 1) {
                auto binding = f.binding; binding.projection_id = ProjectionId(std::string(32, '0'));
                std::ofstream stream(record); stream << serialize(binding); stream.close(); require(!stream.fail(), "bootstrap binding injection"); changed = true;
            }
            return open(fd, name, how);
        };
        auto p = f.open(io); auto e = error([&] { ensure_plan_namespace(p, *f.state); }, C::AuthorityMismatch, O::OutcomeUncertain);
        struct stat after{}; require(::lstat(record.c_str(), &after) == 0 && changed && barriers == last && e.error == 0 &&
            before.st_ino == after.st_ino && before.st_dev == after.st_dev && before.st_uid == after.st_uid && before.st_mode == after.st_mode,
            "bootstrap rejects in-place binding change");
        projection_error([&] { f.open(); }, ProjectionCause::BindingConflict);
        require(fs::is_directory(f.plans() / ".preparations") && !fs::exists(f.admin() / ".plans-bootstrap"), "bootstrap residue retained");
        write(record, serialize(f.binding)); require(::lstat(record.c_str(), &f.protected_files.at(record)) == 0, "fixture-only binding restoration");
        auto q = f.open(); require(ensure_plan_namespace(q, *f.state) == O::NotMutated, "fresh ensure after fixture restoration"); f.pristine();
    }
    std::cout << "6 bootstrap binding changes at final enumeration rejected\n";
}
void eof_inventory_changes() {
    for (const auto mode : {"recovery", "inspection", "fresh", "resumed", "existing", "private"}) {
        Fixture f;
        const bool bootstrap = std::string(mode) == "fresh" || std::string(mode) == "resumed" || std::string(mode) == "existing";
        if (!bootstrap) f.publish();
        if (std::string(mode) == "resumed") { directory(f.admin() / ".plans-bootstrap"); directory(f.admin() / ".plans-bootstrap/.preparations"); }
        if (std::string(mode) == "existing") { auto q = f.open(); ensure_plan_namespace(q, *f.state); }
        const int last = std::string(mode) == "inspection" ? 0 : bootstrap ? (std::string(mode) == "existing" ? 4 : 8) : 7;
        ProjectionIo io; auto read = io.pread; auto open = io.open; auto readdir = io.readdir;
        int barriers = 0, reads = 0, opens = 0; bool armed = false, changed = false; fs::path added; struct stat injected{};
        io.fsync = [&](int fd) { ++barriers; return ::fsync(fd); };
        io.pread = [&](int fd, void* bytes, size_t n, off_t offset) -> ssize_t {
            if (!bootstrap && barriers == last && offset == 0 && fdpath(fd).ends_with("/plan.toml") && ++reads == 2) armed = true;
            return read(fd, bytes, n, offset);
        };
        io.open = [&](int fd, const char* name, const open_how& how) {
            if (bootstrap && barriers == last && std::string(name) == "." && fdpath(fd) == f.admin() && ++opens == 1) armed = true;
            return open(fd, name, how);
        };
        io.readdir = [&](DIR* dir) -> struct dirent* {
            auto entry = readdir(dir); const int saved = errno;
            const auto target = bootstrap ? f.plans() : std::string(mode) == "private" ? f.plans() / ".preparations" : f.published();
            if (armed && !changed && !entry && saved == 0 && fdpath(::dirfd(dir)) == target) {
                added = target / (bootstrap || std::string(mode) == "private" ? std::string(32, 'a') : "status.toml");
                if (bootstrap || std::string(mode) == "private") directory(added); else write(added, "injected entry");
                require(::lstat(added.c_str(), &injected) == 0, "injected residue observation"); changed = true;
            }
            errno = saved; return entry;
        };
        auto p = f.open(io); auto e = error([&] {
            if (bootstrap) ensure_plan_namespace(p, *f.state);
            else if (std::string(mode) == "inspection") inspect_activation_plan(p, *f.state, d1_test::id);
            else recover_activation_plan(p, *f.state, f.expected());
        }, C::Substituted, std::string(mode) == "inspection" ? O::NotMutated : O::OutcomeUncertain);
        require(changed && barriers == last && e.error == 0 && fs::exists(added), "EOF insertion rejected and retained");
        if (std::string(mode) != "private") projection_error([&] { f.open(); }, ProjectionCause::ProjectionConflict);
        else { auto q = f.open(); require(recover_activation_plan(q, *f.state, f.expected()).outcome == O::NotMutated, "fresh recovery accounts for private residue"); }
        struct stat retained{}; require(::lstat(added.c_str(), &retained) == 0 &&
            injected.st_dev == retained.st_dev && injected.st_ino == retained.st_ino && injected.st_mode == retained.st_mode &&
            injected.st_uid == retained.st_uid && injected.st_size == retained.st_size &&
            injected.st_mtim.tv_sec == retained.st_mtim.tv_sec && injected.st_mtim.tv_nsec == retained.st_mtim.tv_nsec &&
            injected.st_ctim.tv_sec == retained.st_ctim.tv_sec && injected.st_ctim.tv_nsec == retained.st_ctim.tv_nsec,
            "production preserves injected residue observations");
        if (!bootstrap && std::string(mode) != "private") {
            std::ifstream bytes(added); require(std::string(std::istreambuf_iterator<char>(bytes), {}) == "injected entry", "injected bytes preserved");
        }
        f.pristine(!bootstrap && std::string(mode) != "private");
    }
    std::cout << "6 readdir EOF insertions rejected, errno=0, residue and unrelated state verified\n";
}
void concluding_callback_changes() {
    // D4 model A: mutations follow the affected bytes/inventory observations.
    // Later metadata checks need not expose them. Report actual results,
    // without treating normal recovery as conformance or durability evidence.
    for (const auto target : {"intent", "plan", "inventory", "binding"}) {
        Fixture f; f.publish(); ProjectionIo io; auto read = io.pread; auto open = io.open;
        int barriers = 0, reads = 0; bool armed = false, enumerated = false, changed = false;
        fs::path residue; struct stat before{};
        std::string injected_bytes;
        io.fsync = [&](int fd) { ++barriers; return ::fsync(fd); };
        io.pread = [&](int fd, void* bytes, size_t n, off_t offset) -> ssize_t {
            if (barriers == 7 && offset == 0 && fdpath(fd).ends_with("/plan.toml") && ++reads == 2) armed = true;
            return read(fd, bytes, n, offset);
        };
        io.open = [&](int fd, const char* name, const open_how& how) {
            if (armed && std::string(name) == "." && fdpath(fd) == f.admin()) enumerated = true;
            // Mutate during concluding binding I/O, after document reads AND
            // global enumeration. The binding case changes the already-read
            // generation record while the projection record is being opened.
            // Subsequent metadata checks are not byte/inventory observations.
            if (enumerated && !changed && std::string(name) == "authority.toml") {
                if (std::string(target) == "binding") {
                    residue = f.base / "generations/.athena-projection-authority.toml";
                    auto binding = f.binding; binding.projection_id = ProjectionId(std::string(32, '0'));
                    injected_bytes = serialize(binding); write(residue, injected_bytes);
                } else if (std::string(target) == "inventory") {
                    residue = f.plans() / ".preparations" / std::string(32, 'a'); directory(residue);
                } else {
                    residue = f.published() / (std::string(target) == "intent" ? "intent.toml" : "plan.toml");
                    auto bytes = std::string(target) == "intent" ? serialize(f.intent()) : serialize(d1_test::plan());
                    bytes.back() = '#'; injected_bytes = bytes; write(residue, bytes);
                }
                require(::lstat(residue.c_str(), &before) == 0, "concluding callback residue"); changed = true;
            }
            return open(fd, name, how);
        };
        auto p = f.open(io);
        bool unexpected_rejection = false;
        try {
            const auto result = recover_activation_plan(p, *f.state, f.expected());
            std::cout << "D4 A diagnostic target=" << target
                << " change=NOT_DETECTED recovery=RETURNED outcome(enum)="
                << static_cast<int>(result.outcome)
                << " (not evidence of conformance or current-instance durability)\n";
        } catch (const PlanPersistenceError& e) {
            const auto expected_cause = std::string(target) == "binding" ?
                C::AuthorityMismatch : C::Substituted;
            const bool consistent = changed && barriers == 7 &&
                e.cause == expected_cause && e.error == 0 && e.outcome == O::OutcomeUncertain;
            unexpected_rejection = !consistent;
            std::cout << "D4 A diagnostic target=" << target
                << " recovery=REJECTED cause(enum)=" << static_cast<int>(e.cause)
                << " errno=" << e.error << " outcome(enum)=" << static_cast<int>(e.outcome)
                << (consistent ? " rejection=CONSISTENT_WITH_PERTURBATION" :
                    " rejection=UNEXPECTED change=UNDETERMINED") << '\n';
        }
        require(changed && barriers == 7,
            "A concluding callback injection executed after seven barriers");
        struct stat after{}; require(::lstat(residue.c_str(), &after) == 0 && before.st_dev == after.st_dev &&
            before.st_ino == after.st_ino && before.st_size == after.st_size &&
            before.st_ctim.tv_sec == after.st_ctim.tv_sec && before.st_ctim.tv_nsec == after.st_ctim.tv_nsec,
            "A concluding execution preserves injected instance");
        if (std::string(target) == "inventory") {
            require(fs::is_directory(residue) && fs::is_empty(residue),
                "A injected directory retained unchanged");
        } else {
            std::ifstream bytes(residue, std::ios::binary);
            require(bytes.is_open(), "A injected document remains readable");
            require(std::string(std::istreambuf_iterator<char>(bytes), {}) == injected_bytes,
                "A modified bytes retained");
        }
        if (std::string(target) == "binding") {
            projection_error([&] { f.open(); }, ProjectionCause::BindingConflict);
            write(residue, serialize(f.binding)); require(::lstat(residue.c_str(), &f.protected_files.at(residue)) == 0, "fixture-only binding restoration");
        }
        f.pristine();
        require(!unexpected_rejection, "A recovery rejected with unexpected cause/errno/outcome");
    }
    std::cout << "D4 A: 4 diagnostics recorded; injection, seven barriers, "
        "residue and unrelated state verified; no conformance or durability claim\n";
}
void faults() { barrier_faults(); io_faults(); substitutions(); final_read_substitutions(); }
std::string numbered(std::size_t n) { char b[33]; std::snprintf(b, sizeof(b), "%032zx", n); return b; }
void bounds() {
    Fixture f; auto i = f.intent();
    ActivationPlan plan{d1_test::id, {}, {}};
    auto path = [](std::size_t n, std::size_t length) {
        // Sixteen components preserve depth ordering even in the short tail.
        std::string out; std::size_t remaining = length - 15;
        for (int part = 0; part < 16; ++part) {
            if (part) out += '/';
            const auto size = std::min<std::size_t>(255, remaining - (15 - part));
            auto component = part == 0 ? numbered(n).substr(24) : std::string();
            if (component.size() > size) component.resize(size);
            component += std::string(size - component.size(), 'a'); out += component; remaining -= size;
        }
        return out;
    };
    ActivationPlan one{d1_test::id, {}, {{0, path(0, 4095), EnsureDirectory{}}}};
    const auto one_size = serialize(one).size();
    one.operations.push_back({1, path(1, 4095), EnsureDirectory{}});
    const auto unit = serialize(one).size() - one_size;
    const auto header = one_size - unit; // Nonempty header excludes operations = [].
    std::size_t total = header;
    const auto target = max_document_size - 128;
    for (std::size_t n = 0;; ++n) {
        const auto overhead = unit - 2 * 4095 + std::to_string(n).size() - 1;
        const auto full = overhead + 2 * 4095;
        if (total + full <= target) { plan.operations.push_back({n, path(n, 4095), EnsureDirectory{}}); total += full; }
        else {
            const auto remaining = target - total;
            if (remaining >= overhead + 2 * 64) {
                const auto length = (remaining - overhead) / 2;
                plan.operations.push_back({n, path(n, length), EnsureDirectory{}});
            }
            break;
        }
    }
    const auto pb = serialize(plan), ib = serialize(i);
    require(pb.size() <= max_document_size && ib.size() <= max_document_size && pb.size() + ib.size() > max_document_size,
        "independent document limits permit pair above 16 MiB");
    const auto expected = derive_plan_digest(i, plan);
    auto p = f.open(); auto result = publish_activation_plan(p, *f.state, i, plan);
    require(result.document.digest == expected, "large pair publishes with unchanged D1 digest");
    require(recover_activation_plan(p, *f.state, {i.activation_id, result.document.digest}).outcome == O::NotMutated,
        "large pair recovery");
    f.pristine();
    // At the exact byte bound, malformed syntax is a codec error rather than an
    // administrative quota; above it the read is rejected before parsing.
    for (auto name : {"intent.toml", "plan.toml"}) {
        write(f.published() / "intent.toml", ib); write(f.published() / "plan.toml", pb);
        write(f.published() / name, std::string(max_document_size, 'x'));
        const auto at_bound = error([&] { inspect_activation_plan(p, *f.state, d1_test::id); }, C::InvalidDocument, O::NotMutated);
        require(bool(at_bound.underlying), "exact byte bound reaches the D1 codec");
        try { std::rethrow_exception(at_bound.underlying); }
        catch (const SchemaError& original) { require(original.code() != SchemaErrorCode::Bounds, "exact byte bound is not oversized"); }
        fs::resize_file(f.published() / name, max_document_size + 1);
        const auto above = error([&] { inspect_activation_plan(p, *f.state, d1_test::id); }, C::InvalidDocument, O::NotMutated);
        require(bool(above.underlying), "D1 oversize diagnostic retained");
        try { std::rethrow_exception(above.underlying); }
        catch (const SchemaError& original) { require(original.code() == SchemaErrorCode::Bounds, "independent D1 file bound"); }
    }
}
void limits() {
    Fixture f; auto p = f.open(); ensure_plan_namespace(p, *f.state);
    for (std::size_t n = 0; n < 4096; ++n) directory(f.plans() / ".preparations" / numbered(n));
    p.revalidate();
    error([&] { publish_activation_plan(p, *f.state, f.intent(), d1_test::plan()); }, C::AdministrativeLimit, O::NotMutated);
    require(!fs::exists(f.prepared()), "capacity checked before mkdir");
    directory(f.plans() / ".preparations" / numbered(4096));
    ProjectionIo io; auto direct = io.open; std::size_t opened = 0;
    io.open = [&](int fd, const char* name, const open_how& how) {
        if (fdpath(fd) == (f.plans() / ".preparations").native() && std::strlen(name) == 32) ++opened;
        return direct(fd, name, how);
    };
    projection_error([&] { f.open(io); }, ProjectionCause::ProjectionConflict);
    require(opened == 4096, "first excess entry rejected before opening, independent of readdir order");
    fs::remove(f.plans() / ".preparations" / numbered(4096));
    fs::remove(f.plans() / ".preparations" / numbered(0));
    directory(f.prepared()); write(f.prepared() / "intent.toml", serialize(f.intent())); write(f.prepared() / "plan.toml", serialize(d1_test::plan()));
    auto result = publish_activation_plan(p, *f.state, f.intent(), d1_test::plan());
    require(result.publication == PlanPublication::Published, "rename allowed at combined capacity");
    require(publish_activation_plan(p, *f.state, f.intent(), d1_test::plan()).publication == PlanPublication::AlreadyPublished, "published retry at capacity");
    directory(f.plans() / ".preparations" / numbered(0));
    projection_error([&] { p.revalidate(); }, ProjectionCause::ProjectionConflict); // 4,097 containers, regardless of duplicate.
    fs::remove(f.plans() / ".preparations" / numbered(0));
    fs::remove(f.plans() / ".preparations" / numbered(1)); directory(f.prepared());
    projection_error([&] { p.revalidate(); }, ProjectionCause::ProjectionConflict);
    f.pristine();
    f.state.reset();
    { Fixture x; auto q = x.open();
        for (const auto& name : {"plans", ".plans-bootstrap", "adoptions", "extra"}) directory(x.admin() / name);
        rejects([&] { q.revalidate(); }); }
    { Fixture x; x.publish(); auto q = x.open();
        fs::resize_file(x.published() / "plan.toml", max_document_size + 1);
        projection_error([&] { q.revalidate(); }, ProjectionCause::ProjectionConflict); }
}
ActivationIntent worker_intent(const fs::path& base, const ActivationId& id = d1_test::id) {
    auto i = d1_test::intent(); i.activation_id = id; i.generation_authority = (base / "generations").native();
    i.store_authority = (base / "store").native(); i.projection_root = (base / "projection").native(); return i;
}
ActivationPlan worker_plan(const ActivationId& id = d1_test::id) {
    auto p = d1_test::plan(); p.activation_id = id;
    for (auto& op : p.operations) if (auto* link = std::get_if<CreateLink>(&op.state)) link->new_witness.owner = id;
    return p;
}
void notify_and_wait(int report, int gate) {
    char byte = 'x'; if (::write(report, &byte, 1) != 1) ::_exit(30);
    if (::read(gate, &byte, 1) != 1) ::_exit(31);
}
int worker(int argc, char** argv) {
    require(argc == 9, "worker arguments");
    const fs::path base(argv[2]); const std::string kind(argv[3]); const int at = std::stoi(argv[4]);
    const int report = std::stoi(argv[5]), gate = std::stoi(argv[6]); const std::string variant(argv[7]), action(argv[8]);
    if (action == "concurrent") { char b; require(::read(gate, &b, 1) == 1, "worker start gate"); }
    ProjectionState state((base / "generations").native());
    auto i = worker_intent(base, variant == "different-id" ? d1_test::old_id : d1_test::id);
    if (variant == "different-pair") ++i.target_generation;
    auto plan = worker_plan(i.activation_id);
    ProjectionBinding binding{i.projection_id, i.generation_authority, i.store_authority, i.projection_root};
    athena::store::ObjectStore store(base / "store"); ProjectionIo io;
    int calls = 0;
    if (action == "kill" || action == "kill-recovery") {
        if (kind == "fsync" || kind == "fsync-before") io.fsync = [&](int fd) {
            ++calls;
            if (kind == "fsync-before" && calls == at) notify_and_wait(report, gate);
            const int rc = ::fsync(fd);
            require(rc == 0,"real barrier before crash notification");
            if (kind == "fsync" && calls == at) notify_and_wait(report, gate);
            return rc;
        };
        if (kind == "mkdir") { const auto direct = io.mkdir;
            io.mkdir = [&, direct](int fd, const char* name, mode_t mode) { const int rc = direct(fd, name, mode); if (++calls == at) notify_and_wait(report, gate); return rc; }; }
        if (kind == "mkdir-before") { const auto direct = io.mkdir;
            io.mkdir = [&, direct](int fd, const char* name, mode_t mode) {
                if (++calls == at) notify_and_wait(report,gate);
                return direct(fd,name,mode);
            }; }
        if (kind == "rename-before" || kind == "rename-after") { const auto direct = io.rename;
            io.rename = [&, direct](int a, const char* n, int b, const char* m, unsigned flags) {
                ++calls; if (kind == "rename-before" && calls == at) notify_and_wait(report, gate);
                const int rc = direct(a, n, b, m, flags);
                if (kind == "rename-after" && calls == at) notify_and_wait(report, gate);
                return rc;
            }; }
        if (kind == "partial") {
            // Only this exec worker uses fixed pipe descriptors. No mutable
            // global callback state; production still uses operation-owned I/O.
            require(::dup2(report, 198) == 198 && ::dup2(gate, 199) == 199, "partial worker transport");
            io.durable.write = [](int fd, const void* b, size_t n) -> ssize_t {
                const auto written = ::write(fd, b, std::min<std::size_t>(n, 7));
                notify_and_wait(198, 199); return written;
            };
        }
    }
    auto p = ProjectionContext::open(binding, store, io);
    if (action == "kill-recovery" || action == "retry-recovery") {
        const PlanIdentity expected{i.activation_id,derive_plan_digest(i,plan)};
        const auto result = recover_activation_plan(p,state,expected);
        require(result.publication == PlanPublication::AlreadyPublished && result.outcome == O::NotMutated &&
            result.document.digest == expected.digest,"fresh exec recovery uses independent request identity");
        if (action == "kill-recovery" && kind == "complete") notify_and_wait(report,gate);
        return 0;
    }
    try {
        publish_activation_plan(p, state, i, plan);
        if (action == "kill" && kind == "complete") notify_and_wait(report,gate);
        return 0;
    }
    catch (const PlanPersistenceError& e) {
        if (e.cause == C::PreparationConflict || e.cause == C::BootstrapConflict) return 20;
        if (e.cause == C::PublicationConflict) return 21;
        if (e.cause == C::AdministrativeLimit) return 22;
        throw;
    }
}
pid_t spawn_worker(const Fixture& f, std::string kind, int at, int report, int gate,
    std::string variant, std::string action) {
    const auto pid = ::fork(); require(pid >= 0, "fork worker");
    if (pid == 0) {
        const auto executable = fs::read_symlink("/proc/self/exe").native();
        const auto index = std::to_string(at), output = std::to_string(report), input = std::to_string(gate);
        ::execl(executable.c_str(), executable.c_str(), "worker", f.base.c_str(), kind.c_str(), index.c_str(),
            output.c_str(), input.c_str(), variant.c_str(), action.c_str(), nullptr); ::_exit(32);
    }
    return pid;
}
int wait_worker(pid_t pid) { int status = 0; require(::waitpid(pid, &status, 0) == pid && WIFEXITED(status), "worker normal exit"); return WEXITSTATUS(status); }
void kills() {
    std::vector<std::pair<std::string, int>> events;
    events.emplace_back("mkdir-before",1);
    for (int n = 1; n <= 22; ++n) {
        events.emplace_back("fsync-before",n); events.emplace_back("fsync", n);
    }
    for (int n = 1; n <= 3; ++n) events.emplace_back("mkdir", n);
    for (int n = 1; n <= 2; ++n) { events.emplace_back("rename-before", n); events.emplace_back("rename-after", n); }
    events.emplace_back("partial", 1);
    events.emplace_back("complete",0);
    for (const auto& [kind, at] : events) {
        Fixture f; f.state.reset(); // No inherited State open-file description.
        int report[2], gate[2]; require(::pipe(report) == 0 && ::pipe(gate) == 0, "kill pipes");
        const auto pid = spawn_worker(f, kind, at, report[1], gate[0], "same", "kill");
        ::close(report[1]); ::close(gate[0]); char b;
        require(::read(report[0], &b, 1) == 1, "worker reached precise mutation/barrier boundary");
        require(::kill(pid, SIGKILL) == 0, "SIGKILL worker"); int status;
        require(::waitpid(pid, &status, 0) == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL, "killed independently locked worker");
        ::close(report[0]); ::close(gate[1]);
        const auto original = residue(f,true);
        const auto target = fs::exists(f.prepared()) ? f.prepared() : f.published();
        const auto target_inode = fs::exists(target) ? std::optional<ino_t>{inode(target)} : std::nullopt;
        const auto retry = spawn_worker(f, "none", 0, -1, -1, "same", "retry");
        const bool incomplete = (kind == "mkdir" && at != 2) || kind == "partial" ||
            ((kind == "fsync" || kind == "fsync-before") && at == 9);
        require(wait_worker(retry) == (incomplete ? 20 : 0), "fresh exec retry classifies actual residue");
        f.state = std::make_unique<ProjectionState>(f.binding.generation_authority);
        auto p = f.open();
        if (!incomplete) require(recover_activation_plan(p, *f.state, f.expected()).outcome == O::NotMutated, "fresh recovery after process death");
        else require(!fs::exists(f.published()), "incomplete state never published");
        if (incomplete) require(residue(f,true) == original,"failed crash retry preserves incomplete instances and bytes");
        else if (target_inode) require(inode(f.published()) == *target_inode,"successful crash retry preserves the container inode");
        f.pristine();
    }
    std::cout << "R3: " << events.size() << " publication SIGKILL points; independent exec State, residue and container identity; process-death mechanics, not reboot/power-loss durability\n";
}
// N: process death only, not power loss. Workers exec and acquire their own
// State descriptions; the parent releases its lock before starting each worker.
void recovery_kills() {
    std::vector<std::pair<std::string,int>> events;
    for (int n = 1; n <= 7; ++n) {
        events.emplace_back("fsync-before",n); events.emplace_back("fsync",n);
    }
    events.emplace_back("complete",0);
    for (const auto& [kind,at] : events) {
        Fixture f; f.publish(); const auto original = residue(f,true); f.state.reset();
        int report[2], gate[2]; require(::pipe(report) == 0 && ::pipe(gate) == 0,"recovery kill pipes");
        const auto pid = spawn_worker(f,kind,at,report[1],gate[0],"same","kill-recovery");
        ::close(report[1]); ::close(gate[0]); char reached;
        require(::read(report[0],&reached,1) == 1,"recovery reached actual barrier/completion");
        require(::kill(pid,SIGKILL) == 0,"kill independently locked recovery"); int status;
        require(::waitpid(pid,&status,0) == pid && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,"recovery SIGKILL confirmed");
        ::close(report[0]); ::close(gate[1]);
        require(residue(f,true) == original,"recovery crash preserves all D4 names/instances/bytes"); f.pristine();
        require(wait_worker(spawn_worker(f,"none",0,-1,-1,"same","retry-recovery")) == 0,"fresh exec recovery after kill");
        f.state = std::make_unique<ProjectionState>(f.binding.generation_authority);
        ProjectionIo io; std::vector<std::string> order;
        io.fsync = [&](int fd) { const auto rc = ::fsync(fd); if (!rc) order.push_back(relative(f,fd)); return rc; };
        auto p = f.open(io); const auto baseline = descriptor_count();
        for (int retry = 0; retry < 2; ++retry) {
            order.clear(); const auto result = recover_activation_plan(p,*f.state,f.expected());
            require(result.publication == PlanPublication::AlreadyPublished && result.outcome == O::NotMutated &&
                order == recovery_order && descriptor_count() == baseline,"idempotent recovery completes seven real barriers without leaks");
        }
        require(residue(f,true) == original,"retry retains original publication and document identities"); f.pristine();
    }
    std::cout << "R3: 15 recovery SIGKILL points (before/after all seven fsyncs, after completion); fresh exec State, exact residue, ordered idempotent retries, no leaks\n";
}
void processes() {
    for (auto variant : {"same", "different-pair", "different-id", "capacity-same", "capacity-different-id"}) {
        Fixture f;
        const bool capacity_case = std::string(variant).starts_with("capacity-");
        if (capacity_case) {
            auto p = f.open(); ensure_plan_namespace(p, *f.state);
            for (std::size_t n = 0; n < 4095; ++n) directory(f.plans() / ".preparations" / numbered(n));
        }
        f.state.reset(); int gate[2]; require(::pipe(gate) == 0, "process gate");
        const auto first = spawn_worker(f, "none", 0, -1, gate[0], "same", "concurrent");
        const auto second = spawn_worker(f, "none", 0, -1, gate[0],
            std::string(variant) == "capacity-different-id" ? "different-id" : variant, "concurrent");
        require(::write(gate[1], "xx", 2) == 2, "simultaneous contenders gate"); ::close(gate[0]); ::close(gate[1]);
        const int a = wait_worker(first), b = wait_worker(second);
        if (std::string(variant) == "capacity-different-id") require((a == 0 && b == 22) || (a == 22 && b == 0), "one capacity slot reserved under State: " + std::to_string(a) + ", " + std::to_string(b));
        else if (std::string(variant) == "different-pair") require((a == 0 && b == 21) || (a == 21 && b == 0), "different-pair conflict under State");
        else require(a == 0 && b == 0, "same-pair/different-ID serialized convergence");
        f.state = std::make_unique<ProjectionState>(f.binding.generation_authority); auto p = f.open(); p.revalidate(); f.pristine();
    }
}
// T17 only: ordinary fixtures deliberately keep their /tmp default. ext4 is
// supplied by the qualification host, never provisioned using a block device.
fs::path qualification_parent() {
    const char* value = std::getenv("ATHENA_D4_PRIVILEGED_TMPDIR");
    const fs::path parent = value ? value : "/tmp";
    require(parent.is_absolute() && fs::is_directory(parent), "T17 parent must be an existing absolute directory");
    return parent;
}
struct QualificationTree {
    fs::path base;
    bool preserve = false;
    explicit QualificationTree(const fs::path& parent) : base(Fixture::temporary(parent)) {}
    ~QualificationTree() noexcept {
        if (!preserve) {
            std::error_code ec; fs::remove_all(base, ec);
            if (ec) {
                qualification_cleanup_failed = true;
                std::fprintf(stderr, "ERROR: T17 cleanup '%s': %s\n", base.c_str(), ec.message().c_str());
            }
        }
    }
};
struct QualificationMounts {
    fs::path base;
    bool& preserve;
    bool* enclosing_preserve;
    std::vector<fs::path> targets;
    bool failed = false;
    int (*unmount)(const char*) = ::umount;
    QualificationMounts(fs::path root, bool& keep, bool* enclosing = nullptr)
        : base(std::move(root)), preserve(keep), enclosing_preserve(enclosing) {}
    // Register before mounting so allocation cannot strand an untracked mount.
    bool mount(const char* source, const fs::path& target, const char* type, unsigned long flags, const char* data = nullptr) {
        targets.push_back(target);
        if (::mount(source, target.c_str(), type, flags, data) == 0) return true;
        const int number = errno; targets.pop_back();
        if (number != EPERM && number != EACCES)
            throw std::system_error(number, std::generic_category(), "T17 mount " + target.native());
        std::cout << "SKIP T17 mount " << target << ": " << std::strerror(number) << '\n';
        return false;
    }
    bool cleanup() noexcept {
        // A descendant guard propagates preservation before this guard unwinds.
        // Never detach its container or retry a failed cleanup in a destructor.
        if (failed || preserve) return false;
        while (!targets.empty()) {
            if (unmount(targets.back().c_str()) == 0) { targets.pop_back(); continue; }
            const int number = errno;
            preserve = true; failed = true; qualification_cleanup_failed = true;
            if (enclosing_preserve) *enclosing_preserve = true;
            std::fprintf(stderr, "ERROR: T17 umount '%s': %s (errno %d); preserving fixture '%s'\n",
                targets.back().c_str(), std::strerror(number), number, base.c_str());
            return false;
        }
        return true;
    }
    void finish() { require(cleanup(), "T17 cleanup failed; fixture preserved, qualification FAILED"); }
    ~QualificationMounts() noexcept { cleanup(); }
};
void qualification_roundtrip(Fixture& f) {
    require(f.publish().outcome == O::MutatedDurably, "T17 fresh publication");
    auto p = f.open();
    inspect_activation_plan(p, *f.state, d1_test::id);
    require(recover_activation_plan(p, *f.state, f.expected()).outcome == O::NotMutated, "T17 durable recovery");
    f.pristine();
}
// No mounts or external directories: simulate syscall outcomes to cover unwinding.
int simulated_unmount_calls = 0;
int simulated_unmount(const char*) {
    ++simulated_unmount_calls;
    if (simulated_unmount_calls == 1) return 0;
    errno = EBUSY; return -1;
}
void qualification_cleanup_test() {
    require(!qualification_cleanup_failed, "clean initial cleanup status");
    simulated_unmount_calls = 0;
    bool keep = false;
    {
        QualificationMounts mounts("/unused", keep);
        mounts.unmount = simulated_unmount;
        mounts.targets = {"/unused/container", "/unused/nested", "/unused/leaf"};
        require(!mounts.cleanup() && keep && mounts.targets.size() == 2 &&
            mounts.targets.back() == "/unused/nested", "failed mount and container remain tracked");
        require(!mounts.cleanup() && simulated_unmount_calls == 2, "failed cleanup is idempotent");
    }
    require(simulated_unmount_calls == 2, "destructor never retries failed cleanup");
    qualification_cleanup_failed = false;
    simulated_unmount_calls = 1;
    fs::path retained_tree, retained_fixture;
    {
        QualificationTree tree(athena::test::temporary_root());
        directory(tree.base / "container");
        QualificationMounts outer(tree.base, tree.preserve);
        outer.unmount = simulated_unmount;
        outer.targets = {tree.base / "container"};
        const auto base = tree.base;
        retained_tree = base;
        fs::path fixture;
        try {
            Fixture f(tree.base / "container");
            fixture = f.base;
            retained_fixture = fixture;
            QualificationMounts inner(f.base, f.preserve_on_cleanup, &tree.preserve);
            inner.unmount = simulated_unmount;
            inner.targets = {f.base / "nested"};
            throw std::runtime_error("simulated test failure");
        } catch (const std::runtime_error&) {
            require(!fixture.empty() && tree.preserve && qualification_cleanup_failed,
                "exception propagates descendant cleanup failure");
        }
        require(fs::exists(fixture / "store"), "fixture retained after destructor during unwinding");
        require(!outer.cleanup() && outer.targets.size() == 1 && simulated_unmount_calls == 2,
            "enclosing filesystem never unmounted after descendant failure");
        require(fs::exists(base / "container"), "container retained");
    }
    require(simulated_unmount_calls == 2 && fs::exists(retained_fixture / "store"),
        "outer destructors preserve the complete fixture and never unmount its container");
    // The simulated mounts never existed; remove only this private tree.
    fs::remove_all(retained_tree);
    qualification_cleanup_failed = false;
    bool success_keep = false;
    QualificationMounts success("/unused", success_keep);
    success.unmount = simulated_unmount;
    simulated_unmount_calls = 0;
    success.targets = {"/unused/leaf"};
    require(success.cleanup() && success.cleanup() && success.targets.empty() && simulated_unmount_calls == 1,
        "successful cleanup is idempotent");
}
bool qualification_mount_prerequisites(const std::string& which) {
    if (::geteuid() != 0) {
        std::cout << "SKIP T17 " << which << ": effective root required\n";
        return false;
    }
    __user_cap_header_struct header{_LINUX_CAPABILITY_VERSION_3, 0};
    __user_cap_data_struct caps[2]{};
    if (::syscall(SYS_capget, &header, caps) != 0)
        throw std::system_error(errno, std::generic_category(), "T17 capget");
    if (!(caps[CAP_SYS_ADMIN / 32].effective & (1U << (CAP_SYS_ADMIN % 32)))) {
        std::cout << "SKIP T17 " << which << ": effective CAP_SYS_ADMIN required\n";
        return false;
    }
    if (which == "tmpfs") {
        std::ifstream filesystems("/proc/filesystems");
        require(filesystems.is_open(), "T17 read /proc/filesystems");
        std::string line; bool available = false;
        while (std::getline(filesystems, line)) {
            std::istringstream fields(line); std::string field;
            while (fields >> field) if (field == "tmpfs") available = true;
        }
        require(filesystems.eof() && !filesystems.bad(), "T17 filesystem prerequisites read");
        if (!available) { std::cout << "SKIP T17 tmpfs: filesystem unavailable\n"; return false; }
    }
    return true;
}
int privileged(const std::string& which, bool report = true) {
    require(which == "owner" || which == "bind" || which == "nested" || which == "tmpfs" || which == "ext4",
        "unknown T17 case");
    if ((which == "tmpfs" || which == "bind" || which == "nested") &&
        !qualification_mount_prerequisites(which)) return 77;
    if (which == "ext4") {
        const char* value = std::getenv("ATHENA_D4_EXT4_TMPDIR");
        if (!value) { std::cout << "SKIP T17 ext4: ATHENA_D4_EXT4_TMPDIR not supplied (existing ext4 directory required)\n"; return 77; }
        const fs::path parent(value);
        require(parent.is_absolute() && fs::is_directory(parent), "T17 ext4 parent must exist and be absolute");
        struct statfs info{}; require(::statfs(parent.c_str(), &info) == 0, "T17 ext4 statfs");
        // ext2/3 share this magic; require the kernel's mounted filesystem type too.
        std::ifstream mounts("/proc/self/mountinfo"); std::string line; bool ext4 = false;
        require(mounts.is_open(), "T17 open mountinfo");
        struct statx identity{};
        require(::statx(AT_FDCWD, parent.c_str(), 0, STATX_MNT_ID, &identity) == 0 &&
            (identity.stx_mask & STATX_MNT_ID), "T17 ext4 mount identity");
        while (std::getline(mounts, line)) {
            if (line.starts_with(std::to_string(identity.stx_mnt_id) + " ") && line.find(" - ext4 ") != std::string::npos) ext4 = true;
        }
        require(mounts.eof() && !mounts.bad(), "T17 read mountinfo");
        require(info.f_type == EXT4_SUPER_MAGIC && ext4, "T17 supplied directory is not mounted ext4");
        Fixture f(parent); qualification_roundtrip(f);
    } else if (which == "tmpfs") {
        QualificationTree tree(qualification_parent());
        directory(tree.base / "tmpfs");
        QualificationMounts mounts(tree.base, tree.preserve);
        if (!mounts.mount("tmpfs", tree.base / "tmpfs", "tmpfs", MS_NODEV | MS_NOSUID, "mode=0700,size=64m")) return 77;
        {
            Fixture f(tree.base / "tmpfs"); qualification_roundtrip(f); auto p = f.open();
            QualificationMounts crossing(f.base, f.preserve_on_cleanup, &tree.preserve);
            const auto target = f.plans() / ".preparations";
            if (!crossing.mount("tmpfs", target, "tmpfs", MS_NODEV | MS_NOSUID, "mode=0700,size=8m")) {
                // RAII releases the fixture before its containing filesystem.
                return 77;
            }
            struct stat before{}, after{};
            require(::stat(f.plans().c_str(), &before) == 0 && ::stat(target.c_str(), &after) == 0 &&
                before.st_dev != after.st_dev, "T17 real different-device tmpfs crossing");
            projection_error([&] { p.revalidate(); }, ProjectionCause::MountCrossing);
            error([&] { inspect_activation_plan(p, *f.state, d1_test::id); }, C::UnsafeEntry, O::NotMutated);
            error([&] { recover_activation_plan(p, *f.state, f.expected()); }, C::UnsafeEntry, O::NotMutated);
            crossing.finish(); p.revalidate(); f.pristine();
        }
        mounts.finish();
    } else {
        if (::geteuid() != 0) { std::cout << "SKIP T17 " << which << ": effective root required\n"; return 77; }
        Fixture f(qualification_parent()); qualification_roundtrip(f); auto p = f.open();
        if (which == "owner") {
            const auto file = f.published() / "plan.toml";
            const auto unsafe_metadata = [&] {
                projection_error([&] { p.revalidate(); }, ProjectionCause::ProjectionConflict);
                error([&] { inspect_activation_plan(p, *f.state, d1_test::id); }, C::UnsafeEntry, O::NotMutated);
                error([&] { recover_activation_plan(p, *f.state, f.expected()); }, C::UnsafeEntry, O::NotMutated);
                error([&] { publish_activation_plan(p, *f.state, f.intent(), d1_test::plan()); }, C::UnsafeEntry, O::NotMutated);
            };
            require(::chown(file.c_str(), 1, -1) == 0, "T17 real foreign uid");
            unsafe_metadata();
            require(::chown(file.c_str(), 0, -1) == 0, "T17 restore owner");
            for (mode_t mode : {0620, 0640, 04600}) {
                require(::chmod(file.c_str(), mode) == 0, "T17 unsafe metadata");
                unsafe_metadata();
            }
            require(::chmod(file.c_str(), 0600) == 0, "T17 restore mode");
        } else {
            require(which == "bind" || which == "nested", "unknown T17 case");
            directory(f.base / "mount-source");
            const auto target = f.plans() / ".preparations";
            QualificationMounts mounts(f.base, f.preserve_on_cleanup);
            // The root mount is allowed by D2. Open it successfully before
            // installing a descendant crossing, so rejection is attributable
            // to the nested mount rather than to an already invalid outer tree.
            std::optional<ProjectionContext> mounted_context;
            if (which == "nested") {
                const auto root = f.base / "projection";
                if (!mounts.mount(root.c_str(), root, nullptr, MS_BIND)) return 77;
                mounted_context.emplace(f.open());
                mounted_context->revalidate();
            }
            if (!mounts.mount((f.base / "mount-source").c_str(), target, nullptr, MS_BIND)) {
                mounted_context.reset(); mounts.finish(); return 77;
            }
            struct statx source{}, crossed{};
            require(::statx(AT_FDCWD, (f.base / "mount-source").c_str(), 0, STATX_BASIC_STATS | STATX_MNT_ID, &source) == 0 &&
                ::statx(AT_FDCWD, target.c_str(), 0, STATX_BASIC_STATS | STATX_MNT_ID, &crossed) == 0 &&
                (source.stx_mask & STATX_MNT_ID) && (crossed.stx_mask & STATX_MNT_ID) &&
                source.stx_dev_major == crossed.stx_dev_major && source.stx_dev_minor == crossed.stx_dev_minor &&
                source.stx_mnt_id != crossed.stx_mnt_id, "T17 real same-device different-mount crossing");
            auto& crossed_context = mounted_context ? *mounted_context : p;
            projection_error([&] { crossed_context.revalidate(); }, ProjectionCause::MountCrossing);
            error([&] { inspect_activation_plan(crossed_context, *f.state, d1_test::id); }, C::UnsafeEntry, O::NotMutated);
            error([&] { recover_activation_plan(crossed_context, *f.state, f.expected()); }, C::UnsafeEntry, O::NotMutated);
            error([&] { publish_activation_plan(crossed_context, *f.state, f.intent(), d1_test::plan()); }, C::UnsafeEntry, O::NotMutated);
            mounted_context.reset(); // Release fds on the allowed root mount.
            mounts.finish();
        }
        p.revalidate(); f.pristine();
    }
    require(!qualification_cleanup_failed, "T17 cleanup failed; qualification FAILED");
    if (report) std::cout << "PASS T17 " << which << " (filesystem ordering/namespace mechanics; no power-loss qualification)\n";
    return 0;
}

}
int main(int argc, char** argv) {
    int qualification_status = 0;
    const int result = d1_test::run([&]() {
        const std::string mode = argc > 1 ? argv[1] : "ordinary";
        if (mode == "qualification-cleanup") qualification_cleanup_test();
        else if (mode == "ordinary") ordinary();
        else if (mode == "recovery-cloexec") recovery_cloexec_errors();
        else if (mode == "locator-cloexec") locator_cloexec_error();
        else if (mode == "filesystem-errors") filesystem_error_integrity();
        else if (mode == "recovery-kills") recovery_kills();
        else if (mode == "recovery-evidence") { recovery_evidence(); recovery_substitutions(); }
        else if (mode == "recovery-capacity") recovery_capacity();
        else if (mode == "recovery-targeted") { recovery_evidence(); recovery_substitutions(); substitutions(); final_read_substitutions(); late_binding_changes(true); eof_inventory_changes(); boundaries("recovery-targeted"); }
        else if (mode == "publication-evidence") { publication_evidence(); publication_registry_faults(); }
        else if (mode == "publication-capacity") publication_capacity();
        else if (mode == "publication-registry") publication_registry_faults();
        else if (mode == "inspection") inspection_evidence();
        else if (mode == "bootstrap-evidence") { bootstrap(); bootstrap_evidence(); }
        else if (mode == "evidence") { evidence_foundation(); evidence_structural(); evidence_statx_fields(); }
        else if (mode == "late-bootstrap") late_bootstrap_substitutions();
        else if (mode == "late-binding") late_binding_changes();
        else if (mode == "concluding-callback-diagnostics") concluding_callback_changes();
        else if (mode == "conclusion") { late_binding_changes(true); bootstrap_binding_changes(); eof_inventory_changes(); }
        else if (mode == "faults") { faults(); late_bootstrap_substitutions(); late_binding_changes(); late_binding_changes(true); bootstrap_binding_changes(); eof_inventory_changes(); }
        else if (mode == "boundaries") boundaries();
        else if (mode.starts_with("boundaries-")) {
            require(argc == 2 || argc == 4, "boundary mode takes optional inclusive first/last positions");
            if (argc == 4) boundaries(mode.substr(11), std::stoull(argv[2]), std::stoull(argv[3]));
            else boundaries(mode.substr(11));
        }
        else if (mode == "limits") limits();
        else if (mode == "fixture-construction-failure") {
            const auto parent = fs::temp_directory_path() / ("athena-d4-exception-" + std::to_string(::getpid()));
            fs::create_directory(parent);
            const auto before = fs::directory_iterator(parent) == fs::directory_iterator{};
            require(before, "D4 exception fixture parent initially empty");
            ::setenv("ATHENA_TEST_FAIL_D4_FIXTURE_SETUP", "1", 1);
            try { Fixture f(parent); throw std::runtime_error("D4 injected failure was not raised"); }
            catch (const std::runtime_error& e) {
                require(std::string(e.what()) == "injected D4 fixture-construction failure", "D4 expected injected exception");
            }
            ::unsetenv("ATHENA_TEST_FAIL_D4_FIXTURE_SETUP");
            require(fs::directory_iterator(parent) == fs::directory_iterator{}, "D4 constructor failure removed owned temporary directory");
            fs::remove(parent);
        }
        else if (mode == "bounds") bounds();
        else if (mode == "kills") kills();
        else if (mode == "processes") processes();
        else if (mode == "worker") ::exit(worker(argc, argv));
        else if (mode == "privileged") {
            bool skipped = false;
            for (const auto* name : {"owner", "bind", "nested", "tmpfs", "ext4"}) {
                skipped |= privileged(name, false) == 77;
                require(!qualification_cleanup_failed, "T17 cleanup failed");
            }
            if (!skipped) std::cout << "PASS T17 privileged (all individual cases completed and cleaned)\n";
            qualification_status = skipped ? 77 : 0;
        }
        else if (mode.starts_with("privileged-")) {
            const int status = privileged(mode.substr(11));
            qualification_status = status;
        }
        else throw std::runtime_error("unknown test mode");
    });
    if (qualification_cleanup_failed) { std::cerr << "FAIL: fixture/mount cleanup\n"; return 1; }
    return result ? result : qualification_status;
}

#endif // ATHENA_D4_LOCATOR_FAULT_LIBRARY
