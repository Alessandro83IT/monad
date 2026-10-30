// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation/projection_fs.hpp"
#include "activation_schema_support.hpp"
#include "filesystem_fixture_support.hpp"
#include "test_tmp_root.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <signal.h>
#include <linux/fs.h>
#include <unistd.h>
using namespace athena::activation;
namespace fs = std::filesystem;
using d1_test::require;
namespace {
const ActivationId id("0123456789abcdef0123456789abcdef");
const ActivationId next_id("11111111111111111111111111111111");
WitnessReference ref(unsigned n = 0) { return {id, n}; }
struct Fixture {
    fs::path base;
    ProjectionBinding binding;
    athena::store::ObjectStore store;
    std::unique_ptr<ProjectionState> state;
    static fs::path temporary() {
        return athena::test::make_temporary_directory("athena-d2");
    }
    Fixture() : base(temporary()), binding{d1_test::projection_id,
        (base / "generations").native(), (base / "store").native(), (base / "projection").native()}, store(base / "store") {
        for (auto name : {"generations", "store", "projection"}) fixture_fs::create_directories(base / name);
        state = std::make_unique<ProjectionState>(binding.generation_authority);
    }
    ~Fixture() { state.reset(); fs::remove_all(base); } // Fixture cleanup, never the primitive under test.
    auto create(ProjectionIo io = {}) { return ProjectionContext::create_binding(binding, store, *state, std::move(io)); }
    auto open(ProjectionIo io = {}) { return ProjectionContext::open(binding, store, std::move(io)); }
    fs::path projection() const { return base / "projection"; }
    fs::path admin() const { return projection() / ".athena-activation"; }
    fs::path witness(unsigned n = 0) const { return admin() / "witnesses" / id.value() / std::to_string(n); }
    fs::path work(unsigned n = 0) const { return admin() / "work" / id.value() / std::to_string(n); }
};
template<class F> ProjectionError error(F action, ProjectionCause expected) {
    try { action(); }
    catch (const ProjectionError& e) { require(e.cause == expected, std::string("Wrong cause: ") + e.what()); return e; }
    throw std::runtime_error("Expected projection error");
}
template<class F> void rejected(F action) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Unsafe input accepted");
}
std::string fdpath(int fd) { return fs::read_symlink("/proc/self/fd/" + std::to_string(fd)).native(); }
struct statx sx(const fs::path& path) {
    struct statx s{};
    require(::syscall(SYS_statx, AT_FDCWD, path.c_str(), AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_MNT_ID, &s) == 0, "statx");
    return s;
}
void same(const fs::path& a, const fs::path& b) {
    const auto x = sx(a), y = sx(b);
    require(S_ISLNK(x.stx_mode) && S_ISLNK(y.stx_mode), "Hard links must name symlink inodes");
    require(x.stx_ino == y.stx_ino && x.stx_dev_major == y.stx_dev_major && x.stx_dev_minor == y.stx_dev_minor &&
        x.stx_mnt_id == y.stx_mnt_id, "Hard-linked symlink identity");
    require(fs::read_symlink(a).native() == fs::read_symlink(b).native(), "Literal target equality");
    require(x.stx_nlink >= 2, "Symlink witness link count must grow");
}
void write(const fs::path& path, std::string_view text) { std::ofstream out(path, std::ios::binary); out << text; require(bool(out), "write fixture"); }
void codecs() {
    ProjectionBinding binding{d1_test::projection_id, "/generation", "/store", std::string("/root/\xff", 7)};
    auto bytes = serialize(binding);
    require(parse_projection_binding(bytes) == binding, "Binding raw-byte roundtrip");
    for (auto bad : {d1_test::replace(bytes, "schema_version = 1", "schema_version = 1.0"),
        d1_test::replace(bytes, "schema_version = 1", "schema_version = 2"),
        bytes + "extra = 1\n", d1_test::replace(bytes, "projection_id = \"f", "projection_id = \"F"),
        d1_test::replace(bytes, "2f67656e65726174696f6e", "2F67656e65726174696f6e"), bytes + "# comment\n"})
        rejected([&] { parse_projection_binding(bad); });
    for (auto locator : {"relative", "/a/../b", "/a//b", "/a/", "/a/./b"}) {
        auto bad = binding; bad.projection_root = locator; rejected([&] { serialize(bad); });
    }
    for (auto path : {".athena-activation", ".athena-activation/foo", ".athena-activation/witnesses/x"})
        error([&] { validate_projection_path(path); }, ProjectionCause::ProjectionConflict);
    for (auto path : {".athena-activation-old", "foo/.athena-activation"}) validate_projection_path(path);
    for (auto path : {"/a", "a//b", "a/", ".", "..", "a/../b", "a/./b"}) rejected([&] { validate_projection_path(path); });
}
void mechanics() {
    Fixture f; auto context = f.create();
    context.revalidate();
    std::vector<std::string> targets{"/dangling/destination", "../relative/target", "/absolute/../literal", std::string("../\xff\x80", 5), std::string(2000, 'x')};
    unsigned op = 0;
    for (const auto& target : targets) {
        const auto path = "link" + std::to_string(op);
        require(context.create_symlink(*f.state, path, target, ref(op)) == ProjectionOutcome::MutatedDurably, "CREATE result");
        same(f.witness(op), f.projection() / path);
        require(fs::symlink_status(f.work(op)).type() == fs::file_type::not_found, "Work name consumed by rename");
        context.validate_owned_symlink(path, target, ref(op));
        ++op;
    }
    fs::create_directory(f.projection() / "foo");
    context.create_symlink(*f.state, "foo/.athena-activation", "literal", ref(op++));
    context.create_symlink(*f.state, ".athena-activation-old", "literal", ref(op++));
    context.create_symlink(*f.state, std::string("high-\xff", 6), "literal", ref(op++));
    const WitnessReference next{next_id, 2};
    context.succeed_witness(*f.state, "link0", targets[0], ref(), next);
    same(f.witness(), f.admin() / "witnesses" / next_id.value() / "2");
    context.validate_owned_symlink("link0", targets[0], next);
    require(context.succeed_witness(*f.state, "link0", targets[0], ref(), next) == ProjectionOutcome::NotMutated,
        "Exact successor retry is idempotent");
    require(fs::is_symlink(f.witness()), "Predecessor retained");
    auto reopened = f.open(); reopened.validate_owned_symlink("link0", targets[0], ref());
    write(f.base / "real-target", "contents"); const auto real_target = f.base / "real-target";
    const auto before = sx(real_target);
    context.create_symlink(*f.state, "real", real_target.native(), ref(op++));
    require(sx(real_target).stx_ino == before.stx_ino && sx(real_target).stx_nlink == before.stx_nlink,
        "Hard-link witness does not dereference an existing target");
    const WitnessReference adoption{d1_test::adoption_id, 0};
    error([&] { context.validate_owned_symlink("link0", targets[0], adoption); }, ProjectionCause::UnsupportedWitnessKind);
    error([&] { context.create_symlink(*f.state, "adopt", "literal", adoption); }, ProjectionCause::UnsupportedWitnessKind);
    rejected([&] { context.create_symlink(*f.state, "nul", std::string("a\0b", 3), ref(100)); });
}
void security() {
    { Fixture f; fs::rename(f.projection(), f.base / "real"); fs::create_directory_symlink(f.base / "real", f.projection()); rejected([&] { f.create(); }); }
    { Fixture f; fs::create_directory(f.admin()); error([&] { f.create(); }, ProjectionCause::BindingConflict); require(fs::is_directory(f.admin()), "Foreign admin retained"); }
    { Fixture f; fs::create_directory_symlink(f.base / "store", f.admin()); error([&] { f.create(); }, ProjectionCause::BindingConflict); require(fs::is_symlink(f.admin()), "Foreign admin symlink retained"); }
    for (auto name : {"", "/witnesses", "/work"}) {
        Fixture f; auto c = f.create(); const auto path = fs::path(f.admin().native() + name);
        fs::rename(path, f.base / "saved"); fs::create_directory_symlink(f.base / "saved", path);
        rejected([&] { f.open(); }); rejected([&] { c.revalidate(); });
        require(fs::is_symlink(path), "Conflicting symlink retained");
    }
    for (auto name : {"", "/witnesses", "/work"}) {
        Fixture f; auto c = f.create(); const auto path = fs::path(f.admin().native() + name);
        require(::chmod(path.c_str(), 0755) == 0, "chmod fixture");
        error([&] { c.revalidate(); }, ProjectionCause::ProjectionConflict);
        require((sx(path).stx_mode & 07777) == 0755, "No chmod adoption");
    }
    { Fixture f; auto c = f.create(); fs::create_directory(f.admin() / "unexpected");
      error([&] { c.revalidate(); }, ProjectionCause::ProjectionConflict); require(fs::is_directory(f.admin() / "unexpected"), "Unexpected state retained"); }
    { Fixture f; auto c = f.create(); fs::create_directory(f.admin() / "adoptions"); rejected([&] { f.open(); }); }
    { Fixture f; auto c = f.create(); fs::create_directory_symlink(f.base / "store", f.projection() / "ancestor");
      error([&] { c.create_symlink(*f.state, "ancestor/tool", "target", ref()); }, ProjectionCause::UnsafeAncestor);
      require(!fs::exists(f.base / "store/tool"), "No ancestor traversal"); }
    { Fixture f; auto c = f.create(); c.create_symlink(*f.state, "link", "target", ref());
      fs::remove(f.projection() / "link"); fs::create_symlink("target", f.projection() / "link");
      error([&] { c.validate_owned_symlink("link", "target", ref()); }, ProjectionCause::OwnershipMismatch);
      require(sx(f.witness()).stx_ino != sx(f.projection() / "link").stx_ino, "Foreign identical inode differs"); }
    { Fixture f; auto c = f.create(); c.create_symlink(*f.state, "link", "target", ref());
      fs::remove(f.witness()); fs::create_symlink("target", f.witness());
      error([&] { c.validate_owned_symlink("link", "target", ref()); }, ProjectionCause::OwnershipMismatch); }
    for (int kind = 0; kind < 4; ++kind) {
        Fixture f; auto c = f.create(); c.create_symlink(*f.state, "link", "target", ref()); fs::remove(f.projection() / "link");
        if (kind == 0) fs::create_symlink("changed", f.projection() / "link");
        if (kind == 1) write(f.projection() / "link", "foreign file");
        if (kind == 2) fs::create_directory(f.projection() / "link");
        error([&] { c.validate_owned_symlink("link", "target", ref()); },
            kind == 0 ? ProjectionCause::OwnershipMismatch : kind == 3 ? ProjectionCause::MissingManaged : ProjectionCause::ForeignEntry);
    }
    { Fixture f; auto c = f.create(); c.create_symlink(*f.state, "link", "target", ref()); fs::remove(f.witness());
      error([&] { c.validate_owned_symlink("link", "target", ref()); }, ProjectionCause::MissingWitness); require(fs::is_symlink(f.projection() / "link"), "Missing witness no mutation"); }
    { Fixture f; auto c = f.create(); c.create_symlink(*f.state, "link", "target", ref());
      const auto path = f.admin() / "witnesses" / id.value(); require(::chmod(path.c_str(), 0755) == 0, "chmod container"); rejected([&] { f.open(); }); }
    // Ownership fault injection works without privilege/user namespace assumptions.
    for (int fault : {0, 1, 2}) {
        Fixture f; auto c = f.create(); ProjectionIo io; auto direct = io.stat;
        io.stat = [direct, fault](int fd, const char* name, int flags, unsigned mask, struct statx* out) {
            auto rc = direct(fd, name, flags, mask, out);
            if (rc == 0 && fdpath(fd).find(".athena-activation") != std::string::npos) {
                if (fault == 0) out->stx_uid = ::geteuid() + 1;
                if (fault == 1) ++out->stx_mnt_id;
                if (fault == 2) out->stx_mask &= ~STATX_MNT_ID;
            }
            return rc;
        };
        rejected([&] { f.open(io); });
    }
    { Fixture f; auto c = f.create(); ProjectionIo io;
      io.open = [](int, const char*, const open_how&) { errno = ENOSYS; return -1; }; rejected([&] { f.open(io); }); }
    { Fixture f; auto c = f.create(); ProjectionIo io; auto direct = io.open;
      io.open = [direct](int fd, const char* name, const open_how& how) {
          require((how.resolve & (RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV)) ==
              (RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV), "Required openat2 resolve flags");
          require((how.flags & (O_NOFOLLOW | O_CLOEXEC)) == (O_NOFOLLOW | O_CLOEXEC), "Required open flags");
          return direct(fd, name, how);
      }; auto checked = f.open(io); checked.create_symlink(*f.state, "link", "literal", ref()); }
    // Same-device nested mount crossing is reproducible through statx seam.
    { Fixture f; auto c = f.create(); fs::create_directory(f.projection() / "nested"); ProjectionIo io; auto direct = io.stat;
      io.stat = [direct](int fd, const char* name, int flags, unsigned mask, struct statx* out) {
          const int rc = direct(fd, name, flags, mask, out);
          if (!rc && fdpath(fd).ends_with("/nested")) ++out->stx_mnt_id;
          return rc;
      }; auto injected = f.open(io);
      error([&] { injected.create_symlink(*f.state, "nested/link", "target", ref()); }, ProjectionCause::MountCrossing); }
}
void bindings() {
    { Fixture f; error([&] { f.open(); }, ProjectionCause::BindingConflict); require(!fs::exists(f.admin()), "Validation cannot initialize"); }
    for (int side : {0, 1}) {
        Fixture f; auto c = f.create(); auto path = side ? f.admin() / "authority.toml" : f.base / "generations/.athena-projection-authority.toml";
        fs::remove(path); error([&] { f.open(); }, ProjectionCause::BindingConflict);
        error([&] { f.create(); }, ProjectionCause::BindingConflict);
    }
    { Fixture f; auto c = f.create(); auto bad = f.binding; bad.projection_id = ProjectionId(std::string(32, 'a'));
      write(f.admin() / "authority.toml", serialize(bad)); error([&] { f.open(); }, ProjectionCause::BindingConflict); }
    { Fixture f; auto c = f.create(); auto bad = f.binding; bad.store_authority = f.base.native();
      rejected([&] { ProjectionContext::open(bad, f.store); }); }
    { Fixture f; auto c = f.create(); fs::rename(f.projection(), f.base / "saved"); fixture_fs::create_directories(f.projection());
      error([&] { c.revalidate(); }, ProjectionCause::BindingConflict); }
    { Fixture f; fs::rename(f.base / "generations", f.base / "saved"); fixture_fs::create_directories(f.base / "generations");
      error([&] { f.create(); }, ProjectionCause::BindingConflict);
      require(!fs::exists(f.admin()), "Stale State token cannot initialize a substituted authority"); }
    for (auto authority : {"generations", "store"}) {
        Fixture f; auto c = f.create(); fs::rename(f.base / authority, f.base / "saved"); fixture_fs::create_directories(f.base / authority);
        error([&] { c.revalidate(); }, ProjectionCause::BindingConflict);
    }
    { Fixture f; auto c = f.create(); fs::rename(f.admin(), f.base / "saved"); fixture_fs::create_directories(f.admin());
      error([&] { c.revalidate(); }, ProjectionCause::ProjectionConflict); }
    { Fixture f; auto c = f.create(); auto path = f.admin() / "authority.toml";
      auto bytes = serialize(f.binding); write(path, d1_test::replace(bytes, "schema_version = 1", "schema_version = 2"));
      error([&] { f.open(); }, ProjectionCause::BindingConflict); }
}
void faults() {
    // Exhaustively fail each fsync in binding creation. Exact filesystem facts
    // distinguish both missing records, one side, and both visible sides.
    int binding_barriers = 0;
    std::vector<std::pair<bool, bool>> binding_facts;
    { Fixture f; ProjectionIo io; io.fsync = [&](int fd) {
        ++binding_barriers;
        binding_facts.emplace_back(fs::exists(f.admin() / "authority.toml"),
            fs::exists(f.base / "generations/.athena-projection-authority.toml"));
        return ::fsync(fd);
      }; auto c = f.create(io); }
    require(binding_barriers > 4, "Binding barriers exercised");
    for (int fail_at = 1; fail_at <= binding_barriers; ++fail_at) {
        Fixture f; ProjectionIo io; int calls = 0;
        io.fsync = [&](int fd) { if (++calls == fail_at) { errno = EIO; return -1; } return ::fsync(fd); };
        const auto e = error([&] { f.create(io); }, ProjectionCause::DurabilityFailure);
        require(e.outcome == ProjectionOutcome::OutcomeUncertain, "Binding partial state uncertain");
        require(fs::is_directory(f.admin()), "Partial reserved state retained");
        require(fs::exists(f.admin() / "authority.toml") == binding_facts[fail_at - 1].first &&
            fs::exists(f.base / "generations/.athena-projection-authority.toml") == binding_facts[fail_at - 1].second,
            "Exact published binding facts at failed barrier");
        error([&] { f.create(); }, ProjectionCause::BindingConflict);
        if (fs::exists(f.admin() / "authority.toml") && fs::exists(f.base / "generations/.athena-projection-authority.toml")) {
            auto reopened = f.open(); reopened.revalidate(); // Visible exact complete binding validates; no crash-durability claim.
        } else rejected([&] { f.open(); });
    }
    // Existing exact private preparation can resume; a same-target different
    // work inode remains a conflict rather than an adoption.
    { Fixture f; auto c = f.create(); ProjectionIo io;
      io.rename = [](int, const char*, int, const char*, unsigned) { errno = EIO; return -1; };
      auto injected = f.open(io); rejected([&] { injected.create_symlink(*f.state, "link", "target", ref()); });
      fs::remove(f.work()); fs::create_symlink("target", f.work());
      auto before = sx(f.work());
      error([&] { c.create_symlink(*f.state, "link", "target", ref()); }, ProjectionCause::OwnershipMismatch);
      require(sx(f.work()).stx_ino == before.stx_ino && !fs::is_symlink(f.projection() / "link"), "Conflicting work retained"); }
    // A writer racing the NOREPLACE boundary cannot be replaced by CREATE.
    { Fixture f; auto c = f.create(); ProjectionIo io; auto direct = io.rename;
      io.rename = [&](int a, const char* x, int b, const char* y, unsigned flags) {
          fs::create_symlink("raced", f.projection() / "link");
          return direct(a, x, b, y, flags);
      }; auto injected = f.open(io);
      auto e = error([&] { injected.create_symlink(*f.state, "link", "target", ref()); }, ProjectionCause::ProjectionConflict);
      require(e.error == EEXIST && fs::read_symlink(f.projection() / "link") == "raced", "NOREPLACE race preserves winner"); }
    // Existing destination is never replaced; witness/work preparation remains inspectable.
    for (int kind : {0, 1, 2}) {
        Fixture f; auto c = f.create();
        if (kind == 0) fs::create_symlink("target", f.projection() / "link");
        if (kind == 1) write(f.projection() / "link", "foreign");
        if (kind == 2) fs::create_directory(f.projection() / "link");
        const auto before = sx(f.projection() / "link");
        auto e = error([&] { c.create_symlink(*f.state, "link", "target", ref()); }, ProjectionCause::ProjectionConflict);
        require(e.error == EEXIST && e.outcome == ProjectionOutcome::MutatedDurably, "CREATE conflict with durable private preparation");
        require(sx(f.projection() / "link").stx_ino == before.stx_ino, "Destination retained"); same(f.witness(), f.work());
    }
    // Skip the four container recovery barriers to target symlink mechanics.
    for (int fail_at = 1; fail_at <= 4; ++fail_at) {
        Fixture f; auto c = f.create(); c.create_symlink(*f.state, "seed", "target", ref(99));
        ProjectionIo io; int count = 0;
        io.fsync = [&](int fd) { if (++count == fail_at + 4) { errno = EIO; return -1; } return ::fsync(fd); };
        auto injected = f.open(io);
        auto e = error([&] { injected.create_symlink(*f.state, "link", "target", ref()); }, ProjectionCause::DurabilityFailure);
        require(e.outcome == ProjectionOutcome::OutcomeUncertain, "CREATE failed barrier uncertain");
        require(fs::is_symlink(f.witness()), "Witness preserved");
        require(fs::is_symlink(f.work()) == (fail_at == 2), "Work facts after barrier failure");
        require(fs::is_symlink(f.projection() / "link") == (fail_at >= 3), "Managed facts after barrier failure");
        auto fresh = f.open();
        if (fail_at >= 3) fresh.validate_owned_symlink("link", "target", ref());
        else { fresh.create_symlink(*f.state, "link", "target", ref()); fresh.validate_owned_symlink("link", "target", ref()); }
    }
    for (int stage : {0, 1, 2}) {
        Fixture f; auto c = f.create(); c.create_symlink(*f.state, "seed", "target", ref(99)); ProjectionIo io;
        if (stage == 0) io.symlink = [](const char*, int, const char*) { errno = ENOSPC; return -1; };
        if (stage == 1) io.link = [](int, const char*, int, const char*, int flags) { require(flags == 0, "No symlink dereference"); errno = EIO; return -1; };
        if (stage == 2) io.rename = [](int, const char*, int, const char*, unsigned flags) { require(flags == RENAME_NOREPLACE, "True NOREPLACE"); errno = EIO; return -1; };
        auto injected = f.open(io);
        auto e = error([&] { injected.create_symlink(*f.state, "link", "target", ref()); }, stage == 1 ? ProjectionCause::IoFailure : ProjectionCause::ProjectionConflict);
        require(e.outcome == (stage == 0 ? ProjectionOutcome::NotMutated : ProjectionOutcome::MutatedDurably), "Namespace mutation distinct from cause");
        require(!fs::is_symlink(f.projection() / "link"), "Failed publication leaves destination missing");
        require(fs::is_symlink(f.witness()) == (stage > 0), "Witness facts");
        require(fs::is_symlink(f.work()) == (stage == 2), "Work facts");
        auto fresh = f.open(); fresh.create_symlink(*f.state, "link", "target", ref());
        fresh.validate_owned_symlink("link", "target", ref());
    }
    for (int stage : {0, 1}) {
        Fixture f; auto c = f.create(); c.create_symlink(*f.state, "link", "target", ref()); ProjectionIo io;
        if (stage == 0) io.link = [](int, const char*, int, const char*, int) { errno = EIO; return -1; };
        int barriers = 0;
        if (stage == 1) io.fsync = [&](int fd) { if (++barriers == 3) { errno = EIO; return -1; } return ::fsync(fd); };
        auto injected = f.open(io); auto e = error([&] { injected.succeed_witness(*f.state, "link", "target", ref(), ref(1)); },
            stage == 0 ? ProjectionCause::ProjectionConflict : ProjectionCause::DurabilityFailure);
        require(e.outcome == (stage == 0 ? ProjectionOutcome::NotMutated : ProjectionOutcome::OutcomeUncertain), "Succession outcomes");
        require(fs::is_symlink(f.witness(1)) == (stage == 1), "Successor facts"); c.validate_owned_symlink("link", "target", ref());
    }
    for (int barrier = 1; barrier <= 4; ++barrier) {
        Fixture f; auto c = f.create(); ProjectionIo io; int count = 0;
        io.fsync = [&](int fd) { if (++count == barrier) { errno = EIO; return -1; } return ::fsync(fd); };
        auto injected = f.open(io);
        auto e = error([&] { injected.create_symlink(*f.state, "link", "target", ref()); }, ProjectionCause::DurabilityFailure);
        require(e.outcome == ProjectionOutcome::OutcomeUncertain, "Private mkdir barrier uncertainty");
        require(fs::is_directory(f.admin() / "witnesses" / id.value()), "Witness container retained");
        require(fs::is_directory(f.admin() / "work" / id.value()) == (barrier >= 3), "Work container facts");
        require(!fs::is_symlink(f.witness()), "No symlink before container barriers");
    }
    { Fixture f; auto c = f.create(); ProjectionIo io; io.mkdir = [](int, const char*, mode_t) { errno = ENOSPC; return -1; };
      auto injected = f.open(io); auto e = error([&] { injected.create_symlink(*f.state, "link", "target", ref()); }, ProjectionCause::ProjectionConflict);
      require(e.outcome == ProjectionOutcome::NotMutated, "mkdir syscall failure before mutation"); }
}
void container_recovery() {
    // Cross every original setup failure with every retry setup failure. Reopen
    // on every attempt: no process-local knowledge can prove publication.
    for (int initial = 1; initial <= 4; ++initial) {
        for (int recovery = 1; recovery <= 4; ++recovery) {
            Fixture f; auto c = f.create(); ProjectionIo first; int calls = 0;
            first.fsync = [&](int fd) {
                if (++calls == initial) { errno = EIO; return -1; }
                return ::fsync(fd);
            };
            auto interrupted = f.open(first);
            auto e = error([&] { interrupted.create_symlink(*f.state, "link", "target", ref()); }, ProjectionCause::DurabilityFailure);
            require(e.outcome == ProjectionOutcome::OutcomeUncertain, "Original container publication uncertain");
            const auto witness_container = f.witness().parent_path();
            const auto witness_inode = sx(witness_container).stx_ino;
            for (int repeat = 0; repeat < 2; ++repeat) {
                const auto work_container = f.work().parent_path();
                const auto work_inode = fs::is_directory(work_container) ? sx(work_container).stx_ino : 0;
                ProjectionIo retry; int barriers = 0;
                retry.fsync = [&](int fd) {
                    if (++barriers == recovery) { errno = EIO; return -1; }
                    return ::fsync(fd);
                };
                auto fresh = f.open(retry);
                e = error([&] { fresh.create_symlink(*f.state, "link", "target", ref()); }, ProjectionCause::DurabilityFailure);
                require(barriers == recovery && e.error == EIO && e.outcome == ProjectionOutcome::OutcomeUncertain,
                    "Repeated recovery barrier failure cannot report durable success or no mutation");
                require(sx(witness_container).stx_ino == witness_inode, "Uncertain container retained without replacement");
                require(!work_inode || sx(work_container).stx_ino == work_inode, "Uncertain work container retained without replacement");
                require(!fs::is_symlink(f.witness()) && !fs::is_symlink(f.work()) && !fs::is_symlink(f.projection() / "link"),
                    "No symlink preparation or publication before all container barriers succeed");
            }
            const std::vector<std::string> setup{
                witness_container.native(), (f.admin() / "witnesses").native(),
                f.work().parent_path().native(), (f.admin() / "work").native()};
            std::vector<std::string> observed; ProjectionIo complete; auto direct = complete.symlink;
            complete.fsync = [&](int fd) { observed.push_back(fdpath(fd)); return ::fsync(fd); };
            complete.symlink = [&](const char* target, int fd, const char* name) {
                require(observed == setup, "Both container and parent barriers complete before witness creation");
                return direct(target, fd, name);
            };
            auto fresh = f.open(complete);
            require(observed.empty(), "Opening a recovery context performs no barriers");
            require(fresh.create_symlink(*f.state, "link", "target", ref()) == ProjectionOutcome::MutatedDurably,
                "CREATE retry succeeds only after recovery and publication barriers");
            auto expected = setup;
            for (const auto& path : {witness_container, f.work().parent_path(), f.projection(), f.work().parent_path()})
                expected.push_back(path.native());
            require(observed == expected, "Exact CREATE recovery durability ordering");
            same(f.witness(), f.projection() / "link");
            require(sx(witness_container).stx_ino == witness_inode, "Successful recovery reuses retained container");
            fresh.validate_owned_symlink("link", "target", ref());
            require(observed == expected, "Ownership inspection performs no barriers");
        }
    }
    for (int initial : {1, 2}) {
        for (int recovery : {1, 2}) {
            Fixture f; auto c = f.create(); c.create_symlink(*f.state, "link", "target", ref());
            const WitnessReference next{next_id, 0};
            const auto next_container = f.admin() / "witnesses" / next_id.value();
            ProjectionIo first; int calls = 0;
            first.fsync = [&](int fd) {
                if (++calls == initial) { errno = EIO; return -1; }
                return ::fsync(fd);
            };
            auto interrupted = f.open(first);
            auto e = error([&] { interrupted.succeed_witness(*f.state, "link", "target", ref(), next); }, ProjectionCause::DurabilityFailure);
            require(e.outcome == ProjectionOutcome::OutcomeUncertain, "Successor container publication uncertain");
            const auto retained_inode = sx(next_container).stx_ino;
            for (int repeat = 0; repeat < 2; ++repeat) {
                ProjectionIo retry; int barriers = 0;
                retry.fsync = [&](int fd) {
                    if (++barriers == recovery) { errno = EIO; return -1; }
                    return ::fsync(fd);
                };
                auto fresh = f.open(retry);
                e = error([&] { fresh.succeed_witness(*f.state, "link", "target", ref(), next); }, ProjectionCause::DurabilityFailure);
                require(barriers == recovery && e.error == EIO && e.outcome == ProjectionOutcome::OutcomeUncertain,
                    "Repeated succession recovery failure remains uncertain");
                require(sx(next_container).stx_ino == retained_inode && !fs::is_symlink(next_container / "0"),
                    "Successor container retained; no witness before recovery barriers");
                same(f.witness(), f.projection() / "link");
            }
            const std::vector<std::string> setup{next_container.native(), (f.admin() / "witnesses").native()};
            std::vector<std::string> observed; ProjectionIo complete; auto direct = complete.link;
            complete.fsync = [&](int fd) { observed.push_back(fdpath(fd)); return ::fsync(fd); };
            complete.link = [&](int from, const char* source, int to, const char* dest, int flags) {
                require(observed == setup && flags == 0, "Successor link follows both recovery barriers without dereferencing");
                return direct(from, source, to, dest, flags);
            };
            auto fresh = f.open(complete);
            require(fresh.succeed_witness(*f.state, "link", "target", ref(), next) == ProjectionOutcome::MutatedDurably,
                "Fresh-context succession recovery succeeds");
            auto expected = setup; expected.push_back(next_container.native());
            require(observed == expected && sx(next_container).stx_ino == retained_inode, "Exact succession recovery ordering");
            same(f.witness(), next_container / "0");
            observed.clear();
            require(fresh.succeed_witness(*f.state, "link", "target", ref(), next) == ProjectionOutcome::NotMutated,
                "Exact successor retry retains namespace outcome");
            require(observed == expected, "Exact successor retry also completes recovery barriers");
        }
    }
}
void concurrency() {
    Fixture f; auto context = f.create(); f.state.reset();
    // Independent State acquisitions serialize the shared activation containers.
    // Children share no live lock open-file description with the parent.
    pid_t children[2]{};
    for (unsigned i = 0; i < 2; ++i) {
        children[i] = ::fork(); require(children[i] >= 0, "concurrency fork");
        if (!children[i]) {
            try {
                ProjectionState state(f.binding.generation_authority); auto c = f.open();
                const auto path = "concurrent" + std::to_string(i);
                c.create_symlink(state, path, "target", ref(i));
                c.validate_owned_symlink(path, "target", ref(i));
            } catch (...) { ::_exit(1); }
            ::_exit(0);
        }
    }
    for (auto child : children) {
        int status; require(::waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "Independent concurrent operations succeeded under State");
    }
    auto fresh = f.open();
    for (unsigned i = 0; i < 2; ++i) fresh.validate_owned_symlink("concurrent" + std::to_string(i), "target", ref(i));
}
void death() {
    for (int stage : {0, 1, 2}) {
        Fixture f; auto c = f.create(); c.create_symlink(*f.state, "seed", "target", ref(99));
        int pipefd[2]; require(::pipe2(pipefd, O_CLOEXEC) == 0, "pipe");
        const pid_t child = ::fork(); require(child >= 0, "fork");
        if (!child) {
            ::close(pipefd[0]); ProjectionIo io; int barriers = 0;
            io.fsync = [&](int fd) {
                // Existing-container recovery precedes the symlink barriers.
                if (++barriers == stage + 5) {
                    char ready = 'x'; if (::write(pipefd[1], &ready, 1) != 1) ::_exit(2);
                    for (;;) ::pause();
                }
                return ::fsync(fd);
            };
            try { auto fresh = f.open(io); fresh.create_symlink(*f.state, "link", "target", ref()); }
            catch (...) { ::_exit(3); }
            ::_exit(4);
        }
        ::close(pipefd[1]); char ready = 0; require(::read(pipefd[0], &ready, 1) == 1, "Mutation barrier reached"); ::close(pipefd[0]);
        require(::kill(child, SIGKILL) == 0, "kill"); int status; require(::waitpid(child, &status, 0) == child && WIFSIGNALED(status), "wait death");
        require(fs::is_symlink(f.witness()), "Death preserves witness");
        require(fs::is_symlink(f.work()) == (stage == 1), "Death work facts");
        require(fs::is_symlink(f.projection() / "link") == (stage == 2), "Death destination facts");
        const auto inspect = ::fork(); require(inspect >= 0, "inspection fork");
        if (!inspect) { ::execl("/proc/self/exe", "activation_projection_fs_test", "inspect", f.base.c_str(), stage == 2 ? "owned" : "partial", nullptr); ::_exit(127); }
        require(::waitpid(inspect, &status, 0) == inspect && WIFEXITED(status) && WEXITSTATUS(status) == 0, "Fresh exec context inspection");
    }
}
}
int main(int argc, char** argv) {
    return d1_test::run([&] {
        if (argc == 4) {
            const fs::path base(argv[2]); ProjectionBinding binding{d1_test::projection_id,
                (base / "generations").native(), (base / "store").native(), (base / "projection").native()};
            athena::store::ObjectStore store(base / "store"); auto c = ProjectionContext::open(binding, store);
            if (std::string_view(argv[3]) == "owned") c.validate_owned_symlink("link", "target", ref());
            return;
        }
        codecs(); bindings(); mechanics(); security(); faults(); container_recovery(); death(); concurrency();
        std::cout << "D2 projection binding/security/witness/fault/process-death mechanics passed\n";
    });
}
