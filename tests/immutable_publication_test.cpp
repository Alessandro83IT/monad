// SPDX-License-Identifier: GPL-3.0-or-later
#include "filesystem_fixture_support.hpp"
#include "object_store_test_support.hpp"
#include "generation/publication.hpp"
#include "generation/generation.hpp"
#include "core/detail/filesystem.hpp"
#include "store/gc.hpp"
#include "store/metadata.hpp"
#include <limits>
#include <signal.h>
#include <linux/magic.h>
#include <set>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <cstddef>
namespace d = athena::core::detail;
namespace gp = athena::generation::publication;
namespace {
int fault = 0, pause_at = 0, ready = -1, gate = -1;
bool visible = false;
fs::path corrupt_path;
int cleanup_calls = 0;
int write_calls = 0, sync_calls = 0, sync_failure_at = 0;
bool existing_winner = false;
fs::path observed_root;
NormalizedArtifact observed_model;
std::set<std::string> synchronized;
void barrier_point(int point) {
    if (pause_at != point) return;
    require(::write(ready, "r", 1) == 1, "ready barrier");
    char token; require(::read(gate, &token, 1) == 1, "gate barrier");
}
ssize_t write_op(int fd, const void* p, size_t n) {
    ++write_calls;
    if (fault == 1 || fault == 6) { errno = ENOSPC; return -1; }
    if (fault == 15) {
        if (write_calls == 1) { errno = EINTR; return -1; }
        n = std::min<std::size_t>(n, 2);
    }
    if (fault == 16) return 0;
    return ::write(fd, p, n);
}
int sync_op(int fd) {
    ++sync_calls;
    if (sync_failure_at == sync_calls) { errno = EIO; return -1; }
    const auto status = d::stat_fd(fd);
    const bool file = S_ISREG(status.st_mode);
    const auto locator = fs::read_symlink("/proc/self/fd/" + std::to_string(fd));
    const auto name = locator.filename().string();
    if ((fault == 11 && !file && !visible && name == "sha256") ||
        ((fault == 12 || fault == 13) && !file && visible && name == "sha256") ||
        (fault == 14 && !file && visible && name.starts_with(".publish-")) ||
        (fault == 17 && file && existing_winner) ||
        (fault == 18 && !file && existing_winner)) {
        errno = EIO; return -1;
    }
    if (!observed_root.empty()) {
        const auto locator = fs::read_symlink("/proc/self/fd/" + std::to_string(fd));
        const auto path = locator.lexically_relative(observed_root).native();
        const auto key = path == "." ? std::string{} : path;
        require(!S_ISLNK(status.st_mode), "synchronization followed a link");
        require(!(status.st_mode & 0222), "synchronized before final modes");
        if (!file) for (const auto& entry : observed_model.entries) {
            if (std::holds_alternative<SymbolicLink>(entry.value)) continue;
            if (key.empty() || entry.path.starts_with(key + "/"))
                require(synchronized.contains(entry.path), "directory synchronized before descendant");
        }
        synchronized.insert(key);
    }
    if (((fault == 2 || fault == 8) && file) || (fault == 3 && !file && !visible) || (fault == 5 && visible)) {
        errno = EIO; return -1;
    }
    if (fault == 7 && !file && corrupt_path.empty()) {
        const auto locator = fs::read_symlink("/proc/self/fd/" + std::to_string(fd));
        if (fs::exists(locator / "bin/tool")) corrupt_path = locator / "bin/tool";
    }
    if (fault == 7 && !file && !visible && !corrupt_path.empty()) {
        ::chmod(corrupt_path.c_str(), 0644);
        std::ofstream(corrupt_path) << "corrupted before verification";
        corrupt_path.clear();
    }
    const auto result = ::fsync(fd);
    if (visible && result == 0) barrier_point(3);
    return result;
}
int rename_op(int a, const char* x, int b, const char* y, unsigned f) {
    barrier_point(1);
    if (fault == 4) { errno = EIO; return -1; }
    const auto result = d::linux_syscalls().rename(a, x, b, y, f);
    if (result < 0 && errno == EEXIST) existing_winner = true;
    if (result == 0) {
        visible = true;
        if (fault == 10) {
            const auto namespace_path = fs::read_symlink("/proc/self/fd/" + std::to_string(b));
            const auto target = namespace_path / y / "bin/tool";
            ::chmod(target.c_str(), 0644); std::ofstream(target) << "post-publication corruption";
        }
        barrier_point(2);
    }
    return result;
}
int unlink_op(int a, const char* n, int f) {
    ++cleanup_calls;
    if (fault == 6 || fault == 8 || fault == 13 || fault == 19) { errno = EACCES; return -1; }
    return ::unlinkat(a, n, f);
}
d::Syscalls ops() { auto o = d::linux_syscalls(); o.write = write_op; o.fsync = sync_op; o.rename = rename_op; o.unlink = unlink_op; return o; }
int unsupported_fs(int, struct statfs* value) { *value = {}; value->f_type = OVERLAYFS_SUPER_MAGIC; return 0; }
void reset(int f = 0) {
    fault = f; visible = false; existing_winner = false; pause_at = 0;
    write_calls = 0; sync_calls = 0; sync_failure_at = 0;
}
template<class F> void persistence_failure(int f, bool uncertain, F call) {
    reset(f);
    try { call(); throw std::runtime_error("expected persistence failure"); }
    catch (const d::PersistenceError& e) { require(e.outcome_uncertain == uncertain &&
        e.error == (f == 1 || f == 6 ? ENOSPC : EIO), "failure classification/primary errno");
        const auto expected = f == 1 || f == 6 || f == 16 ? d::PersistenceError::Stage::Write :
            f == 2 || f == 8 || f == 17 ? d::PersistenceError::Stage::FileBarrier :
            f == 4 || f == 10 ? d::PersistenceError::Stage::Publication : d::PersistenceError::Stage::DirectoryBarrier;
        require(e.stage == expected, "failure stage lost");
        if (f == 6 || f == 8 || f == 13) require(!e.cleanup_failure.empty(), "cleanup secondary detail lost"); }
    reset();
}
template<class F> void rejects_state(F call) {
    try { call(); } catch (const std::exception&) { return; }
    require(false, "unsafe or corrupt generation state accepted");
}
void forbid_read_mutation() {
    // Applied only in an isolated child: any barrier, preflight or mutation fails.
    const sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_fsync, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EIO),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_fdatasync, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EIO),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_fstatfs, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EIO),
#ifdef SYS_mkdir
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_mkdir, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EIO),
#endif
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_mkdirat, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EIO),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_renameat2, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EIO),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_unlinkat, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EIO),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)
    };
    const sock_fprog program{static_cast<unsigned short>(std::size(filter)), const_cast<sock_filter*>(filter)};
    require(::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0, "read-only syscall guard privileges");
    require(::prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == 0, "read-only syscall guard");
}
void read_and_visibility_cases(const fs::path& root) {
    using namespace athena::core;
    const auto state = root / "read-state", store = root / "read-store";
    athena::generation::GenerationManager manager(state);
    manager.initialize();
    const auto artifact = ArtifactId("sha256:" + std::string(64, 'c'));
    const RepositorySnapshotRef snapshot{RepositoryId("repo"), RepositoryRevision("rev"), SchemaVersion(1),
        ContentDigest("sha256:" + std::string(64, 'a')), SnapshotId("sha256:" + std::string(64, 'b'))};
    const athena::realization::RealizationRecord record{
        {PackageId("object"), PackageVersion("1"), snapshot.repository_id, snapshot,
         RepositoryProvenance{snapshot.repository_id, snapshot.revision, MetadataReference("recipe"), snapshot}},
        artifact, {}, ArtifactOrigin::AmbientSourceBuild};
    const auto legacy = store / "legacy-1";
    fixture_fs::create_directories(legacy);
    athena::store::write_metadata(legacy, {"legacy", "1", "", "", "", {}});
    // A byte-known namespace fixture tests reachability, not scanner eligibility.
    const auto retained_object = store / "objects/v1/sha256" / artifact.value().substr(7);
    fixture_fs::create_directories(retained_object);
    const auto published = manager.create_unprotected_for_test({{"legacy", legacy.string()}}, {record});
    require(manager.current().id == 0 && published.id == 1, "publication selected current");
    const auto missing = root / "missing-read-state";
    auto proc = d::open_directory("/proc/sys");
    rejects_state([&] { d::require_durable_directory(proc.get()); });
    const auto child = ::fork(); require(child >= 0, "read-only enumeration fork");
    if (child == 0) {
        try {
            forbid_read_mutation();
            require(manager.list().size() == 2 && manager.get(1).id == 1 && manager.current().id == 0,
                    "read-only inspection failed under syscall guard");
            athena::store::GarbageCollector gc(store, state);
            // C collection is explicitly a mutating locked operation, even
            // when no payload is unreachable. Only list/get remain read-only.
            require(fs::exists(legacy / ".athena/metadata.toml") && fs::exists(retained_object),
                    "retained payload disappeared");
            require(athena::generation::GenerationManager(missing).list().empty(), "missing namespace not empty");
            athena::generation::GenerationManager unsupported("/proc/sys",
                athena::generation::GenerationManager::GenerationDirectoryTag{});
            require(unsupported.list().empty(), "unsupported mutation filesystem could not be read");
            ::_exit(0);
        } catch (const std::exception& e) { std::cerr << "read-only child: " << e.what() << '\n'; ::_exit(1); }
    }
    int status;
    require(::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "read-only enumeration attempted mutation/barrier/preflight");
    require(!fs::exists(missing), "enumeration created missing state");
    require(manager.create_unprotected_for_test({}).id == 2 && manager.current().id == 0, "unselected generation ignored by allocation");
    fs::create_directory(state / "generations/3");
    rejects_state([&] { manager.list(); }); // Incomplete authoritative metadata must not become empty.
}
void locator_cases(const fs::path& root) {
    const auto a = root / "relative-a", b = root / "relative-b";
    fixture_fs::create_directories(a); fixture_fs::create_directories(b);
    const auto child = ::fork(); require(child >= 0, "relative authority fork");
    if (child == 0) {
        try {
            fs::current_path(a);
            const std::vector<fs::path> locators{a / "absolute", a / "absolute/", "./generations", "generations/", "./relative/path/"};
            const std::vector<fs::path> custom_locators{a / "tagged-absolute", a / "tagged-absolute/", "./tagged-generations", "tagged-generations/", "./tagged-relative/path/"};
            std::vector<athena::generation::GenerationManager> defaults, tagged;
            for (std::size_t i = 0; i < locators.size(); ++i) {
                defaults.emplace_back(locators[i]);
                tagged.emplace_back(custom_locators[i], athena::generation::GenerationManager::GenerationDirectoryTag{});
            }
            fs::current_path(b);
            for (std::size_t i = 0; i < locators.size(); ++i) {
                const auto bound = (locators[i].is_absolute() ? locators[i] : a / locators[i]).lexically_normal();
                defaults[i].initialize();
                const auto generation = defaults[i].create_unprotected_for_test({});
                require(defaults[i].get(generation.id).id == generation.id && defaults[i].current().id == 0,
                        "default authority binding changed");
                require(fs::exists(bound / "generations" / std::to_string(generation.id) / "generation.toml"), "default locator spelling");
                tagged[i].initialize();
                const auto custom = tagged[i].create_unprotected_for_test({});
                const auto custom_bound = (custom_locators[i].is_absolute() ? custom_locators[i] : a / custom_locators[i]).lexically_normal();
                require(tagged[i].get(custom.id).id == custom.id && tagged[i].current().id == 0, "tagged authority binding changed");
                require(fs::exists(custom_bound / std::to_string(custom.id) / "generation.toml"), "tagged locator spelling");
            }
            require(fs::is_empty(b), "authority followed changed cwd");
            ::_exit(0);
        } catch (const std::exception& e) { std::cerr << "locator child: " << e.what() << '\n'; ::_exit(1); }
    }
    int status;
    require(::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0, "locator regression");
    const auto outside = root / "linked-state";
    athena::generation::GenerationManager outside_manager(outside); outside_manager.initialize();
    fs::create_directory_symlink(outside, root / "state-link");
    fs::create_directory_symlink(outside / "generations", root / "generation-link");
    athena::generation::GenerationManager linked(root / "./state-link/");
    athena::generation::GenerationManager tagged_link(root / "./generation-link/",
        athena::generation::GenerationManager::GenerationDirectoryTag{});
    for (auto* manager : {&linked, &tagged_link}) {
        rejects_state([&] { manager->initialize(); });
        rejects_state([&] { manager->create({}); });
        rejects_state([&] { manager->list(); });
        rejects_state([&] { manager->get(0); });
        rejects_state([&] { manager->current(); });
    }
    rejects_state([&] { athena::generation::GenerationManager unsafe(root / "state-link/../hidden"); });
    rejects_state([&] { athena::generation::GenerationManager unsafe(root / "generation-link/../hidden",
        athena::generation::GenerationManager::GenerationDirectoryTag{}); });
    require(outside_manager.list().size() == 1 && outside_manager.current().id == 0, "symlink authority changed target");
    const auto broken = root / "broken-namespace";
    fs::create_directory(broken); std::ofstream(broken / "generations") << "not a directory";
    rejects_state([&] { athena::generation::GenerationManager(broken).list(); });
}
void crash_case(const fs::path& parent, int point, bool object = false, bool mechanics_only = false) {
    fs::create_directory(parent);
    auto model = payload_model(); auto id = derive_artifact_id(model);
    const auto source = parent / "source";
    if (object) { fs::create_directory(source); mechanics::materialize(source, model); }
    ObjectStore store(parent);
    const auto destination = object ? store.object_path(id) : parent / "7";
    int r[2], g[2]; require(::pipe(r) == 0 && ::pipe(g) == 0, "crash pipes");
    auto pid = ::fork(); require(pid >= 0, "crash fork");
    if (pid == 0) {
        ::close(r[0]); ::close(g[1]); ready = r[1]; gate = g[0]; pause_at = point; visible = false;
        try {
            if (object && mechanics_only) {
                // Byte-known tree test of real durability/publication helpers.
                // This does not bypass the scanner in ObjectStore production.
                auto ns = d::open_durable_directory(destination.parent_path());
                const auto name = d::private_directory(ns.get());
                const auto payload = destination.parent_path() / name;
                mechanics::materialize(payload, model); mechanics::finalize(payload, model);
                mechanics::synchronize(payload, model, ops());
                exact_payload(payload, model, true);
                d::publish_tree(ns.get(), name, ns.get(), id.value().substr(7), ops());
            } else if (object) store.publish_unprotected_for_test(source, id, ops());
            else gp::persist(parent, 7, "complete metadata\n", ops());
            ::_exit(2);
        } catch (const std::exception& e) { std::cerr << "crash child: " << e.what() << "\n"; ::_exit(3); }
    }
    ::close(r[1]); ::close(g[0]); char token;
    require(::read(r[0], &token, 1) == 1, "crash boundary ready");
    require(fs::exists(destination) == (point != 1), "crash namespace visibility");
    if (point != 1) {
        if (object) exact_payload(destination, model, true);
        else require(read_bytes(destination / "generation.toml") == "complete metadata\n", "complete visible metadata");
    }
    require(::kill(pid, SIGKILL) == 0, "kill at barrier");
    int status; require(::waitpid(pid, &status, 0) == pid && WIFSIGNALED(status), "killed child");
    ::close(r[0]); ::close(g[1]);
}
}
int main(int argc, char**) {
    StoreFixture fixture;
    if (argc == 1) {
    locator_cases(fixture.root);
    read_and_visibility_cases(fixture.root);
    const auto gen = fixture.root / "generations"; fs::create_directory(gen);
    for (int f : {1, 2, 3, 4, 5}) {
        const auto id = static_cast<std::uint64_t>(f);
        persistence_failure(f, f == 5, [&] { gp::persist(gen, id, "metadata", ops()); });
        require(fs::exists(gen / std::to_string(id)) == (f == 5), "generation failure visibility");
    }
    cleanup_calls = 0;
    persistence_failure(6, false, [&] { gp::persist(gen, 60, "metadata", ops()); });
    require(cleanup_calls > 0 && !fs::exists(gen / "60"), "cleanup did not preserve primary");
    gp::persist(gen, 6, "winner");
    bool collision = false;
    try { gp::persist(gen, 6, "loser"); } catch (const std::runtime_error&) { collision = true; }
    require(collision, "collision accepted");
    require(read_bytes(gen / "6/generation.toml") == "winner", "generation overwritten");
    for (auto name : {"", "01", "+1", "-1", "1suffix", " 1", ".publish-1"}) require(!gp::canonical_id(name), "noncanonical id");
    require(gp::canonical_id("0") == 0 && gp::canonical_id("9223372036854775806") == gp::max_id - 1 &&
            gp::canonical_id("9223372036854775807") == gp::max_id, "canonical persisted limits");
    for (auto name : {"9223372036854775808", "18446744073709551615", "18446744073709551616"}) {
        try { gp::canonical_id(name); require(false, "overflow accepted"); } catch (const std::overflow_error&) {}
    }
    const auto boundary_state = fixture.root / "boundary-state";
    athena::generation::GenerationManager boundary(boundary_state); boundary.initialize();
    const auto boundary_parent = boundary_state / "generations";
    gp::persist(boundary_parent, gp::max_id - 1, athena::generation::serialize(athena::generation::make_generation(gp::max_id - 1, {})));
    require(boundary.get(gp::max_id - 1).id == gp::max_id - 1, "penultimate persisted ID not decodable");
    require(boundary.create_unprotected_for_test({}).id == gp::max_id && boundary.get(gp::max_id).id == gp::max_id,
            "maximum allocation/persistence failed");
    const auto boundary_before = snapshot(boundary_parent);
    try { boundary.create_unprotected_for_test({}); require(false, "maximum allocation did not exhaust"); } catch (const std::overflow_error&) {}
    unchanged(boundary_parent, boundary_before);
    require(boundary.list().size() == 3 && boundary.current().id == 0, "maximum ID list/selection changed");
    const auto invalid_parent = fixture.root / "invalid-id"; fs::create_directory(invalid_parent);
    try { gp::persist(invalid_parent, gp::max_id + 1, "not decodable"); require(false, "invalid ID published"); }
    catch (const std::overflow_error&) {}
    require(fs::is_empty(invalid_parent), "out-of-domain persistence mutated namespace");
    const auto state = fixture.root / "state";
    athena::generation::GenerationManager manager(state); manager.initialize();
    for (auto name : {"01", "1suffix", "+1"}) fs::create_directory(state / "generations" / name);
    fs::create_directory_symlink(fixture.root, state / "generations/99");
    require(manager.create_unprotected_for_test({}).id == 1 && manager.list().size() == 2, "malformed generation names affected allocation/list");
    fs::create_directory(state / "generations/9223372036854775808");
    try { manager.create_unprotected_for_test({}); require(false, "allocation overflow accepted"); } catch (const std::overflow_error&) {}
    try { manager.list(); require(false, "out-of-domain root silently ignored"); } catch (const std::overflow_error&) {}
    for (int p : {1, 2, 3}) {
        crash_case(fixture.root / ("crash-" + std::to_string(p)), p);
        crash_case(fixture.root / ("tree-crash-" + std::to_string(p)), p, true, true);
    }
    // Two processes target precisely the same intended ID; exactly one wins.
    const auto race = fixture.root / "race"; fs::create_directory(race);
    int pipefd[2]; require(::pipe(pipefd) == 0, "race pipe"); pid_t children[2];
    for (int i = 0; i < 2; ++i) {
        children[i] = ::fork(); require(children[i] >= 0, "race fork");
        if (children[i] == 0) {
            ::close(pipefd[1]); char t; if (::read(pipefd[0], &t, 1) != 1) ::_exit(3);
            try { gp::persist(race, 9, i ? "second" : "first"); ::_exit(0); }
            catch (...) { ::_exit(1); }
        }
    }
    ::close(pipefd[0]); require(::write(pipefd[1], "go", 2) == 2, "race release"); ::close(pipefd[1]);
    int winners = 0;
    for (auto child : children) { int s; require(::waitpid(child, &s, 0) == child && WIFEXITED(s), "race result"); require(WEXITSTATUS(s) <= 1, "race child"); winners += WEXITSTATUS(s) == 0; }
    require(winners == 1, "generation winner count");
    const auto allocated_state = fixture.root / "allocated";
    athena::generation::GenerationManager allocated(allocated_state); allocated.initialize();
    require(::pipe(pipefd) == 0, "allocation race pipe");
    for (int i = 0; i < 2; ++i) {
        children[i] = ::fork(); require(children[i] >= 0, "allocation race fork");
        if (children[i] == 0) {
            ::close(pipefd[1]); char t; if (::read(pipefd[0], &t, 1) != 1) ::_exit(3);
            try { allocated.create_unprotected_for_test({}); ::_exit(0); } catch (...) { ::_exit(1); }
        }
    }
    ::close(pipefd[0]); require(::write(pipefd[1], "go", 2) == 2, "allocation race release"); ::close(pipefd[1]);
    for (auto child : children) { int s; require(::waitpid(child, &s, 0) == child && WIFEXITED(s) && WEXITSTATUS(s) == 0, "allocation race failed"); }
    const auto generations = allocated.list();
    require(generations.size() == 3 && generations[1].id == 1 && generations[2].id == 2, "allocation raced");
    const auto unsupported = fixture.root / "unsupported"; fs::create_directory(unsupported);
    auto rejected_ops = ops(); rejected_ops.fstatfs = unsupported_fs;
    bool rejected = false;
    try { gp::persist(unsupported, 0, "metadata", rejected_ops); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected && fs::is_empty(unsupported), "unsupported filesystem mutated generation namespace");
    // All six namespace barriers in the actual ObjectStore setup use the table.
    for (int at = 1; at <= 6; ++at) {
        const auto namespace_root = fixture.root / ("namespace-fault-" + std::to_string(at));
        fs::create_directory(namespace_root); reset(); sync_failure_at = at;
        try { mechanics::publication_namespace(namespace_root, ops()); require(false, "namespace barrier hook bypassed"); }
        catch (const d::PersistenceError& e) {
            require(e.stage == d::PersistenceError::Stage::DirectoryBarrier && !e.outcome_uncertain &&
                    e.error == EIO && sync_calls == at, "namespace failure misclassified");
        }
        const auto ns = namespace_root / "objects/v1/sha256";
        if (fs::exists(ns)) require(fs::is_empty(ns), "namespace failure prepared or published payload");
    }
    reset();
    const auto namespace_root = fixture.root / "namespace-success"; fs::create_directory(namespace_root);
    auto namespace_fd = mechanics::publication_namespace(namespace_root, ops());
    require(sync_calls == 6, "successful namespace barrier omitted");
    const auto unsupported_store = fixture.root / "unsupported-store"; fs::create_directory(unsupported_store);
    rejects_state([&] { mechanics::publication_namespace(unsupported_store, rejected_ops); });
    require(fs::is_empty(unsupported_store), "unsupported object namespace mutated");
    auto model = payload_model();
    for (int f : {1, 16}) {
        const auto failed_tree = fixture.root / ("write-fault-" + std::to_string(f)); fs::create_directory(failed_tree);
        persistence_failure(f, false, [&] { mechanics::materialize(failed_tree, model, ops()); });
    }
    const auto partial_tree = fixture.root / "partial-write"; fs::create_directory(partial_tree);
    reset(15); mechanics::materialize(partial_tree, model, ops());
    require(write_calls > 1, "partial writes/EINTR hook bypassed"); reset(); exact_payload(partial_tree, model, false);
    // Exercise the production durability walk without bypassing scanner policy.
    const auto tree = fixture.root / "tree"; fs::create_directory(tree);
    mechanics::materialize(tree, model); mechanics::finalize(tree, model);
    persistence_failure(2, false, [&] { mechanics::synchronize(tree, model, ops()); });
    persistence_failure(3, false, [&] { mechanics::synchronize(tree, model, ops()); });
    observed_root = tree; observed_model = model; synchronized.clear();
    mechanics::synchronize(tree, model, ops());
    require(synchronized.contains("") && synchronized.size() == 8, "durability walk omitted or followed nodes");
    observed_root.clear(); exact_payload(tree, model, true);
    auto parent = d::open_directory(fixture.root);
    d::directory_barrier(parent.get());
    persistence_failure(4, false, [&] { d::publish_tree(parent.get(), "tree", parent.get(), "published", ops()); });
    persistence_failure(5, true, [&] { d::publish_tree(parent.get(), "tree", parent.get(), "published", ops()); });
    exact_payload(fixture.root / "published", model, true);
    const auto loser = fixture.root / "loser"; fs::create_directory(loser);
    mechanics::materialize(loser, model); mechanics::finalize(loser, model); mechanics::synchronize(loser, model);
    const auto winner_before = snapshot(fixture.root / "published");
    persistence_failure(18, true, [&] { d::publish_tree(parent.get(), "loser", parent.get(), "published", ops()); });
    require(d::publish_tree(parent.get(), "loser", parent.get(), "published", ops()) == d::Publication::AlreadyExists,
            "valid concurrent winner did not converge");
    require(d::cleanup_private_tree(parent.get(), "loser").empty(), "losing private cleanup failed");
    unchanged(fixture.root / "published", winner_before); exact_payload(fixture.root / "published", model, true);
    auto other_mount = d::open_directory("/");
    bool supported = true;
    try { d::require_durable_directory(other_mount.get()); } catch (const std::runtime_error&) { supported = false; }
    if (supported && d::stat_fd(parent.get()).st_dev != d::stat_fd(other_mount.get()).st_dev) {
        try { d::publish_tree(parent.get(), "published", other_mount.get(), ".athena-crossfs-test"); require(false, "cross-filesystem publication accepted"); }
        catch (const d::PersistenceError& e) { require(e.error == EXDEV && !e.outcome_uncertain, "cross-filesystem failure model"); }
        require(fs::exists(fixture.root / "published"), "cross-filesystem failure changed source");
    }
    const auto outside = fixture.root / "outside"; fs::create_directory(outside);
    std::ofstream(outside / "sentinel") << "untouched";
    const auto prepared = fixture.root / "published";
    ::chmod(prepared.c_str(), 0755); fs::remove(prepared / "empty");
    fs::create_directory_symlink(outside, prepared / "empty");
    bool unsafe = false;
    try { mechanics::synchronize(prepared, model); } catch (const std::exception&) { unsafe = true; }
    require(unsafe && read_bytes(outside / "sentinel") == "untouched", "durability traversal escaped via symlink");
    return 0;
    }
    // Full ArtifactId publication requires the scanner's complete metadata inspection.
    const auto source = fixture.root / "source"; fs::create_directory(source);
    auto model = payload_model(); mechanics::materialize(source, model);
    auto id = derive_artifact_id(model); ObjectStore store(fixture.root);
    try { store.publish_unprotected_for_test(source, id); }
    catch (const ObjectError& e) {
        if (e.code() != ObjectErrorCode::InvalidSource || std::string(e.what()).find("hidden trusted.*") == std::string::npos) throw;
        require(!fs::exists(store.object_path(id)), "failed scanner published");
        std::cout << "SKIP complete ArtifactId publication coverage: " << e.what() << '\n';
        return 77;
    }
    require(athena::store::GarbageCollector(store.root(), fixture.root / "gc-state").collect().size() == 1, "remove probe object");
    for (int p : {1, 2, 3}) crash_case(fixture.root / ("object-crash-" + std::to_string(p)), p, true);
    for (int f : {1, 6, 11, 2, 3, 4, 8}) {
        persistence_failure(f, false, [&] { store.publish_unprotected_for_test(source, id, ops()); });
        require(!fs::exists(store.object_path(id)), "object pre-publication failure became visible");
    }
    for (int f : {12, 14, 13, 5}) {
        persistence_failure(f, true, [&] { store.publish_unprotected_for_test(source, id, ops()); });
        require(fs::exists(store.object_path(id)), "object post-rename failure lost winner");
        store.verify(id); exact_payload(store.object_path(id), model, true);
        require(athena::store::GarbageCollector(store.root(), fixture.root / "gc-state").collect().size() == 1, "remove uncertain object");
    }
    require(store.publish_durable_unprotected_for_test(source, id).result == d::Publication::Published, "new object result");
    require(store.publish_durable_unprotected_for_test(source, id).result == d::Publication::AlreadyExists, "valid winner result");
    store.verify(id); // Valid EEXIST convergence.
    const auto existing_before = snapshot(store.object_path(id));
    for (int f : {17, 18}) {
        persistence_failure(f, true, [&] { store.publish_durable_unprotected_for_test(source, id, ops()); });
        unchanged(store.object_path(id), existing_before);
    }
    require(athena::store::GarbageCollector(store.root(), fixture.root / "gc-state").collect().size() == 1, "remove fixture object");
    persistence_failure(10, true, [&] { store.publish_unprotected_for_test(source, id, ops()); });
    require(fs::exists(store.object_path(id)), "post-publication error claimed rollback");
    require(athena::store::GarbageCollector(store.root(), fixture.root / "gc-state").collect().size() == 1, "remove post-publication corruption");
    reset(7);
    rejects(ObjectErrorCode::Materialization, [&] { store.publish_unprotected_for_test(source, id, ops()); });
    reset(); require(!fs::exists(store.object_path(id)), "prepared mismatch published");
    reset(); concurrent(store.object_path(id), model, [&](int) { store.publish_unprotected_for_test(source, id, ops()); });
    require(athena::store::GarbageCollector(store.root(), fixture.root / "gc-state").collect().size() == 1, "remove race object");
    fs::create_directory(store.object_path(id));
    rejects(ObjectErrorCode::CollisionCorruption, [&] { store.publish_unprotected_for_test(source, id); });
    require(fs::is_empty(store.object_path(id)), "corrupted winner changed");
    reset(19);
    try { store.publish_unprotected_for_test(source, id, ops()); require(false, "corrupt winner accepted with cleanup fault"); }
    catch (const ObjectError& e) {
        require(e.code() == ObjectErrorCode::CollisionCorruption && !e.cleanup_failure().empty(),
                "cleanup replaced primary winner integrity failure");
    }
    reset(); require(fs::is_empty(store.object_path(id)), "cleanup deleted/changed corrupt winner");
    auto wrong = athena::core::ArtifactId("sha256:" + std::string(64, 'f'));
    rejects(ObjectErrorCode::SourceIdentityMismatch, [&] { store.publish_unprotected_for_test(source, wrong); });
    require(!fs::exists(store.object_path(wrong)), "mismatch published");
}
