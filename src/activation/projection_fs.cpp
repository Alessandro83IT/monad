// SPDX-License-Identifier: GPL-3.0-or-later
#include "projection_fs.hpp"
#include "codec_detail.hpp"
#include "legacy.hpp"
#include "../store/transaction_pins.hpp"
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace athena::activation {
namespace d = core::detail;
namespace a = store::administrative;
namespace {
constexpr auto no_mutation = ProjectionOutcome::NotMutated;
[[noreturn]] void fail(ProjectionCause cause, std::string_view message, int error = 0,
                      ProjectionOutcome outcome = no_mutation) {
    throw ProjectionError(cause, outcome, error, message);
}
bool inode(const struct statx& a, const struct statx& b) {
    return a.stx_ino == b.stx_ino && a.stx_dev_major == b.stx_dev_major &&
        a.stx_dev_minor == b.stx_dev_minor && a.stx_mnt_id == b.stx_mnt_id;
}
void target_bytes(std::string_view target) {
    if (target.empty() || target.find('\0') != target.npos || target.size() > max_path_size)
        throw SchemaError(SchemaErrorCode::Path, "Invalid literal symlink destination");
}
}
ProjectionError::ProjectionError(ProjectionCause c, ProjectionOutcome o, int e, std::string_view message)
    : std::runtime_error(std::string(message) + (e ? ": " + std::string(std::strerror(e)) : "")),
      outcome(o), cause(c), error(e) {}
ProjectionIo::ProjectionIo()
    : open([](int fd, const char* name, const open_how& how) {
        return static_cast<int>(::syscall(SYS_openat2, fd, name, &how, sizeof(how))); }),
      stat([](int fd, const char* name, int flags, unsigned mask, struct statx* out) {
        return static_cast<int>(::syscall(SYS_statx, fd, name, flags, mask, out)); }),
      symlink(::symlinkat), link(::linkat), readlink(::readlinkat), fsync(::fsync),
      rename(d::linux_syscalls().rename), mkdir(::mkdirat), unlink(::unlinkat) {}
void validate_projection_path(std::string_view path) {
    validate_relative_path(path);
    if (path == ".athena-activation" || path.starts_with(".athena-activation/"))
        fail(ProjectionCause::ProjectionConflict, "Reserved projection administration path");
}
namespace {
std::string state_locator(std::string path) { validate_absolute_locator(path); return path; }
}
ProjectionState::ProjectionState(std::string generations)
    : generations_(state_locator(std::move(generations))),
      generation_(d::open_directory(generations_, false, true)),
      state_(core::state_lock_path(generations_), core::LockKind::State) {
    auto fresh = d::open_directory(generations_, false, true);
    a::same_mount(generation_.get(), fresh.get());
    const auto pinned = d::stat_fd(generation_.get()), named = d::stat_fd(fresh.get());
    if (pinned.st_dev != named.st_dev || pinned.st_ino != named.st_ino)
        fail(ProjectionCause::BindingConflict, "State authority substituted during acquisition");
}
void ProjectionContext::state(const ProjectionState& session) const {
    if (session.generations() != binding_.generation_authority || !session.state_.owns_lock() ||
        !inode(identity(generation_.get()), identity(session.generation_.get())))
        fail(ProjectionCause::BindingConflict, "State token generation authority mismatch");
}
ProjectionContext::ProjectionContext(ProjectionBinding binding, ProjectionIo io)
    : binding_(std::move(binding)), io_(std::move(io)) { binding_.validate(); }
struct statx ProjectionContext::identity(int fd, std::string_view name, bool empty) const {
    struct statx result{}; const std::string n(name);
    if (io_.stat(fd, n.c_str(), AT_SYMLINK_NOFOLLOW | (empty ? AT_EMPTY_PATH : 0),
                 STATX_BASIC_STATS | STATX_MNT_ID, &result) < 0)
        fail(ProjectionCause::IoFailure, "statx projection identity (no fallback)", errno);
    constexpr unsigned required = STATX_TYPE | STATX_MODE | STATX_UID | STATX_INO | STATX_NLINK | STATX_MNT_ID;
    if ((result.stx_mask & required) != required)
        fail(ProjectionCause::MountCrossing, "Incomplete projection statx identity");
    return result;
}
void ProjectionContext::same_mount(const struct statx& actual) const {
    if (!(actual.stx_mask & STATX_MNT_ID) || actual.stx_mnt_id != mount_.stx_mnt_id ||
        actual.stx_dev_major != mount_.stx_dev_major || actual.stx_dev_minor != mount_.stx_dev_minor)
        fail(ProjectionCause::MountCrossing, "Projection mount crossing");
}
void ProjectionContext::private_directory(int fd) const {
    const auto s = identity(fd); same_mount(s);
    if (!S_ISDIR(s.stx_mode) || s.stx_uid != ::geteuid() || (s.stx_mode & 07777) != 0700)
        fail(ProjectionCause::ProjectionConflict, "Private projection directory must be effective-owner mode 0700");
}
void ProjectionContext::unchanged(int parent, std::string_view name, int fd) const {
    struct statx named{};
    if (name.find('/') != name.npos) {
        // statx on a multi-component name would follow intermediate symlinks.
        // Reopen through the same containment constraints instead.
        open_how how{}; how.flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
        how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV;
        const std::string path(name);
        const int raw = io_.open(parent, path.c_str(), how);
        if (raw < 0) fail(ProjectionCause::UnsafeAncestor, "Named directory revalidation", errno);
        auto fresh = Fd::adopt(raw, io_.durable);
        named = identity(fresh.get());
    } else named = identity(parent, name, false);
    const auto opened = identity(fd);
    if (!inode(named, opened) || named.stx_mode != opened.stx_mode || named.stx_uid != opened.stx_uid)
        fail(ProjectionCause::ProjectionConflict, "Projection directory/record substituted");
}
ProjectionContext::Fd ProjectionContext::directory(int parent, std::string_view path, bool security) const {
    validate_relative_path(path); const std::string name(path);
    open_how how{}; how.flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV;
    const int raw = io_.open(parent, name.c_str(), how);
    if (raw < 0) fail(errno == EXDEV ? ProjectionCause::MountCrossing : ProjectionCause::UnsafeAncestor,
                      "Unsafe projection directory (openat2; no fallback)", errno);
    auto result = Fd::adopt(raw, io_.durable);
    same_mount(identity(result.get())); unchanged(parent, path, result.get());
    if (security) private_directory(result.get());
    return result;
}
void ProjectionContext::open_roots(const store::ObjectStore& store) {
    if (store.root().native() != binding_.store_authority)
        fail(ProjectionCause::BindingConflict, "ObjectStore authority mismatch");
    // Existing no-follow ancestry traversal permits the root itself to be a mountpoint.
    generation_ = d::open_directory(binding_.generation_authority, false, true);
    store_ = d::open_directory(binding_.store_authority, false, true);
    root_ = d::open_directory(binding_.projection_root);
    mount_ = identity(root_.get());
    d::require_durable_directory(root_.get(), io_.durable);
    d::require_durable_directory(generation_.get(), io_.durable);
}
std::string ProjectionContext::record(int parent, std::string_view name) const {
    d::leaf(name); const std::string n(name);
    open_how how{}; how.flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV;
    const int raw = io_.open(parent, n.c_str(), how);
    if (raw < 0) fail(ProjectionCause::BindingConflict, "Missing/unsafe projection binding; recovery required", errno);
    auto fd = Fd::adopt(raw, io_.durable);
    const auto s = identity(fd.get());
    const auto p = identity(parent);
    if (s.stx_mnt_id != p.stx_mnt_id || !S_ISREG(s.stx_mode) || s.stx_uid != ::geteuid() ||
        (s.stx_mode & 07777) != 0600 || s.stx_nlink != 1)
        fail(ProjectionCause::BindingConflict, "Unsafe projection binding record");
    unchanged(parent, name, fd.get());
    std::string bytes; char buffer[4096];
    for (;;) {
        const auto count = ::read(fd.get(), buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) fail(ProjectionCause::BindingConflict, "Read binding", errno);
        if (!count) break;
        if (bytes.size() + count > 32 * 1024) fail(ProjectionCause::BindingConflict, "Oversized binding");
        bytes.append(buffer, count);
    }
    unchanged(parent, name, fd.get()); return bytes;
}
void ProjectionContext::namespace_shape() const try {
    const auto shape = a::entries(admin_.get());
    if (shape != std::vector<std::string>{"authority.toml", "witnesses", "work"} &&
        shape != std::vector<std::string>{"adoptions", "authority.toml", "witnesses", "work"})
        fail(ProjectionCause::ProjectionConflict, "Unexpected administration entries");
    if (shape.size() == 4) LegacyAccess::namespace_shape(*this);
    for (int root : {witnesses_.get(), work_.get()}) {
        for (const auto& id : a::entries(root)) {
            ActivationId check(id);
            auto dir = directory(root, id, true);
            for (const auto& name : a::entries(dir.get())) {
                codec_detail::decimal(name);
                const auto s = identity(dir.get(), name, false); same_mount(s);
                if (!S_ISLNK(s.stx_mode) || s.stx_uid != ::geteuid())
                    fail(ProjectionCause::ForeignEntry, "Unsafe private symlink entry");
            }
        }
    }
} catch (const ProjectionError&) { throw; }
catch (const SchemaError& e) { fail(ProjectionCause::ProjectionConflict, e.what()); }
catch (const std::exception& e) { fail(ProjectionCause::IoFailure, e.what()); }
ProjectionContext ProjectionContext::open(const ProjectionBinding& binding, const store::ObjectStore& store, ProjectionIo io) {
    ProjectionContext context(binding, std::move(io)); context.open_roots(store);
    try {
        if (parse_projection_binding(context.record(context.generation_.get(), ".athena-projection-authority.toml")) != binding)
            fail(ProjectionCause::BindingConflict, "Generation projection binding mismatch");
        context.admin_ = context.directory(context.root_.get(), ".athena-activation", true);
        context.witnesses_ = context.directory(context.admin_.get(), "witnesses", true);
        context.work_ = context.directory(context.admin_.get(), "work", true);
    } catch (const SchemaError& e) { fail(ProjectionCause::BindingConflict, e.what()); }
    catch (const ProjectionError& e) {
        if (e.error == ENOENT) fail(ProjectionCause::BindingConflict, "Partial projection binding; recovery required", e.error);
        throw;
    }
    context.revalidate(); return context;
}
void ProjectionContext::revalidate() const {
    // Reopening only authority roots validates that their canonical durable locators
    // still name the pinned objects; all projection operations use retained fds.
    for (const auto& pair : {std::pair{binding_.generation_authority, generation_.get()},
                            std::pair{binding_.store_authority, store_.get()},
                            std::pair{binding_.projection_root, root_.get()}}) {
        auto fresh = d::open_directory(pair.first);
        if (!inode(identity(fresh.get()), identity(pair.second)))
            fail(ProjectionCause::BindingConflict, "Bound authority root substituted");
    }
    same_mount(identity(root_.get()));
    private_directory(admin_.get()); private_directory(witnesses_.get()); private_directory(work_.get());
    unchanged(root_.get(), ".athena-activation", admin_.get());
    unchanged(admin_.get(), "witnesses", witnesses_.get()); unchanged(admin_.get(), "work", work_.get());
    try {
        if (parse_projection_binding(record(generation_.get(), ".athena-projection-authority.toml")) != binding_ ||
            parse_projection_binding(record(admin_.get(), "authority.toml")) != binding_)
            fail(ProjectionCause::BindingConflict, "Two-sided projection binding disagreement");
    } catch (const SchemaError& e) { fail(ProjectionCause::BindingConflict, e.what()); }
    namespace_shape();
}
void ProjectionContext::barrier(int fd, bool mutated) const {
    struct stat s{};
    if (io_.durable.fstat(fd, &s) < 0 || !S_ISDIR(s.st_mode))
        fail(ProjectionCause::DurabilityFailure, "Directory barrier validation", errno,
             mutated ? ProjectionOutcome::OutcomeUncertain : no_mutation);
    int result;
    do { result = io_.fsync(fd); } while (result < 0 && errno == EINTR);
    if (result < 0) fail(ProjectionCause::DurabilityFailure, "Projection directory barrier", errno,
                         mutated ? ProjectionOutcome::OutcomeUncertain : no_mutation);
}
ProjectionContext::Fd ProjectionContext::mkdir_private(int parent, std::string_view name) const {
    d::leaf(name); const std::string n(name);
    if (io_.mkdir(parent, n.c_str(), 0700) < 0)
        fail(ProjectionCause::ProjectionConflict, "Exclusive private mkdir", errno);
    try {
        auto fd = directory(parent, name, true);
        barrier(fd.get(), true); barrier(parent, true); return fd;
    } catch (ProjectionError& e) { e.outcome = ProjectionOutcome::OutcomeUncertain; throw; }
    catch (const std::exception& e) {
        fail(ProjectionCause::IoFailure, e.what(), 0, ProjectionOutcome::OutcomeUncertain);
    }
}
ProjectionContext ProjectionContext::create_binding(const ProjectionBinding& binding, const store::ObjectStore& store, const ProjectionState& session, ProjectionIo io) {
    ProjectionContext context(binding, std::move(io)); context.open_roots(store); context.state(session);
    // Fresh means both named namespaces absent, including partial private preparation.
    for (const auto& pair : {std::pair{context.generation_.get(), ".athena-projection-authority.toml"},
                            std::pair{context.root_.get(), ".athena-activation"}}) {
        struct statx s{};
        if (context.io_.stat(pair.first, pair.second, AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_MNT_ID, &s) == 0 || errno != ENOENT)
            fail(ProjectionCause::BindingConflict, "Projection binding is not fresh; recovery required");
    }
    // Exclusive reserved directory marks partial initialization immediately.
    // Records are prepared privately; any leftovers require explicit recovery.
    bool touched = false;
    try {
        context.admin_ = context.mkdir_private(context.root_.get(), ".athena-activation");
        touched = true;
        context.witnesses_ = context.mkdir_private(context.admin_.get(), "witnesses");
        context.work_ = context.mkdir_private(context.admin_.get(), "work");
        const auto bytes = serialize(binding);
        // Publish a regular record from a private exclusive preparation directory.
        auto publish_record = [&](int parent, std::string_view name) {
            const auto temp = d::private_directory(parent, context.io_.durable);
            auto preparation = d::open_at(parent, temp, O_RDONLY | O_DIRECTORY);
            a::private_directory(preparation.get()); a::same_mount(parent, preparation.get());
            auto file = d::open_at(preparation.get(), "record", O_WRONLY | O_CREAT | O_EXCL, 0600);
            d::complete_write(file.get(), bytes, context.io_.durable);
            a::private_file(file.get());
            int synced;
            do { synced = context.io_.fsync(file.get()); } while (synced < 0 && errno == EINTR);
            if (synced < 0) fail(ProjectionCause::DurabilityFailure, "Binding file barrier", errno,
                                  ProjectionOutcome::OutcomeUncertain);
            context.barrier(preparation.get(), true);
            const std::string dest(name);
            context.unchanged(parent, temp, preparation.get());
            context.unchanged(preparation.get(), "record", file.get());
            if (context.io_.rename(preparation.get(), "record", parent, dest.c_str(), RENAME_NOREPLACE) < 0)
                fail(ProjectionCause::BindingConflict, "Binding publication conflict", errno);
            context.barrier(parent, true); context.barrier(preparation.get(), true);
            context.unchanged(parent, temp, preparation.get());
            if (context.io_.unlink(parent, temp.c_str(), AT_REMOVEDIR) < 0)
                fail(ProjectionCause::IoFailure, "Remove own empty preparation", errno, ProjectionOutcome::OutcomeUncertain);
            context.barrier(parent, true);
        };
        publish_record(context.admin_.get(), "authority.toml");
        context.barrier(context.admin_.get(), true);
        publish_record(context.generation_.get(), ".athena-projection-authority.toml");
        context.revalidate(); return context;
    } catch (d::PersistenceError& e) {
        fail(ProjectionCause::DurabilityFailure, e.what(), e.error, ProjectionOutcome::OutcomeUncertain);
    } catch (ProjectionError& e) {
        if (touched) e.outcome = ProjectionOutcome::OutcomeUncertain;
        throw;
    } catch (const std::exception& e) {
        fail(ProjectionCause::IoFailure, e.what(), 0,
             touched ? ProjectionOutcome::OutcomeUncertain : no_mutation);
    }
}
std::string ProjectionContext::witness_name(const WitnessReference& witness) const {
    // Round-trip D1 validation includes int64 bounds and canonical owner grammar.
    (void)serialize(witness);
    if (!std::holds_alternative<ActivationId>(witness.owner))
        fail(ProjectionCause::UnsupportedWitnessKind, "Adoption witnesses require D3 authorization");
    return std::to_string(witness.entry_id);
}
ProjectionContext::Fd ProjectionContext::container(int root, const WitnessReference& witness, bool create, bool* created) const {
    (void)witness_name(witness);
    const auto& id = std::get<ActivationId>(witness.owner).value();
    struct statx s{};
    if (io_.stat(root, id.c_str(), AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_MNT_ID, &s) < 0) {
        if (errno != ENOENT) fail(ProjectionCause::IoFailure, "Inspect activation container", errno);
        if (!create) fail(ProjectionCause::MissingWitness, "Missing activation witness container");
        auto result = mkdir_private(root, id);
        if (created) *created = true;
        return result;
    }
    auto result = directory(root, id, true);
    if (create) {
        // An existing container may be residue from an uncertain mkdir barrier.
        // Mutation must re-establish publication before using it; inspection
        // remains read-only. Failure stays uncertain even without a new mkdir.
        barrier(result.get(), true); barrier(root, true);
    }
    return result;
}
std::pair<ProjectionContext::Fd, std::string> ProjectionContext::parent(std::string_view path) const {
    validate_projection_path(path);
    const auto slash = path.rfind('/');
    if (slash != path.npos) return {directory(root_.get(), path.substr(0, slash)), std::string(path.substr(slash + 1))};
    const int raw = ::fcntl(root_.get(), F_DUPFD_CLOEXEC, 0);
    if (raw < 0) fail(ProjectionCause::IoFailure, "Duplicate projection root", errno);
    return {Fd::adopt(raw, io_.durable), std::string(path)};
}
std::string ProjectionContext::literal(int parent, std::string_view name) const {
    const std::string n(name);
    // Grow on equality: readlink does not append NUL and truncation is silent.
    std::string result(128, '\0');
    for (;;) {
        ssize_t count;
        do { count = io_.readlink(parent, n.c_str(), result.data(), result.size()); } while (count < 0 && errno == EINTR);
        if (count < 0) fail(ProjectionCause::OwnershipMismatch, "readlinkat literal destination", errno);
        if (static_cast<size_t>(count) < result.size()) { result.resize(count); return result; }
        if (result.size() > max_path_size) fail(ProjectionCause::OwnershipMismatch, "Oversized symlink destination");
        result.resize(std::min(result.size() * 2, max_path_size + 1));
    }
}
void ProjectionContext::link_identity(int parent, std::string_view name, std::string_view target, ProjectionCause missing) const {
    const std::string n(name); struct statx s{};
    if (io_.stat(parent, n.c_str(), AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_MNT_ID, &s) < 0)
        fail(errno == ENOENT ? missing : ProjectionCause::IoFailure, "Inspect symlink", errno);
    same_mount(s);
    if (!S_ISLNK(s.stx_mode) || s.stx_uid != ::geteuid())
        fail(ProjectionCause::ForeignEntry, "Expected effective-owner symlink");
    if (literal(parent, name) != target)
        fail(ProjectionCause::OwnershipMismatch, "Literal symlink destination mismatch");
    const auto again = identity(parent, name, false);
    if (!inode(s, again) || s.stx_mode != again.stx_mode || s.stx_uid != again.stx_uid)
        fail(ProjectionCause::OwnershipMismatch, "Symlink substituted during validation");
}
void ProjectionContext::validate_owned_symlink(std::string_view path, std::string_view target, const WitnessReference& witness) const {
    if (std::holds_alternative<AdoptionId>(witness.owner)) try {
        validate_projection_path(path); target_bytes(target); revalidate();
        auto dir = LegacyAccess::resolve(*this, path, target, witness);
        const auto name = std::to_string(witness.entry_id);
        auto [destination, leaf] = parent(path);
        link_identity(dir.get(), name, target, ProjectionCause::MissingWitness);
        link_identity(destination.get(), leaf, target, ProjectionCause::MissingManaged);
        if (!inode(identity(dir.get(), name, false), identity(destination.get(), leaf, false)))
            fail(ProjectionCause::OwnershipMismatch, "Adoption witness and managed inode differ");
        auto [again, ignored] = parent(path);
        if (!inode(identity(again.get()), identity(destination.get())))
            fail(ProjectionCause::UnsafeAncestor, "Adopted parent substituted");
        auto checked_witnesses = LegacyAccess::resolve(*this, path, target, witness);
        if (!inode(identity(checked_witnesses.get()), identity(dir.get())))
            fail(ProjectionCause::OwnershipMismatch, "Adoption witness directory substituted");
        return;
    } catch (const LegacyError& e) {
        fail(e.cause == LegacyCause::Durability ? ProjectionCause::DurabilityFailure : ProjectionCause::OwnershipMismatch,
             e.what(), e.error, e.outcome);
    }
    validate_projection_path(path); target_bytes(target); (void)witness_name(witness); revalidate();
    auto dir = container(witnesses_.get(), witness, false); const auto name = witness_name(witness);
    auto [destination, leaf] = parent(path);
    link_identity(dir.get(), name, target, ProjectionCause::MissingWitness);
    link_identity(destination.get(), leaf, target, ProjectionCause::MissingManaged);
    if (!inode(identity(dir.get(), name, false), identity(destination.get(), leaf, false)))
        fail(ProjectionCause::OwnershipMismatch, "Destination equality is insufficient ownership: symlink inode differs");
    auto [again, ignored] = parent(path);
    if (!inode(identity(again.get()), identity(destination.get())))
        fail(ProjectionCause::UnsafeAncestor, "Managed parent substituted");
    unchanged(witnesses_.get(), std::get<ActivationId>(witness.owner).value(), dir.get());
}
ProjectionOutcome ProjectionContext::create_symlink(const ProjectionState& session, std::string_view path, std::string_view target, const WitnessReference& witness) {
    state(session); validate_projection_path(path); target_bytes(target); const auto name = witness_name(witness); revalidate();
    auto [destination, leaf] = parent(path); // Reject unsafe ancestry before preparation.
    d::require_durable_directory(destination.get(), io_.durable);
    const std::string literal_target(target);
    bool prepared = false;
    try {
        auto witnesses = container(witnesses_.get(), witness, true, &prepared);
        auto work = container(work_.get(), witness, true, &prepared);
        if (io_.symlink(literal_target.c_str(), witnesses.get(), name.c_str()) < 0) {
            if (errno != EEXIST) fail(ProjectionCause::ProjectionConflict, "Exclusive witness symlink creation", errno);
            // Resume only an exact witness in the authenticated private namespace.
            link_identity(witnesses.get(), name, target, ProjectionCause::MissingWitness);
        } else prepared = true;
        barrier(witnesses.get(), true);
        link_identity(witnesses.get(), name, target, ProjectionCause::MissingWitness);
        if (io_.link(witnesses.get(), name.c_str(), work.get(), name.c_str(), 0) < 0) {
            if (errno != EEXIST) fail(ProjectionCause::IoFailure, "Hard-link symlink into private work", errno);
            link_identity(work.get(), name, target, ProjectionCause::MissingWitness);
            if (!inode(identity(witnesses.get(), name, false), identity(work.get(), name, false)))
                fail(ProjectionCause::OwnershipMismatch, "Existing work name is not the witness inode");
        } else prepared = true;
        barrier(work.get(), true);
        link_identity(work.get(), name, target, ProjectionCause::MissingWitness);
        if (!inode(identity(witnesses.get(), name, false), identity(work.get(), name, false)))
            fail(ProjectionCause::OwnershipMismatch, "Work/witness inode mismatch");
        revalidate();
        auto [checked, ignored] = parent(path);
        if (!inode(identity(checked.get()), identity(destination.get())))
            fail(ProjectionCause::UnsafeAncestor, "Destination parent substituted");
        unchanged(witnesses_.get(), std::get<ActivationId>(witness.owner).value(), witnesses.get());
        unchanged(work_.get(), std::get<ActivationId>(witness.owner).value(), work.get());
        link_identity(witnesses.get(), name, target, ProjectionCause::MissingWitness);
        link_identity(work.get(), name, target, ProjectionCause::MissingWitness);
        if (!inode(identity(witnesses.get(), name, false), identity(work.get(), name, false)))
            fail(ProjectionCause::OwnershipMismatch, "Work/witness substituted before publication");
        // True atomic NOREPLACE. An identical foreign destination remains untouched.
        if (io_.rename(work.get(), name.c_str(), destination.get(), leaf.c_str(), RENAME_NOREPLACE) < 0)
            fail(ProjectionCause::ProjectionConflict, "CREATE NOREPLACE publication", errno);
        barrier(destination.get(), true); barrier(work.get(), true);
        return ProjectionOutcome::MutatedDurably;
    } catch (ProjectionError& e) {
        if (prepared && e.outcome == no_mutation) e.outcome = ProjectionOutcome::MutatedDurably;
        throw;
    } catch (const std::exception& e) {
        fail(ProjectionCause::IoFailure, e.what(), 0,
             prepared ? ProjectionOutcome::OutcomeUncertain : no_mutation);
    }
}
ProjectionOutcome ProjectionContext::succeed_witness(const ProjectionState& session, std::string_view path, std::string_view target,
        const WitnessReference& old, const WitnessReference& successor) {
    state(session); const auto name = witness_name(successor);
    validate_owned_symlink(path, target, old);
    bool mutated = false;
    try {
        auto next = container(witnesses_.get(), successor, true, &mutated);
        auto [managed, leaf] = parent(path);
        validate_owned_symlink(path, target, old); // Immediately before namespace mutation.
        auto [checked, ignored] = parent(path);
        if (!inode(identity(checked.get()), identity(managed.get())))
            fail(ProjectionCause::UnsafeAncestor, "Succession managed parent substituted");
        unchanged(witnesses_.get(), std::get<ActivationId>(successor.owner).value(), next.get());
        if (io_.link(managed.get(), leaf.c_str(), next.get(), name.c_str(), 0) < 0) {
            if (errno != EEXIST) fail(ProjectionCause::ProjectionConflict, "Successor witness hard-link", errno);
            validate_owned_symlink(path, target, successor); // Exact private retry only.
        } else mutated = true;
        barrier(next.get(), true);
        validate_owned_symlink(path, target, successor);
        return mutated ? ProjectionOutcome::MutatedDurably : no_mutation;
    } catch (ProjectionError& e) {
        if (mutated && e.outcome == no_mutation) e.outcome = ProjectionOutcome::MutatedDurably;
        throw;
    } catch (const std::exception& e) {
        fail(ProjectionCause::IoFailure, e.what(), 0,
             mutated ? ProjectionOutcome::OutcomeUncertain : no_mutation);
    }
}
}
