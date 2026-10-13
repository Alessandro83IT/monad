// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation/legacy.hpp"
#include "activation_schema_support.hpp"
#include "generation/generation.hpp"
#include "store/manifest.hpp"
#include "store/metadata.hpp"
#include "artifact/identity.hpp"
#include "store/transaction_pins.hpp"
#include <fstream>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <set>
using namespace athena::activation;
namespace fs = std::filesystem;
using d1_test::require;
namespace {
bool qualification_cleanup_failed = false;
void write(const fs::path& p, std::string_view b, mode_t mode = 0600) {
    std::ofstream f(p, std::ios::binary); f << b; f.close(); require(!f.fail(), "fixture write"); require(::chmod(p.c_str(), mode) == 0, "chmod");
}
struct Fixture {
    fs::path base;
    ProjectionBinding binding;
    athena::store::ObjectStore store;
    std::unique_ptr<ProjectionState> state;
    static fs::path temporary() { char b[] = "/tmp/athena-d3-XXXXXX"; auto p = ::mkdtemp(b); require(p, "mkdtemp"); return p; }
    Fixture(bool payload = true) : base(temporary()), binding{d1_test::projection_id,
        (base / "generations").native(), (base / "store").native(), (base / "projection").native()}, store(base / "store") {
        fs::create_directory(base / "store"); fs::create_directory(base / "projection");
        athena::generation::GenerationManager manager(base); manager.initialize();
        if (payload) {
            fs::create_directories(source() / "bin"); write(source() / "bin/a", "first", 0755); write(source() / "bin/b", "second", 0644);
            athena::store::write_manifest(source(), {{"bin/a"}, {"bin/b"}});
            athena::store::write_metadata(source(), {"demo", "1", "fixture", "checksum", "cmake"});
            manager.create_unprotected_for_test({{"demo", source().native(), {}}});
            // Actual pre-M2.4 format, with exact package/store identity.
            write(base / "generations/1/generation.toml", "id = 1\n\n[[entries]]\npackage = \"demo\"\nstore = \"" + source().native() + "\"\n");
            write(base / "generations/current", "1\n");
            fs::create_directory(base / "projection/bin");
            for (auto leaf : {"a", "b"}) fs::create_symlink(source() / "bin" / leaf, base / "projection/bin" / leaf);
        }
        state = std::make_unique<ProjectionState>(binding.generation_authority);
        auto p = ProjectionContext::create_binding(binding, store, *state);
    }
    ~Fixture() noexcept {
        state.reset();
        try {
            // symlink_status avoids changing directories outside this fixture.
            for (const auto& e : fs::recursive_directory_iterator(base))
                if (fs::is_directory(e.symlink_status()))
                    fs::permissions(e.path(), fs::perms::owner_all, fs::perm_options::add);
            fs::remove_all(base);
        } catch (const std::exception& e) {
            qualification_cleanup_failed = true;
            std::cerr << "FAIL: D3 fixture cleanup " << base << ": " << e.what() << '\n';
        } catch (...) {
            qualification_cleanup_failed = true;
            std::fprintf(stderr, "FAIL: D3 fixture cleanup\n");
        }
    }
    fs::path source() const { return base / "store/demo-1"; }
    fs::path admin() const { return base / "projection/.athena-activation"; }
    fs::path published() const { return admin() / "adoptions" / d1_test::adoption_id.value(); }
    fs::path preparation() const { return admin() / "adoptions/.preparations" / d1_test::adoption_id.value(); }
    auto open(ProjectionIo io = {}) { return ProjectionContext::open(binding, store, std::move(io)); }
};
template<class F> LegacyError rejected(F f) {
    try { f(); } catch (const LegacyError& e) { return e; }
    throw std::runtime_error("Expected typed legacy rejection");
}
template<class F> void invalid(F f) { try { f(); } catch (const std::exception&) { return; } throw std::runtime_error("Unsafe input accepted"); }
ino_t inode(const fs::path& p) { struct stat s{}; require(::lstat(p.c_str(), &s) == 0, "lstat"); return s.st_ino; }
bool skip(const LegacyError& e) {
    if (!e.underlying) return false;
    try { std::rethrow_exception(e.underlying); }
    catch (const athena::artifact::Error& a) { return a.code() == athena::artifact::ErrorCode::InspectionFailure && std::string(a.what()).find("hidden trusted.*") != std::string::npos; }
    catch (...) { return false; }
}
void codecs() {
    AdoptionRecord r{d1_test::adoption_id, {d1_test::projection_id, "/generations", "/store", "/projection"},
        {3, "3\n"}, athena::generation::GenerationDocumentDigest(d1_test::digest('a')),
        {LegacyTreeSource{"/store/demo", LegacySealDigest(d1_test::digest('b'))}},
        {{0, 0, "bin/a", "/store/demo/bin/a", {d1_test::adoption_id, 0}}}, {"bin"}};
    auto bytes = serialize(r); require(bytes.ends_with('\n'), "Final LF"); require(parse_adoption_record(bytes) == r, "Canonical roundtrip");
    for (auto b : {bytes + "# comment\n", bytes + "unknown = 1\n", d1_test::replace(bytes, "schema_version = 1", "schema_version = 1.0"),
        d1_test::replace(bytes, "id = 0", "id = 1"), d1_test::replace(bytes, "path_hex = \"62696e2f61\"", "path_hex = \"2e2e2f61\"")})
        invalid([&] { parse_adoption_record(b); });
    r.entries.push_back(r.entries[0]); invalid([&] { serialize(r); }); r.entries.pop_back();
    auto alias_record = r; alias_record.sources = {LegacyAliasSource{"/store/demo", "../objects/v1/sha256/literal",
        athena::core::ArtifactId(d1_test::digest('c')), LegacySealDigest(d1_test::digest('b'))}};
    require(parse_adoption_record(serialize(alias_record)) == alias_record, "Alias record roundtrip");
    auto artifact_record = r; artifact_record.sources = {ArtifactSource{athena::core::ArtifactId(d1_test::digest('c'))}};
    artifact_record.entries[0].target = "/store/objects/v1/sha256/" + std::string(64, 'c') + "/bin/a";
    require(parse_adoption_record(serialize(artifact_record)) == artifact_record, "Artifact record roundtrip");
    r.entries[0].witness = {d1_test::id, 0}; invalid([&] { serialize(r); });
    invalid([&] { parse_adoption_record(std::string(max_document_size + 1, 'x')); });
}
void unsafe_sources() {
    Fixture f; auto p = f.open();
    rejected([&] { seal_legacy(p, *f.state, 1, f.binding.generation_authority); });
    rejected([&] { seal_legacy(p, *f.state, 1, f.binding.projection_root); });
    for (auto locator : {"../demo", "/store/../demo", "/outside/demo", "/a//b"})
        rejected([&] { seal_legacy(p, *f.state, 1, locator); });
    fs::rename(f.source(), f.base / "saved"); fs::create_directory_symlink(f.base / "saved", f.source());
    rejected([&] { seal_legacy(p, *f.state, 1, f.source().native()); });
    fs::remove(f.source()); fs::rename(f.base / "saved", f.source());
    require(::mkfifo((f.source() / "fifo").c_str(), 0600) == 0, "fifo");
    rejected([&] { seal_legacy(p, *f.state, 1, f.source().native()); }); fs::remove(f.source() / "fifo");
    athena::artifact::ScanLimits limits; limits.entries = 1;
    rejected([&] { seal_legacy(p, *f.state, 1, f.source().native(), limits); });
    limits.entries = 4;
    const auto bounded = rejected([&] { seal_legacy(p, *f.state, 1, f.source().native(), limits); });
    require(bounded.underlying != nullptr, "Bounded scanner preserves underlying cause");
    try { std::rethrow_exception(bounded.underlying); }
    catch (const athena::artifact::Error& e) { require(e.code() == athena::artifact::ErrorCode::LengthOverflow, "Nested enumeration shares global entry budget"); }
    limits = {}; limits.path = 4; rejected([&] { seal_legacy(p, *f.state, 1, f.source().native(), limits); });
    limits = {}; limits.bytes = 1; rejected([&] { seal_legacy(p, *f.state, 1, f.source().native(), limits); });
    ProjectionIo io; const auto stat = io.stat;
    io.stat = [&](int fd, const char* n, int flags, unsigned mask, struct statx* s) { auto rc = stat(fd, n, flags, mask, s);
        if (!rc && std::string_view(n) == "demo-1") ++s->stx_mnt_id; return rc; };
    auto mismatch = f.open(io); rejected([&] { seal_legacy(mismatch, *f.state, 1, f.source().native()); });
    require(::chmod((f.base / "generations/1/generation.toml").c_str(), 0666) == 0, "unsafe generation metadata mode");
    rejected([&] { seal_legacy(p, *f.state, 1, f.source().native()); });
    fs::remove(f.base / "generations/1/generation.toml");
    require(::mkfifo((f.base / "generations/1/generation.toml").c_str(), 0600) == 0, "generation FIFO");
    rejected([&] { seal_legacy(p, *f.state, 1, f.source().native()); });
}
void manifest_errors() {
    athena::artifact::NormalizedArtifact tree;
    tree.entries = {{".athena", athena::artifact::Directory{}},
        {".athena/manifest.toml", athena::artifact::RegularFile{false, "[[files]]\npath = \"bin/a\"\n"}},
        {".athena/metadata.toml", athena::artifact::RegularFile{false, ""}},
        {"bin", athena::artifact::Directory{}}, {"bin/a", athena::artifact::RegularFile{false, "payload"}}};
    athena::artifact::normalize(tree);
    require(LegacyAccess::manifest_projection(tree) == std::vector<std::string>{"bin/a"}, "Valid scanned manifest interpretation");
    for (const auto* bytes : {"[", "unknown = 1\n", "files = false\n", "files = [1]\n",
        "[[files]]\npath = 1\n", "[[files]]\npath = \"bin/a\"\nunknown = 1\n",
        "[[files]]\npath = \"../escape\"\n", "[[files]]\npath = \".athena-activation/link\"\n",
        "[[files]]\npath = \"bin/missing\"\n", "[[files]]\npath = \"bin/a\"\n[[files]]\npath = \"bin/a\"\n"}) {
        auto malformed = tree;
        std::get<athena::artifact::RegularFile>(malformed.entries[1].value).content = bytes;
        const auto e = rejected([&] { LegacyAccess::manifest_projection(malformed); });
        require(e.cause == LegacyCause::Manifest && e.outcome == ProjectionOutcome::NotMutated,
            "Malformed manifest syntax, schema and paths have Manifest cause");
        if (e.underlying) {
            try { std::rethrow_exception(e.underlying); }
            catch (const SchemaError&) {}
            catch (const ProjectionError& p) { require(p.cause == ProjectionCause::ProjectionConflict, "Only manifest reserved-path conflict translated"); }
        }
    }
    // The pure parser checks above do not qualify filesystem scanning. Real
    // source safety failures must retain their independent classification.
    Fixture f;
    fs::remove(f.source() / ".athena/manifest.toml"); write(f.source() / ".athena/manifest.toml", "[");
    fs::rename(f.source(), f.base / "retired-source"); write(f.source(), "unsupported source type");
    auto p = f.open();
    require(rejected([&] { seal_legacy(p, *f.state, 1, f.source().native()); }).cause == LegacyCause::UnsafeSource,
        "Filesystem source safety failure is not masked as Manifest");
}
void scanner_manifest_errors() {
    for (const auto* bytes : {"[", "unknown = 1\n", "[[files]]\npath = 1\n",
        "[[files]]\npath = \"../escape\"\n", "[[files]]\npath = \".athena-activation/link\"\n"}) {
        Fixture f; write(f.source() / ".athena/manifest.toml", bytes); auto p = f.open();
        require(rejected([&] { seal_legacy(p, *f.state, 1, f.source().native()); }).cause == LegacyCause::Manifest,
            "Scanner-backed malformed manifest retains Manifest cause");
    }
    for (const auto* bytes : {"[", "name = 1\n"}) {
        Fixture f; write(f.source() / ".athena/metadata.toml", bytes); auto p = f.open();
        require(rejected([&] { seal_legacy(p, *f.state, 1, f.source().native()); }).cause == LegacyCause::Manifest,
            "Scanner-backed malformed administrative metadata retains Manifest cause");
    }
}
void empty_adoption() {
    Fixture f(false); auto p = f.open();
    require(adopt_legacy_projection(p, *f.state, d1_test::adoption_id, {}) == ProjectionOutcome::MutatedDurably, "Explicit empty adoption");
    require(fs::is_regular_file(f.published() / "adoption.toml"), "Published record");
    require(adopt_legacy_projection(p, *f.state, d1_test::adoption_id, {}) == ProjectionOutcome::NotMutated, "Exact empty retry");
    require(fs::is_regular_file(f.base / "generations/current") && !fs::exists(f.admin() / "selection"), "No selection activation");
    write(f.base / "generations/current", "0"); rejected([&] { adopt_legacy_projection(p, *f.state, d1_test::adoption_id, {}); });
}
void empty_faults() {
    { Fixture f(false); ProjectionIo io; io.mkdir = [](int, const char*, mode_t) { errno = ENOSPC; return -1; };
      auto p = f.open(io); require(rejected([&] { adopt_legacy_projection(p, *f.state, d1_test::adoption_id, {}); }).outcome == ProjectionOutcome::NotMutated,
          "Failed initial mkdir has no namespace mutation"); require(!fs::exists(f.admin() / "adoptions"), "Failed mkdir leaves administration unchanged"); }
    int barriers = 0;
    { Fixture f(false); ProjectionIo io; io.fsync = [&](int fd) { ++barriers; return ::fsync(fd); };
      auto p = f.open(io); adopt_legacy_projection(p, *f.state, d1_test::adoption_id, {}); }
    require(barriers > 8, "All publication barriers exercised");
    for (int at = 1; at <= barriers; ++at) {
        Fixture f(false); ProjectionIo io; int calls = 0;
        io.fsync = [&](int fd) { if (++calls == at) { errno = EIO; return -1; } return ::fsync(fd); };
        auto p = f.open(io); auto e = rejected([&] { adopt_legacy_projection(p, *f.state, d1_test::adoption_id, {}); });
        require(e.outcome == ProjectionOutcome::OutcomeUncertain, "Barrier failure uncertainty");
        require(fs::is_regular_file(f.base / "generations/current"), "Current retained");
        try { auto fresh = f.open(); adopt_legacy_projection(fresh, *f.state, d1_test::adoption_id, {}); }
        catch (const std::exception&) { require(!fs::exists(f.published()), "Incomplete state cannot publish ownership"); }
    }
    { Fixture f(false); ProjectionIo io;
      io.rename = [](int, const char*, int, const char*, unsigned) { errno = EIO; return -1; };
      auto p = f.open(io); auto e = rejected([&] { adopt_legacy_projection(p, *f.state, d1_test::adoption_id, {}); });
      require(e.outcome == ProjectionOutcome::OutcomeUncertain && fs::is_regular_file(f.preparation() / "adoption.toml"), "Rename failure preserves preparation");
      auto fresh = f.open(); adopt_legacy_projection(fresh, *f.state, d1_test::adoption_id, {}); }
    { Fixture f(false); ProjectionIo io; io.durable.write = [](int, const void*, size_t) -> ssize_t { errno = ENOSPC; return -1; };
      auto p = f.open(io); rejected([&] { adopt_legacy_projection(p, *f.state, d1_test::adoption_id, {}); });
      require(fs::exists(f.preparation()), "Partial record retained");
      auto fresh = f.open(); rejected([&] { adopt_legacy_projection(fresh, *f.state, d1_test::adoption_id, {}); }); }
    { Fixture f(false); auto p = f.open(); fs::create_symlink("foreign", f.base / "projection/extra");
      auto before = inode(f.base / "projection/extra"); rejected([&] { adopt_legacy_projection(p, *f.state, d1_test::adoption_id, {}); });
      require(inode(f.base / "projection/extra") == before && !fs::exists(f.admin() / "adoptions"), "Foreign projection is not mutated"); }
    { Fixture f(false); auto p = f.open(); fs::remove(f.base / "generations/current");
      require(rejected([&] { adopt_legacy_projection(p, *f.state, d1_test::adoption_id, {}); }).outcome == ProjectionOutcome::NotMutated, "Missing current does not mutate adoption"); }
    { Fixture f(false); ProjectionIo io; const auto open = io.open;
      io.open = [open](int fd, const char* name, const open_how& how) {
          if (std::string_view(name) == "adoption.toml" && (how.flags & O_CREAT)) { errno = ENOSPC; return -1; }
          return open(fd, name, how);
      }; auto p = f.open(io); require(rejected([&] { adopt_legacy_projection(p, *f.state, d1_test::adoption_id, {}); }).outcome == ProjectionOutcome::OutcomeUncertain, "Record create failure preserves uncertain preparation"); }
}
void published_retry_barriers() {
    for (unsigned failure = 0; failure != 6; ++failure) {
        Fixture f(false);
        const auto root = f.admin() / "adoptions";
        const auto preparations = root / ".preparations";
        const std::vector<fs::path> required{f.published() / "adoption.toml", f.published() / "witnesses",
            f.published(), root, preparations, f.admin()};
        const auto current_inode = inode(f.base / "generations/current");
        bool renamed = false, destination_synced = false, publication_failed = false;
        ProjectionIo initial;
        const auto rename = initial.rename;
        initial.rename = [&](int a, const char* x, int b, const char* y, unsigned flags) {
            const auto rc = rename(a, x, b, y, flags);
            if (!rc) { require(flags == RENAME_NOREPLACE, "Adoption publication never overwrites"); renamed = true; }
            return rc;
        };
        initial.fsync = [&](int fd) {
            const auto path = fs::read_symlink("/proc/self/fd/" + std::to_string(fd));
            if (renamed && path == preparations) {
                require(destination_synced, "Source-parent failure follows destination-parent barrier");
                publication_failed = true; errno = EIO; return -1;
            }
            const auto rc = ::fsync(fd);
            if (!rc && renamed && path == root) destination_synced = true;
            return rc;
        };
        auto first = f.open(initial);
        const auto publication_error = rejected([&] { adopt_legacy_projection(first, *f.state, d1_test::adoption_id, {}); });
        require(publication_failed && publication_error.cause == LegacyCause::Durability &&
            publication_error.outcome == ProjectionOutcome::OutcomeUncertain && publication_error.error == EIO,
            "Post-publication preparation-parent failure stays uncertain");
        require(fs::exists(f.published()) && !fs::exists(f.preparation()), "Actual rename publication survives failed barrier");
        const auto record_inode = inode(required[0]);
        bool recovery_failed = false;
        ProjectionIo recovery;
        recovery.fsync = [&](int fd) {
            if (fs::read_symlink("/proc/self/fd/" + std::to_string(fd)) == required[failure]) {
                recovery_failed = true; errno = EIO; return -1;
            }
            return ::fsync(fd);
        };
        auto failing = f.open(recovery);
        const auto recovery_error = rejected([&] { adopt_legacy_projection(failing, *f.state, d1_test::adoption_id, {}); });
        require(recovery_failed && recovery_error.cause == LegacyCause::Durability &&
            recovery_error.outcome == ProjectionOutcome::OutcomeUncertain && recovery_error.error == EIO,
            "Every required recovery barrier failure prevents success");
        std::vector<fs::path> synchronized;
        ProjectionIo success;
        success.fsync = [&](int fd) {
            synchronized.push_back(fs::read_symlink("/proc/self/fd/" + std::to_string(fd)));
            return ::fsync(fd);
        };
        auto fresh = f.open(success);
        require(adopt_legacy_projection(fresh, *f.state, d1_test::adoption_id, {}) == ProjectionOutcome::NotMutated,
            "Fresh exact retry succeeds only after recovery barriers");
        require(synchronized == required, "Published retry restores ordered record, witness and BOTH rename-parent barriers");
        require(inode(required[0]) == record_inode && inode(f.base / "generations/current") == current_inode &&
            !fs::exists(f.preparation()), "Recovery preserves published record and historical current");
    }
}
void recovery_parent_substitution() {
    Fixture f(false); auto p = f.open(); adopt_legacy_projection(p, *f.state, d1_test::adoption_id, {});
    const auto preparations = f.admin() / "adoptions/.preparations";
    bool changed = false; ProjectionIo io;
    io.fsync = [&](int fd) {
        const auto path = fs::read_symlink("/proc/self/fd/" + std::to_string(fd));
        const auto rc = ::fsync(fd);
        if (!rc && !changed && path == preparations) {
            changed = true; fs::rename(preparations, f.base / "retired-preparations");
            fs::create_directory(preparations); require(::chmod(preparations.c_str(), 0700) == 0, "Replacement preparation-parent mode");
        }
        return rc;
    };
    auto failing = f.open(io);
    require(rejected([&] { adopt_legacy_projection(failing, *f.state, d1_test::adoption_id, {}); }).outcome == ProjectionOutcome::OutcomeUncertain && changed,
        "Recovery rejects preparation-parent substitution after its barrier");
    auto fresh = f.open();
    require(adopt_legacy_projection(fresh, *f.state, d1_test::adoption_id, {}) == ProjectionOutcome::NotMutated,
        "Fresh recovery synchronizes the current preparation-parent instance");
}
void death() {
    for (bool after : {false, true}) {
        Fixture f(false); f.state.reset();
        int pipes[2]; require(::pipe2(pipes, O_CLOEXEC) == 0, "pipe"); const auto child = ::fork(); require(child >= 0, "fork");
        if (!child) {
            ::close(pipes[0]); ProjectionIo io; auto direct = io.rename;
            io.rename = [&](int a, const char* x, int b, const char* y, unsigned flags) {
                int rc = 0; if (after) rc = direct(a, x, b, y, flags);
                char ready = 'x'; if (::write(pipes[1], &ready, 1) != 1) ::_exit(2);
                for (;;) ::pause(); return rc;
            };
            try { ProjectionState state(f.binding.generation_authority); auto p = f.open(io); adopt_legacy_projection(p, state, d1_test::adoption_id, {}); }
            catch (...) { ::_exit(3); } ::_exit(4);
        }
        ::close(pipes[1]); char ready; require(::read(pipes[0], &ready, 1) == 1, "Publication barrier reached"); ::close(pipes[0]);
        require(::kill(child, SIGKILL) == 0, "kill"); int status; require(::waitpid(child, &status, 0) == child && WIFSIGNALED(status), "wait death");
        require(fs::exists(f.published()) == after && fs::exists(f.preparation()) != after, "Actual process-death namespace state");
        const auto retry = ::fork(); require(retry >= 0, "retry fork");
        if (!retry) { ::execl("/proc/self/exe", "legacy_d3_test", "retry", f.base.c_str(), nullptr); ::_exit(127); }
        require(::waitpid(retry, &status, 0) == retry && WIFEXITED(status) && WEXITSTATUS(status) == 0, "Fresh exec exact retry");
    }
}
void scanner() {
    Fixture f; auto p = f.open();
    auto inventory = seal_legacy(p, *f.state, 1, f.source().native());
    require(inventory.source == seal_legacy(p, *f.state, 1, f.source().native()).source, "Deterministic repeated seal");
    verify_legacy_seal(p, *f.state, 1, inventory.source);
    const std::vector<LegacySource> sources{inventory.source};
    auto a = inode(f.base / "projection/bin/a"), b = inode(f.base / "projection/bin/b");
    require(adopt_legacy_projection(p, *f.state, d1_test::adoption_id, sources) == ProjectionOutcome::MutatedDurably, "Complete multi-link adoption");
    require(inode(f.published() / "witnesses/0") == a && inode(f.published() / "witnesses/1") == b, "Actual symlink hardlink identity");
    require(inode(f.base / "projection/bin/a") == a && inode(f.base / "projection/bin/b") == b, "Historical links retained");
    p.validate_owned_symlink("bin/a", (f.source() / "bin/a").native(), {d1_test::adoption_id, 0});
    p.succeed_witness(*f.state, "bin/a", (f.source() / "bin/a").native(), {d1_test::adoption_id, 0}, {d1_test::id, 0});
    require(inode(f.admin() / "witnesses" / d1_test::id.value() / "0") == a, "D2 succession preserves adopted symlink inode");
    require(adopt_legacy_projection(p, *f.state, d1_test::adoption_id, sources) == ProjectionOutcome::NotMutated, "Exact populated retry");
    fs::remove(f.base / "projection/bin/a"); fs::create_symlink(f.source() / "bin/a", f.base / "projection/bin/a");
    rejected([&] { adopt_legacy_projection(p, *f.state, d1_test::adoption_id, sources); });
    invalid([&] { p.validate_owned_symlink("bin/a", (f.source() / "bin/a").native(), {d1_test::adoption_id, 0}); });
    write(f.source() / "bin/a", "modified", 0755); auto e = rejected([&] { verify_legacy_seal(p, *f.state, 1, inventory.source); });
    require(e.cause == LegacyCause::SealMismatch, "Typed stale content seal mismatch");
    write(f.source() / "bin/a", "first", 0755); fs::create_directory(f.source() / "extra");
    require(rejected([&] { verify_legacy_seal(p, *f.state, 1, inventory.source); }).cause == LegacyCause::SealMismatch, "Unexpected entry invalidates seal");
    fs::remove(f.source() / "extra"); fs::remove(f.source() / "bin/b"); rejected([&] { verify_legacy_seal(p, *f.state, 1, inventory.source); });
    write(f.source() / "bin/b", "second", 0644);
    write(f.source() / ".athena/manifest.toml", "[[files]]\npath = \"../escape\"\n"); rejected([&] { seal_legacy(p, *f.state, 1, f.source().native()); });
}
using RuntimeIdentity = std::pair<dev_t, ino_t>;
RuntimeIdentity identity(const fs::path&);
RuntimeIdentity identity(int);
void replace_tree(const fs::path&, const fs::path&, unsigned);
void require_payload_barriers(const fs::path&, const std::set<RuntimeIdentity>&);
void scanner_seals() {
    for (int change = 0; change < 9; ++change) {
        Fixture f; auto p = f.open();
        if (change == 0) { fs::remove(f.source() / "bin/b"); fs::create_symlink("literal", f.source() / "bin/b"); }
        auto seal = seal_legacy(p, *f.state, 1, f.source().native());
        if (change == 0) { fs::remove(f.source() / "bin/b"); fs::create_symlink("changed", f.source() / "bin/b"); }
        if (change == 1) write(f.source() / ".athena/metadata.toml", "name = 12\n");
        if (change == 2) write(f.source() / ".athena/manifest.toml", "files = false\n");
        if (change == 3) write(f.source() / "unlisted", "foreign");
        if (change == 4) write(f.source() / ".athena/manifest.toml", "[[files]]\npath = \"bin/a\"\n[[files]]\npath = \"bin/a\"\n");
        if (change == 5) { fs::rename(f.source() / "bin", f.source() / "saved"); fs::create_directory_symlink("saved", f.source() / "bin"); }
        if (change == 6) require(::chmod((f.source() / "bin/a").c_str(), 04755) == 0, "invalid mode fixture");
        if (change == 7) require(::chmod((f.source() / "bin/a").c_str(), 0700) == 0, "legacy access mode change");
        if (change == 8) require(::chmod((f.source() / "bin/a").c_str(), 0666) == 0, "untrusted payload writer permissions");
        rejected([&] { verify_legacy_seal(p, *f.state, 1, seal.source); });
    }
    { Fixture f; ProjectionIo io; io.fsync = [](int) { errno = EIO; return -1; }; auto p = f.open(io);
      require(rejected([&] { seal_legacy(p, *f.state, 1, f.source().native()); }).outcome == ProjectionOutcome::OutcomeUncertain, "Seal barrier uncertainty"); }
    // Actual supported GC alias layout, pointing into the authoritative object
    // namespace. Embedded historical manifests remain part of artifact identity.
    { Fixture f; auto p = f.open(); auto fd = athena::core::detail::open_directory(f.source());
      auto model = athena::artifact::scan_bounded(fd.get()); auto artifact = athena::artifact::derive_artifact_id(model);
      f.state.reset();
      athena::store::TransactionPins pins({f.base / "store", f.base / "generations"});
      auto prepared = f.store.prepare(f.source(), artifact); const auto object = pins.publish(f.store, prepared).path;
      fs::create_directories(f.base / "store/detour/nested");
      fs::rename(f.source(), f.base / "tree"); const auto target = "detour/nested/../../" + fs::relative(object, f.source().parent_path()).native();
      fs::create_symlink(target, f.source());
      f.state = std::make_unique<ProjectionState>(f.binding.generation_authority);
      auto seal = seal_legacy(p, *f.state, 1, f.source().native());
      const auto& alias = std::get<LegacyAliasSource>(seal.source); require(alias.alias_target == target && alias.artifact_id == artifact, "Exact alias literal and artifact identity");
      verify_legacy_seal(p, *f.state, 1, seal.source);
      fs::rename(f.source(), f.base / "old-alias"); fs::create_symlink(target, f.source());
      replace_tree(object, f.base, 100);
      std::set<RuntimeIdentity> synced; ProjectionIo io;
      io.fsync = [&](int fd) { synced.insert(identity(fd)); return ::fsync(fd); };
      auto fresh = f.open(io); verify_legacy_seal(fresh, *f.state, 1, seal.source);
      require_payload_barriers(object, synced);
      require(synced.contains(identity(f.source().parent_path())) && synced.contains(identity(object.parent_path())),
          "Alias locator and object namespace receive current barriers");
      require(synced.contains(identity(f.base / "store/detour")) && synced.contains(identity(f.base / "store/detour/nested")),
          "Alias literal detour directories synchronized without normalizing destination");
      const auto detour = identity(f.base / "store/detour/nested"); bool hit = false; ProjectionIo failure;
      failure.fsync = [&](int fd) { if (identity(fd) == detour) { hit = true; errno = EIO; return -1; } return ::fsync(fd); }; auto failing = f.open(failure);
      require(rejected([&] { verify_legacy_seal(failing, *f.state, 1, seal.source); }).cause == LegacyCause::Durability,
          "Alias current-instance durability failure rejected");
      require(hit, "Literal detour durability boundary reached");
      f.state.reset(); }
}
RuntimeIdentity identity(const fs::path& path) {
    struct stat s{}; require(::lstat(path.c_str(), &s) == 0, "fixture identity"); return {s.st_dev, s.st_ino};
}
RuntimeIdentity identity(int fd) {
    struct stat s{}; require(::fstat(fd, &s) == 0, "barrier descriptor identity"); return {s.st_dev, s.st_ino};
}
void copy_tree(const fs::path& from, const fs::path& to) {
    fs::create_directory(to);
    std::vector<std::pair<fs::path, fs::perms>> directories{{to, fs::status(from).permissions()}};
    for (const auto& e : fs::recursive_directory_iterator(from)) {
        const auto path = to / e.path().lexically_relative(from);
        const auto status = e.symlink_status();
        if (fs::is_directory(status)) {
            fs::create_directory(path); directories.emplace_back(path, status.permissions());
        } else if (fs::is_symlink(status)) fs::create_symlink(fs::read_symlink(e.path()), path);
        else { fs::copy_file(e.path(), path); fs::permissions(path, status.permissions()); }
    }
    for (auto it = directories.rbegin(); it != directories.rend(); ++it) fs::permissions(it->first, it->second);
}
void replace_tree(const fs::path& path, const fs::path& private_base, unsigned number) {
    const auto fresh = private_base / ("replacement-" + std::to_string(number));
    copy_tree(path, fresh);
    const auto before = identity(path); fs::rename(path, private_base / ("retired-" + std::to_string(number)));
    fs::rename(fresh, path); require(identity(path) != before, "New source-directory incarnation");
    // Copies and renames deliberately issue no fsync. Old content identity is
    // retained; the production operation must establish fresh durability.
}
void require_payload_barriers(const fs::path& root, const std::set<RuntimeIdentity>& synced) {
    require(synced.contains(identity(root)), "Current source root received fsync");
    for (const auto& e : fs::recursive_directory_iterator(root))
        if (!fs::is_symlink(e.symlink_status())) require(synced.contains(identity(e.path())), "Current inventory instance received fsync");
}
void scanner_provenance() {
    // A -> identical B, including nested payload/manifest/metadata instances.
    { Fixture f; auto p = f.open(); const auto seal = seal_legacy(p, *f.state, 1, f.source().native());
      const auto record = f.base / "generations/.athena-legacy-seals" / (std::get<LegacyTreeSource>(seal.source).seal_digest.value().substr(7) + ".toml");
      const auto record_id = identity(record); const auto old = identity(f.source()); replace_tree(f.source(), f.base, 0);
      std::set<RuntimeIdentity> synced; ProjectionIo io;
      io.fsync = [&](int fd) { synced.insert(identity(fd)); return ::fsync(fd); }; auto fresh = f.open(io);
      require(verify_legacy_seal(fresh, *f.state, 1, seal.source).source == seal.source, "Identical replacement verified after its own barriers");
      require_payload_barriers(f.source(), synced); require(!synced.contains(old), "Retired source durability evidence not reused");
      require(synced.contains(identity(f.source().parent_path())) && synced.contains(identity(f.base)), "Required namespace parents synchronized");
      require(identity(record) == record_id && synced.contains(record_id), "Verification resyncs existing record without replacement"); }
    // Modified replacement cannot authorize the old digest.
    { Fixture f; auto p = f.open(); const auto seal = seal_legacy(p, *f.state, 1, f.source().native());
      replace_tree(f.source(), f.base, 0); write(f.source() / "bin/a", "changed", 0755);
      require(rejected([&] { verify_legacy_seal(p, *f.state, 1, seal.source); }).cause == LegacyCause::SealMismatch, "Changed replacement rejected"); }
    // Identical nested file, directory and symlink replacements all require
    // their current file/namespace barriers, even though their digest is equal.
    for (int type = 0; type != 3; ++type) {
        Fixture f;
        if (type == 2) { fs::remove(f.source() / "bin/b"); fs::create_symlink("literal", f.source() / "bin/b"); }
        auto p = f.open(); const auto seal = seal_legacy(p, *f.state, 1, f.source().native());
        if (type == 0) { write(f.base / "fresh-file", "first", 0755); fs::rename(f.source() / "bin/a", f.base / "old-file"); fs::rename(f.base / "fresh-file", f.source() / "bin/a"); }
        if (type == 1) replace_tree(f.source() / "bin", f.base, 0);
        if (type == 2) { fs::rename(f.source() / "bin/b", f.base / "old-link"); fs::create_symlink("literal", f.source() / "bin/b"); }
        std::set<RuntimeIdentity> synced; ProjectionIo io; io.fsync = [&](int fd) { synced.insert(identity(fd)); return ::fsync(fd); };
        auto fresh = f.open(io); verify_legacy_seal(fresh, *f.state, 1, seal.source); require_payload_barriers(f.source(), synced);
    }
    // Fail EVERY observed current-instance barrier, including payload, source
    // directory, namespace parents, existing record and administrative parents.
    unsigned count = 0;
    { Fixture f; auto p = f.open(); const auto seal = seal_legacy(p, *f.state, 1, f.source().native()); ProjectionIo io;
      io.fsync = [&](int fd) { ++count; return ::fsync(fd); }; auto fresh = f.open(io); verify_legacy_seal(fresh, *f.state, 1, seal.source); }
    require(count >= 12, "Current-instance durability barrier inventory");
    for (unsigned at = 1; at <= count; ++at) {
        Fixture f; auto p = f.open(); const auto seal = seal_legacy(p, *f.state, 1, f.source().native()); replace_tree(f.source(), f.base, 0);
        const auto replacement = identity(f.source());
        ProjectionIo io; unsigned calls = 0;
        io.fsync = [&](int fd) { if (++calls == at) { errno = EIO; return -1; } return ::fsync(fd); };
        for (int retry = 0; retry != 2; ++retry) {
            calls = 0; auto failing = f.open(io); const auto e = rejected([&] { verify_legacy_seal(failing, *f.state, 1, seal.source); });
            require(e.cause == LegacyCause::Durability && e.outcome == ProjectionOutcome::OutcomeUncertain && e.error == EIO,
                "Repeated barrier failure never authorizes or loses cause/uncertainty");
            require(identity(f.source()) == replacement && !fs::exists(f.published()), "Failure preserves actual source and projection state");
        }
        std::set<RuntimeIdentity> synced; ProjectionIo recovery;
        recovery.fsync = [&](int fd) { synced.insert(identity(fd)); return ::fsync(fd); };
        auto fresh = f.open(recovery); verify_legacy_seal(fresh, *f.state, 1, seal.source); require_payload_barriers(f.source(), synced);
        require(!fs::exists(f.published()), "Seal verification does not adopt projection");
    }
    // Substitutions during synchronization must fail, despite equal content.
    for (int type = 0; type != 4; ++type) {
        Fixture f;
        if (type == 3) { fs::remove(f.source() / "bin/b"); fs::create_symlink("literal", f.source() / "bin/b"); }
        auto p = f.open(); const auto seal = seal_legacy(p, *f.state, 1, f.source().native()); const auto file = identity(f.source() / "bin/a");
        bool changed = false; ProjectionIo io;
        io.fsync = [&](int fd) { const auto rc = ::fsync(fd);
            if (!changed && identity(fd) == file) { changed = true;
                if (type == 0) replace_tree(f.source(), f.base, 0);
                if (type == 1) { write(f.base / "new-file", "first", 0755); fs::rename(f.source() / "bin/a", f.base / "old-file"); fs::rename(f.base / "new-file", f.source() / "bin/a"); }
                if (type == 2) replace_tree(f.source() / "bin", f.base, 0);
                if (type == 3) { fs::rename(f.source() / "bin/b", f.base / "old-link"); fs::create_symlink("literal", f.source() / "bin/b"); }
            } return rc;
        }; auto failing = f.open(io);
        require(rejected([&] { verify_legacy_seal(failing, *f.state, 1, seal.source); }).outcome == ProjectionOutcome::OutcomeUncertain,
            "Substitution during barriers fails closed"); require(changed, "Substitution boundary reached");
        verify_legacy_seal(p, *f.state, 1, seal.source); // fresh complete recovery
    }
    // Adoption and exact published retries must synchronize substituted sources.
    { Fixture f; auto p = f.open(); const auto seal = seal_legacy(p, *f.state, 1, f.source().native()); replace_tree(f.source(), f.base, 0);
      const auto managed = identity(f.base / "projection/bin/a"); ProjectionIo failure;
      failure.fsync = [](int) { errno = EIO; return -1; }; auto failing = f.open(failure);
      rejected([&] { adopt_legacy_projection(failing, *f.state, d1_test::adoption_id, {seal.source}); });
      require(!fs::exists(f.published()) && identity(f.base / "projection/bin/a") == managed, "Unsynchronized source cannot authorize adoption");
      for (unsigned retry = 0; retry != 2; ++retry) {
          if (retry) replace_tree(f.source(), f.base, 1);
          std::set<RuntimeIdentity> synced; ProjectionIo io; io.fsync = [&](int fd) { synced.insert(identity(fd)); return ::fsync(fd); };
          auto fresh = f.open(io); const auto result = adopt_legacy_projection(fresh, *f.state, d1_test::adoption_id, {seal.source});
          require(result == (retry ? ProjectionOutcome::NotMutated : ProjectionOutcome::MutatedDurably), "Adoption/exact retry outcome");
          require_payload_barriers(f.source(), synced); require(identity(f.base / "projection/bin/a") == managed, "Adoption keeps historical inode");
      }
      try { failing.validate_owned_symlink("bin/a", (f.source() / "bin/a").native(), {d1_test::adoption_id, 0});
          throw std::runtime_error("D2 adopted ownership authorized without source barriers");
      } catch (const ProjectionError& e) { require(e.cause == ProjectionCause::DurabilityFailure && e.outcome == ProjectionOutcome::OutcomeUncertain, "D2 retains durability failure classification"); }
    }
    // A replacement at witness creation is reverified and synchronized before
    // publication. This simulates a writer outside the exclusion contract.
    { Fixture f; auto p = f.open(); const auto seal = seal_legacy(p, *f.state, 1, f.source().native());
      bool changed = false; std::set<RuntimeIdentity> synced; ProjectionIo io; const auto link = io.link;
      io.link = [&](int a, const char* x, int b, const char* y, int flags) {
          if (!changed) { changed = true; replace_tree(f.source(), f.base, 0); synced.clear(); }
          return link(a, x, b, y, flags);
      }; io.fsync = [&](int fd) { synced.insert(identity(fd)); return ::fsync(fd); };
      auto fresh = f.open(io); adopt_legacy_projection(fresh, *f.state, d1_test::adoption_id, {seal.source});
      require(changed, "Witness substitution boundary"); require_payload_barriers(f.source(), synced);
    }
    // A substituted published seal record must be its own synchronized inode.
    { Fixture f; auto p = f.open(); const auto seal = seal_legacy(p, *f.state, 1, f.source().native());
      const auto record = f.base / "generations/.athena-legacy-seals" / (std::get<LegacyTreeSource>(seal.source).seal_digest.value().substr(7) + ".toml");
      bool changed = false; const auto previous = identity(record); ProjectionIo io;
      io.fsync = [&](int fd) { const auto rc = ::fsync(fd); if (!changed && identity(fd) == previous) {
          changed = true; fs::copy_file(record, f.base / "new-record"); fs::permissions(f.base / "new-record", fs::perms::owner_read | fs::perms::owner_write);
          fs::rename(f.base / "new-record", record);
      } return rc; }; auto failing = f.open(io);
      require(rejected([&] { verify_legacy_seal(failing, *f.state, 1, seal.source); }).outcome == ProjectionOutcome::OutcomeUncertain,
          "Seal-record inode substitution during fsync fails closed"); require(changed, "Record substitution boundary");
      verify_legacy_seal(p, *f.state, 1, seal.source);
    }
}
void empty_record_provenance() {
    // This record/directory provenance coverage does not need scanner privileges.
    { Fixture f(false); auto p = f.open(); adopt_legacy_projection(p, *f.state, d1_test::adoption_id, {});
      const auto record = f.published() / "adoption.toml"; const auto original = identity(record); bool changed = false; ProjectionIo io;
      io.fsync = [&](int fd) { const auto rc = ::fsync(fd); if (!changed && identity(fd) == original) {
          changed = true; fs::copy_file(record, f.base / "new-record"); fs::rename(f.base / "new-record", record);
      } return rc; }; auto failing = f.open(io);
      require(rejected([&] { adopt_legacy_projection(failing, *f.state, d1_test::adoption_id, {}); }).outcome == ProjectionOutcome::OutcomeUncertain,
          "Published record replacement cannot borrow retired inode barrier"); require(changed, "Published record boundary");
      bool synchronized = false; const auto replacement = identity(record); ProjectionIo recovery;
      recovery.fsync = [&](int fd) { if (identity(fd) == replacement) synchronized = true; return ::fsync(fd); };
      auto fresh = f.open(recovery); require(adopt_legacy_projection(fresh, *f.state, d1_test::adoption_id, {}) == ProjectionOutcome::NotMutated && synchronized,
          "Exact retry synchronizes current published record");
    }
    { Fixture f(false); unsigned regular_barriers = 0; bool changed = false; ProjectionIo io;
      io.fsync = [&](int fd) { const auto rc = ::fsync(fd); struct stat s{}; require(::fstat(fd, &s) == 0, "record boundary fstat");
          if (S_ISREG(s.st_mode) && ++regular_barriers == 2) {
              changed = true; const auto record = f.preparation() / "adoption.toml";
              fs::copy_file(record, f.base / "new-record"); fs::rename(f.base / "new-record", record);
          } return rc;
      }; auto failing = f.open(io);
      require(rejected([&] { adopt_legacy_projection(failing, *f.state, d1_test::adoption_id, {}); }).outcome == ProjectionOutcome::OutcomeUncertain,
          "Prepared record replacement fails after current descriptor barrier");
      require(changed && !fs::exists(f.published()) && fs::exists(f.preparation()), "Actual preparation retained without ownership publication");
      auto fresh = f.open(); adopt_legacy_projection(fresh, *f.state, d1_test::adoption_id, {});
    }
    { Fixture f(false); ProjectionIo io; const auto rename = io.rename; bool changed = false;
      io.rename = [&](int a, const char* x, int b, const char* y, unsigned flags) {
          const auto rc = rename(a, x, b, y, flags); if (!rc) {
              changed = true; fs::rename(f.published(), f.base / "retired-adoption"); copy_tree(f.base / "retired-adoption", f.published());
          } return rc;
      }; auto failing = f.open(io);
      require(rejected([&] { adopt_legacy_projection(failing, *f.state, d1_test::adoption_id, {}); }).outcome == ProjectionOutcome::OutcomeUncertain,
          "Same-byte published directory substitution fails retained-instance validation");
      require(changed && fs::exists(f.published()) && fs::exists(f.base / "retired-adoption"), "Post-publication uncertainty preserves actual namespaces");
      auto fresh = f.open(); adopt_legacy_projection(fresh, *f.state, d1_test::adoption_id, {});
    }
}
void scanner_adoption_faults() {
    int barriers = 0;
    { Fixture f; auto p = f.open(); auto seal = seal_legacy(p, *f.state, 1, f.source().native()); ProjectionIo io;
      io.fsync = [&](int fd) { ++barriers; return ::fsync(fd); }; auto injected = f.open(io);
      adopt_legacy_projection(injected, *f.state, d1_test::adoption_id, {seal.source}); }
    for (int at = 1; at <= barriers; ++at) {
        Fixture f; auto p = f.open(); auto seal = seal_legacy(p, *f.state, 1, f.source().native());
        const auto managed = f.base / "projection/bin/a"; const auto original = inode(managed);
        ProjectionIo io; int count = 0; io.fsync = [&](int fd) { if (++count == at) { errno = EIO; return -1; } return ::fsync(fd); };
        auto injected = f.open(io); require(rejected([&] { adopt_legacy_projection(injected, *f.state, d1_test::adoption_id, {seal.source}); }).outcome == ProjectionOutcome::OutcomeUncertain, "Populated barrier uncertainty");
        require(inode(managed) == original, "Failed adoption preserves historical inode");
        try { auto fresh = f.open(); adopt_legacy_projection(fresh, *f.state, d1_test::adoption_id, {seal.source}); }
        catch (const std::exception&) { require(!fs::exists(f.published()), "Incomplete preparation fails closed"); }
    }
    for (int change = 0; change < 9; ++change) {
        Fixture f; auto p = f.open(); auto seal = seal_legacy(p, *f.state, 1, f.source().native());
        if (change == 0) write(f.base / "generations/current", "0\n");
        if (change == 1) write(f.base / "generations/1/generation.toml", "id = 1\nentries = []\n");
        if (change == 2) { fs::remove(f.base / "projection/bin/a"); fs::create_symlink("wrong", f.base / "projection/bin/a"); }
        if (change == 3) fs::remove(f.base / "projection/bin/a");
        if (change == 4) fs::create_symlink("foreign", f.base / "projection/extra");
        if (change == 5) { fs::rename(f.base / "projection/bin", f.base / "saved"); fs::create_directory_symlink(f.base / "saved", f.base / "projection/bin"); }
        if (change == 6) require(::chmod(f.admin().c_str(), 0755) == 0, "wrong admin mode");
        ProjectionIo io;
        if (change == 7) io.link = [](int, const char*, int, const char*, int flags) { require(flags == 0, "No witness dereference"); errno = EIO; return -1; };
        if (change == 8) { auto direct = io.stat; io.stat = [direct](int fd, const char* name, int flags, unsigned mask, struct statx* s) {
            auto rc = direct(fd, name, flags, mask, s); if (!rc && std::string_view(name) == "a") ++s->stx_mnt_id; return rc; }; }
        invalid([&] { auto fresh = f.open(io); adopt_legacy_projection(fresh, *f.state, d1_test::adoption_id, {seal.source}); });
        require(!fs::exists(f.published()), "Conflict must not publish ownership");
    }
    { Fixture f; auto p = f.open(); auto seal = seal_legacy(p, *f.state, 1, f.source().native()); ProjectionIo io;
      io.rename = [](int, const char*, int, const char*, unsigned) { errno = EIO; return -1; }; auto injected = f.open(io);
      rejected([&] { adopt_legacy_projection(injected, *f.state, d1_test::adoption_id, {seal.source}); });
      const auto witness = f.preparation() / "witnesses/0"; fs::remove(witness); fs::create_symlink(f.source() / "bin/a", witness);
      auto fresh = f.open(); rejected([&] { adopt_legacy_projection(fresh, *f.state, d1_test::adoption_id, {seal.source}); }); }
}
void scanner_death() {
    Fixture f; { auto p = f.open(); seal_legacy(p, *f.state, 1, f.source().native()); }
    f.state.reset(); int pipefd[2]; require(::pipe2(pipefd, O_CLOEXEC) == 0, "witness death pipe");
    const auto child = ::fork(); require(child >= 0, "witness death fork");
    if (!child) {
        ::close(pipefd[0]); ProjectionIo io; auto direct = io.link;
        io.link = [&](int a, const char* x, int b, const char* y, int flags) {
            const auto rc = direct(a, x, b, y, flags);
            char ready = 'x'; if (rc || ::write(pipefd[1], &ready, 1) != 1) ::_exit(2);
            for (;;) ::pause(); return rc;
        };
        try { ProjectionState state(f.binding.generation_authority); auto p = f.open(io);
            auto seal = seal_legacy(p, state, 1, f.source().native()); adopt_legacy_projection(p, state, d1_test::adoption_id, {seal.source}); }
        catch (...) { ::_exit(3); } ::_exit(4);
    }
    ::close(pipefd[1]); char ready; require(::read(pipefd[0], &ready, 1) == 1, "Witness hardlink boundary reached"); ::close(pipefd[0]);
    require(::kill(child, SIGKILL) == 0, "kill witness child"); int status; require(::waitpid(child, &status, 0) == child && WIFSIGNALED(status), "wait witness child");
    require(inode(f.preparation() / "witnesses/0") == inode(f.base / "projection/bin/a"), "Actual unbarriered witness inode survives process death");
    require(!fs::exists(f.published()), "Partial witness preparation is not published ownership");
    const auto retry = ::fork(); require(retry >= 0, "populated retry fork");
    if (!retry) { ::execl("/proc/self/exe", "legacy_d3_test", "retry-populated", f.base.c_str(), nullptr); ::_exit(127); }
    require(::waitpid(retry, &status, 0) == retry && WIFEXITED(status) && WEXITSTATUS(status) == 0, "Fresh exec populated exact retry");
}
}
int run_tests(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "review-findings") return d1_test::run([] {
        manifest_errors(); published_retry_barriers(); recovery_parent_substitution();
        std::cout << "H1: six post-publication EIO cases, six failed recovery barriers, six ordered successful recoveries; parent substitution rejected and recovered\n"
                  << "L1: valid manifest, ten malformed manifest cases, and independent UnsafeSource classification passed\n";
    });
    if (argc == 3) return d1_test::run([&] {
        fs::path base(argv[2]); ProjectionBinding binding{d1_test::projection_id, (base / "generations").native(), (base / "store").native(), (base / "projection").native()};
        athena::store::ObjectStore store(base / "store"); ProjectionState state(binding.generation_authority);
        auto p = ProjectionContext::open(binding, store);
        if (std::string_view(argv[1]) == "retry-populated") {
            auto seal = seal_legacy(p, state, 1, (base / "store/demo-1").native()); adopt_legacy_projection(p, state, d1_test::adoption_id, {seal.source});
        } else adopt_legacy_projection(p, state, d1_test::adoption_id, {});
    });
    if (argc == 2) {
        const char* active_group = "scanner";
        try {
            // Keep the real scanner and its initial-user-namespace capability
            // gate unchanged. Report completed groups even if a later one skips.
            const auto qualify = [&](const char* name, auto action) {
                active_group = name;
                action();
                require(!qualification_cleanup_failed, "D3 fixture cleanup failed");
                std::cout << "PASS T17 D3 " << name << '\n';
            };
            qualify("scanner", scanner);
            qualify("manifest-errors", scanner_manifest_errors);
            qualify("seals", scanner_seals);
            qualify("provenance", scanner_provenance);
            qualify("adoption-faults", scanner_adoption_faults);
            qualify("death", scanner_death);
            return 0;
        }
        catch (const LegacyError& e) { if (skip(e)) { std::cout << "SKIP T17 D3 " << active_group << " (remaining groups not qualified): " << e.what() << '\n'; return 77; } std::cerr << e.what() << '\n'; return 1; }
        catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    }
    return d1_test::run([] { codecs(); unsafe_sources(); manifest_errors(); empty_adoption(); empty_faults(); published_retry_barriers(); recovery_parent_substitution(); empty_record_provenance(); death(); });
}

int main(int argc, char** argv) {
    const int result = run_tests(argc, argv);
    return qualification_cleanup_failed ? 1 : result;
}
