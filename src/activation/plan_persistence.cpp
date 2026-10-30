// SPDX-License-Identifier: GPL-3.0-or-later
#include "plan_persistence.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/fs.h>
#include <memory>
#include <set>
#include <system_error>
#include <unistd.h>

namespace athena::activation {
namespace d = core::detail;
namespace {
using Cause = PlanPersistenceCause;
using Outcome = ProjectionOutcome;
constexpr std::size_t capacity = 4096;
// STATX_BASIC_STATS is a request mask, not a guarantee about returned fields.
constexpr unsigned evidence_statx_fields = STATX_TYPE | STATX_MODE | STATX_UID | STATX_INO |
    STATX_NLINK | STATX_MNT_ID | STATX_SIZE | STATX_MTIME | STATX_CTIME;
static_assert(((STATX_BASIC_STATS | STATX_MNT_ID) & evidence_statx_fields) == evidence_statx_fields);
void require_evidence_fields(const struct statx& value) {
    if ((value.stx_mask & evidence_statx_fields) != evidence_statx_fields)
        throw PlanPersistenceError(Cause::UnsafeEntry, Outcome::NotMutated, 0,
            "Incomplete R2-A statx observation", std::make_exception_ptr(ProjectionError(
                (value.stx_mask & STATX_MNT_ID) ? ProjectionCause::ProjectionConflict : ProjectionCause::MountCrossing,
                Outcome::NotMutated, 0, "Incomplete R2-A statx fields")));
}

bool inode(const struct statx& a, const struct statx& b) {
    return a.stx_ino == b.stx_ino && a.stx_dev_major == b.stx_dev_major &&
        a.stx_dev_minor == b.stx_dev_minor && a.stx_mnt_id == b.stx_mnt_id;
}
bool observation(const struct statx& a, const struct statx& b) {
    return inode(a, b) && a.stx_mode == b.stx_mode && a.stx_uid == b.stx_uid &&
        a.stx_nlink == b.stx_nlink && a.stx_size == b.stx_size &&
        a.stx_mtime.tv_sec == b.stx_mtime.tv_sec && a.stx_mtime.tv_nsec == b.stx_mtime.tv_nsec &&
        a.stx_ctime.tv_sec == b.stx_ctime.tv_sec && a.stx_ctime.tv_nsec == b.stx_ctime.tv_nsec;
}
}
PlanPersistenceError::PlanPersistenceError(Cause c, Outcome o, int e, std::string_view message,
    std::exception_ptr nested)
    : std::runtime_error(std::string(message) + (e ? ": " + std::string(std::strerror(e)) : "")),
      cause(c), outcome(o), error(e), underlying(std::move(nested)) {}

namespace plan_detail {
bool OperationEvidence::same_identity(const struct statx& a, const struct statx& b) {
    require_evidence_fields(a); require_evidence_fields(b); return inode(a, b);
}
bool OperationEvidence::same_observation(const struct statx& a, const struct statx& b) {
    require_evidence_fields(a); require_evidence_fields(b); return observation(a, b);
}
OperationEvidence::ObjectId OperationEvidence::add(std::optional<ObjectId> parent, std::string name,
    const struct statx& value, d::Fd pin) {
    require_evidence_fields(value);
    if (parent && (*parent >= objects.size() || name.empty() || name.size() > 255 || name.find('/') != name.npos))
        throw std::logic_error("Invalid evidence parent/component");
    if (const auto existing = find(parent, name)) {
        auto& object = objects.at(*existing);
        if (!same_observation(object.metadata, value)) throw std::logic_error("Cannot replace evidence anchor");
        if (pin.get() >= 0 && !object.pin) {
            if (pins.size() == max_pinned_fds) throw std::logic_error("Evidence pin budget exceeded");
            pins.push_back(std::move(pin)); object.pin = pins.size() - 1;
        }
        return *existing;
    }
    if (pins.size() + (pin.get() >= 0) > max_pinned_fds) throw std::logic_error("Evidence pin budget exceeded");
    if (!scope) scope = std::make_shared<const Scope>();
    const auto id = objects.size();
    const auto inserted = relationships.emplace(std::make_pair(parent, name), id).first;
    try { objects.push_back({parent, std::move(name), value, std::nullopt}); }
    catch (...) { relationships.erase(inserted); throw; }
    try {
        if (pin.get() >= 0) { pins.push_back(std::move(pin)); objects.back().pin = pins.size() - 1; }
    } catch (...) { objects.pop_back(); relationships.erase(inserted); throw; }
    return id;
}
std::optional<OperationEvidence::ObjectId> OperationEvidence::find(std::optional<ObjectId> parent, std::string_view name) const {
    const auto found = relationships.find(std::make_pair(parent, std::string(name)));
    return found == relationships.end() ? std::optional<ObjectId>{} : found->second;
}
bool OperationEvidence::matches(ObjectId id, const struct statx& value) const {
    return same_observation(objects.at(id).metadata, value);
}
bool OperationEvidence::same_inventory(const Inventory& a, const Inventory& b) const {
    if (!scope || a.scope != scope || b.scope != scope || a.phase != b.phase || a.directory != b.directory || a.entries.size() != b.entries.size() ||
        !same_observation(a.before, a.after) || !same_observation(b.before, b.after) ||
        !same_observation(a.after, b.after)) return false;
    for (std::size_t n = 0; n < a.entries.size(); ++n) {
        if (a.entries[n].object != b.entries[n].object || a.entries[n].object >= objects.size()) return false;
        const auto& x = a.entries[n].observation; const auto& y = b.entries[n].observation;
        if (x.parent != a.directory || y.parent != b.directory || x.parent != y.parent || x.name != y.name || !same_observation(x.metadata, y.metadata)) return false;
    }
    return true;
}
void OperationEvidence::record_transition(TransitionKind kind, Phase phase, ObjectId id,
    ObjectId destination, std::string name, const struct statx& instance) {
    auto& object = objects.at(id); (void)objects.at(destination);
    if (!object.parent || !object.pin || !same_identity(object.metadata, instance) ||
        name.empty() || name.size() > 255 || name.find('/') != name.npos)
        throw std::logic_error("Transition lacks retained instance/relationship");
    if ((kind == TransitionKind::BootstrapPublication &&
         (phase != Phase::Bootstrap || *object.parent != destination || object.name != ".plans-bootstrap" || name != "plans")) ||
        (kind == TransitionKind::PlanPublication &&
         (phase != Phase::Publication || objects.at(*object.parent).name != ".preparations" ||
          objects.at(destination).name != "plans" || objects.at(*object.parent).parent != destination || object.name != name || name.size() != 32 || !lowercase_hex(name))))
        throw std::logic_error("Unexpected evidence namespace transition");
    if (const auto occupied = find(destination, name); occupied && *occupied != id)
        throw std::logic_error("Evidence transition destination is occupied");
    const auto previous = relationships.find(std::make_pair(object.parent, object.name));
    const auto inserted = relationships.emplace(std::make_pair(std::optional<ObjectId>{destination}, name), id);
    try { transitions.push_back({kind, phase, id, *object.parent, destination, object.name, name, instance}); }
    catch (...) { if (inserted.second) relationships.erase(inserted.first); throw; }
    if (previous != inserted.first) relationships.erase(previous);
    object.parent = destination; object.name = std::move(name);
}
void OperationEvidence::record_barrier(Phase phase, ObjectId id, const struct statx& instance) {
    const auto& object = objects.at(id);
    if (!object.pin || !same_identity(object.metadata, instance))
        throw std::logic_error("Barrier lacks retained observed instance");
    barriers.push_back({phase, id, instance});
}
}

struct PlanAccess::Operation {
    const ProjectionContext& p;
    const ProjectionState* state = nullptr;
    // Shared acquisition storage; each integrated API retains it through its concluding phase.
    plan_detail::OperationEvidence evidence;
    // Original anchors never advance. Only observations of our own directory
    // mutations and the active phase inventory indexes advance, invocation-locally.
    std::map<plan_detail::OperationEvidence::ObjectId, struct statx> own_observations;
    std::vector<std::size_t> phase_inventories;
    // One ledger for the entire invocation. Helpers never replace it with their
    // local outcome. A phase completes only after its own full final checks.
    enum class Phase : unsigned { Bootstrap = 1, Preparation = 2, Publication = 4, Recovery = 8 };
    Phase phase = Phase::Bootstrap;
    unsigned mutations = 0, pending = 0, durable = 0;
    unsigned phase_bit() const { return static_cast<unsigned>(phase); }
    void arm() { pending |= phase_bit(); }
    void mutation() { mutations |= phase_bit(); arm(); }
    void complete() {
        // Completing a helper certifies only its own phase. Pending bits from
        // any earlier phase survive; the cumulative mutation bits never reset.
        durable |= phase_bit(); pending &= ~phase_bit();
    }
    struct Node {
        d::Fd fd;
        int parent;
        std::string name;
        bool file;
        struct statx initial;
    };
    struct Namespace { Node plans, preparations; };
    struct Pair {
        Node container, intent, plan;
        std::string intent_bytes, plan_bytes;
        InspectedActivationPlan document;
    };
    Outcome outcome() const {
        return pending || (mutations & ~durable) ? Outcome::OutcomeUncertain :
            mutations ? Outcome::MutatedDurably : Outcome::NotMutated;
    }
    [[noreturn]] void fail(Cause c, std::string_view text, int e = 0) const {
        throw PlanPersistenceError(c, outcome(), e, text);
    }
    [[noreturn]] void unsafe(ProjectionCause c, std::string_view text, int e = 0) const {
        throw PlanPersistenceError(Cause::UnsafeEntry, outcome(), e, text,
            std::make_exception_ptr(ProjectionError(c, outcome(), e, text)));
    }
    template<class F> auto run(F action) -> decltype(action()) {
        try { return action(); }
        catch (PlanPersistenceError& e) { e.outcome = outcome(); throw; }
        catch (const SchemaError& e) {
            throw PlanPersistenceError(Cause::InvalidDocument, outcome(), 0, e.what(), std::current_exception());
        } catch (const ProjectionError& e) {
            const auto c = e.cause == ProjectionCause::BindingConflict ? Cause::AuthorityMismatch :
                e.cause == ProjectionCause::IoFailure ? Cause::Io : Cause::UnsafeEntry;
            throw PlanPersistenceError(c, outcome(), e.error, e.what(), std::current_exception());
        } catch (const d::PersistenceError& e) {
            throw PlanPersistenceError(e.stage == d::PersistenceError::Stage::Write ? Cause::Io : Cause::Durability,
                outcome(), e.error, e.what(), std::current_exception());
        } catch (const std::system_error& e) {
            throw PlanPersistenceError(Cause::Io, outcome(), e.code().value(), e.what(), std::current_exception());
        } catch (const std::exception& e) {
            throw PlanPersistenceError(Cause::Io, outcome(), 0, e.what(), std::current_exception());
        }
    }
    struct statx stat(int fd, std::string_view name = {}, bool empty = true, bool complete = false) const {
        struct statx result{}; const std::string leaf(name);
        if (p.io_.stat(fd, leaf.c_str(), AT_SYMLINK_NOFOLLOW | (empty ? AT_EMPTY_PATH : 0),
                STATX_BASIC_STATS | STATX_MNT_ID, &result) < 0)
            fail(errno == ENOSYS ? Cause::UnsupportedFilesystem : Cause::Io, "D4 statx (no fallback)", errno);
        if (complete) { require_evidence_fields(result); return result; }
        constexpr auto required = STATX_TYPE | STATX_MODE | STATX_UID | STATX_INO | STATX_NLINK | STATX_MNT_ID;
        if ((result.stx_mask & required) != required)
            unsafe((result.stx_mask & STATX_MNT_ID) ? ProjectionCause::ProjectionConflict : ProjectionCause::MountCrossing,
                "Incomplete D4 statx metadata/mount identity");
        return result;
    }
    bool exists(int parent, std::string_view name) const {
        struct statx result{}; const std::string leaf(name);
        if (p.io_.stat(parent, leaf.c_str(), AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_MNT_ID, &result) == 0) return true;
        if (errno == ENOENT) return false;
        fail(errno == ENOSYS ? Cause::UnsupportedFilesystem : Cause::Io, "Inspect D4 entry", errno);
    }
    void metadata(const struct statx& s, bool file) const {
        if (s.stx_mnt_id != p.mount_.stx_mnt_id || s.stx_dev_major != p.mount_.stx_dev_major ||
            s.stx_dev_minor != p.mount_.stx_dev_minor) unsafe(ProjectionCause::MountCrossing, "D4 mount crossing");
        if (s.stx_uid != ::geteuid() || (s.stx_mode & 07777) != (file ? 0600 : 0700) ||
            (file ? !S_ISREG(s.stx_mode) || s.stx_nlink != 1 : !S_ISDIR(s.stx_mode)))
            fail(Cause::UnsafeEntry, "Unsafe D4 type/owner/mode/link count");
        if (file) {
            constexpr auto required = STATX_SIZE | STATX_MTIME | STATX_CTIME;
            if ((s.stx_mask & required) != required) fail(Cause::UnsafeEntry, "Incomplete D4 file observations");
            if (s.stx_size > max_document_size) throw SchemaError(SchemaErrorCode::Bounds, "D1 document size limit exceeded");
        }
    }
    // Evidence acquisition/comparison requires complete observations at every
    // opening/check boundary, not just on the final retained stat.
    Node open(int parent, std::string_view name, bool file = false, Cause missing = Cause::NamespaceIncomplete, bool complete = false) const {
        if (!exists(parent, name)) fail(missing, "Missing D4 entry");
        const auto before = stat(parent, name, false, complete);
        metadata(before, file);
        open_how how{};
        how.flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | (file ? 0 : O_DIRECTORY);
        how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV;
        const std::string leaf(name);
        const int raw = p.io_.open(parent, leaf.c_str(), how);
        if (raw < 0) {
            const int error = errno;
            if (error == EXDEV || error == ELOOP || error == ENOTDIR)
                unsafe(error == EXDEV ? ProjectionCause::MountCrossing : ProjectionCause::UnsafeAncestor,
                    "D4 openat2 (no fallback)", error);
            fail(error == ENOSYS ? Cause::UnsupportedFilesystem : Cause::Io, "D4 openat2 (no fallback)", error);
        }
        auto fd = d::Fd::adopt(raw, p.io_.durable);
        const auto pinned = stat(fd.get(), {}, true, complete); metadata(pinned, file);
        if (!inode(before, pinned)) fail(Cause::Substituted, "D4 entry changed while opening");
        Node result{std::move(fd), parent, leaf, file, pinned}; check(result, complete); return result;
    }
    void check(const Node& node, bool complete = false) const {
        if (!exists(node.parent, node.name)) fail(Cause::Substituted, "Pinned D4 entry disappeared");
        const auto named = stat(node.parent, node.name, false, complete), pinned = stat(node.fd.get(), {}, true, complete);
        if (!inode(named, node.initial) || !inode(pinned, node.initial)) fail(Cause::Substituted, "Pinned D4 instance substituted");
        metadata(named, node.file); metadata(pinned, node.file);
        if (node.file && (!observation(named, node.initial) || !observation(pinned, node.initial)))
            fail(Cause::Substituted, "D4 document changed during operation");
    }
    struct DirectoryObservation {
        int fd;
        struct statx initial;
    };
    struct statx directory_observation(int fd) const {
        auto value = evidence_stat(fd);
        if (!S_ISDIR(value.stx_mode))
            fail(Cause::UnsafeEntry, "Incomplete directory change observations");
        return value;
    }
    void unchanged_directory(const DirectoryObservation& directory) const {
        if (!observation(directory.initial, directory_observation(directory.fd)))
            fail(Cause::Substituted, "D4 inventory changed during validation");
    }
    Node binding_record(int parent, const char* name) const {
        open_how how{}; how.flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
        how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV;
        const int raw = p.io_.open(parent, name, how);
        if (raw < 0) fail(Cause::AuthorityMismatch, "Open retained D2 binding", errno);
        auto fd = d::Fd::adopt(raw, p.io_.durable);
        Node record{std::move(fd), parent, name, true, {}};
        record.initial = evidence_stat(record.fd.get());
        check_binding(record); binding_contents(record); return record;
    }
    void check_binding(const Node& record) const {
        const auto named = evidence_stat(record.parent, record.name, false), held = evidence_stat(record.fd.get());
        for (const auto& value : {named, held}) {
            if (!S_ISREG(value.stx_mode) ||
                value.stx_uid != ::geteuid() || (value.stx_mode & 07777) != 0600 || value.stx_nlink != 1 ||
                !observation(value, record.initial))
                fail(Cause::AuthorityMismatch, "D2 binding observation changed");
        }
    }
    void binding_contents(const Node& record) const {
        // Reuse the exact D2 constrained reader and its metadata/mount gates.
        // The separately pinned record prevents replacement from borrowing its
        // predecessor's observation, including identical replacement bytes.
        if (p.record(record.parent, record.name) != serialize(p.binding_))
            fail(Cause::AuthorityMismatch, "D2 binding bytes changed");
    }
    std::vector<std::string> entries(int fd, std::size_t limit,
        const std::function<void(const std::string&)>& visit = {}) const {
        const DirectoryObservation before{fd, directory_observation(fd)};
        // dup would share the directory offset. Open '.' for a fresh description.
        open_how how{}; how.flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW;
        how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV;
        const int raw = p.io_.open(fd, ".", how);
        if (raw < 0) fail(errno == ENOSYS ? Cause::UnsupportedFilesystem : Cause::Io, "Open fresh D4 enumeration", errno);
        auto fresh = d::Fd::adopt(raw, p.io_.durable);
        if (!inode(evidence_stat(fd), evidence_stat(fresh.get()))) fail(Cause::Substituted, "Enumeration instance mismatch");
        DIR* raw_dir = ::fdopendir(fresh.get());
        if (!raw_dir) fail(Cause::Io, "fdopendir D4 enumeration", errno);
        fresh.release();
        const auto close = [](DIR* value) { ::closedir(value); };
        std::unique_ptr<DIR, decltype(close)> dir(raw_dir, close);
        std::vector<std::string> names; std::size_t count = 0;
        for (;;) {
            errno = 0; const auto* entry = p.io_.readdir(dir.get());
            if (!entry) { if (errno) fail(Cause::Io, "readdir D4 enumeration", errno); break; }
            const auto length = ::strnlen(entry->d_name, sizeof(entry->d_name));
            if (length == 1 && entry->d_name[0] == '.') continue;
            if (length == 2 && entry->d_name[0] == '.' && entry->d_name[1] == '.') continue;
            if (length == 0 || length > 255) fail(Cause::UnsafeEntry, "D4 component length limit");
            if (++count > limit) fail(Cause::AdministrativeLimit, "D4 entry limit exceeded");
            const std::string name(entry->d_name, length);
            if (visit) visit(name); // Grammar/budget checked before storage/open.
            names.push_back(name);
        }
        unchanged_directory(before);
        if (!observation(before.initial, directory_observation(::dirfd(dir.get()))))
            fail(Cause::Substituted, "D4 enumeration changed its retained instance");
        std::sort(names.begin(), names.end()); return names;
    }
    void id_name(const std::string& name) const {
        if (name.size() != 32 || !lowercase_hex(name)) fail(Cause::UnsafeEntry, "Noncanonical D4 activation container name");
    }
    void exact_pair(int container, Cause cause) const {
        const auto names = entries(container, 2, [&](const auto& name) {
            if (name != "intent.toml" && name != "plan.toml") fail(cause, "Unexpected D4 container entry");
        });
        if (names != std::vector<std::string>{"intent.toml", "plan.toml"}) fail(cause, "Incomplete D4 document pair");
    }
    std::size_t shape() const {
        if (exists(p.admin_.get(), ".plans-bootstrap")) {
            auto bootstrap = open(p.admin_.get(), ".plans-bootstrap");
            entries(bootstrap.fd.get(), 1, [&](const auto& name) {
                if (name != ".preparations") fail(Cause::BootstrapConflict, "Unexpected bootstrap child");
                auto child = open(bootstrap.fd.get(), name);
            });
            check(bootstrap);
        }
        if (!exists(p.admin_.get(), "plans")) return 0;
        auto plans = open(p.admin_.get(), "plans");
        if (!exists(plans.fd.get(), ".preparations")) fail(Cause::NamespaceIncomplete, "Missing plans/.preparations");
        auto prep = open(plans.fd.get(), ".preparations");
        std::set<std::string> ids;
        auto reserve = [&](const std::string& name) {
            id_name(name);
            if (ids.size() == capacity) fail(Cause::AdministrativeLimit, "Combined D4 container limit exceeded");
            if (!ids.insert(name).second) fail(Cause::PreparationConflict, "Activation exists in both D4 locations");
        };
        entries(plans.fd.get(), capacity + 1, [&](const auto& name) {
            if (name == ".preparations") return;
            reserve(name); auto container = open(plans.fd.get(), name);
            exact_pair(container.fd.get(), Cause::NamespaceIncomplete);
            for (const auto* file : {"intent.toml", "plan.toml"}) { auto document = open(container.fd.get(), file, true); }
            check(container);
        });
        entries(prep.fd.get(), capacity, [&](const auto& name) {
            reserve(name); auto container = open(prep.fd.get(), name);
        });
        check(prep); check(plans); return ids.size();
    }
    void context() const {
        if (state) p.state(*state);
        p.revalidate_impl(true);
    }
    void preflight() const {
        context();
        try { d::require_durable_directory(p.root_.get(), p.io_.durable); }
        catch (const std::system_error& e) {
            throw PlanPersistenceError(Cause::Io, outcome(), e.code().value(), e.what(), std::current_exception());
        }
        catch (const std::exception& e) {
            throw PlanPersistenceError(Cause::UnsupportedFilesystem, outcome(), 0, e.what(), std::current_exception());
        }
    }
    void bound(const ActivationIntent& intent) const {
        const auto& b = p.binding_;
        if (intent.generation_authority != b.generation_authority || intent.store_authority != b.store_authority ||
            intent.projection_root != b.projection_root || intent.projection_id != b.projection_id)
            fail(Cause::AuthorityMismatch, "Activation intent differs from exact D2 binding");
    }
    Node mkdir(int parent, std::string_view name, Cause conflict = Cause::PreparationConflict, bool complete = false) {
        const std::string leaf(name);
        if (p.io_.mkdir(parent, leaf.c_str(), 0700) < 0) {
            const int error = errno;
            // Fault seams may report failure after a real syscall. Never claim
            // a clean outcome if the previously absent private entry appeared.
            try { if (exists(parent, name)) arm(); }
            catch (...) { arm(); throw; }
            fail(error == EEXIST ? conflict : Cause::Io, "Exclusive D4 mkdir", error);
        }
        mutation();
        return open(parent, name, false, Cause::NamespaceIncomplete, complete);
    }
    struct statx barrier(int fd, const std::function<void()>& validate, bool validate_evidence_fields = false) {
        arm(); validate();
        const auto before = validate_evidence_fields ? evidence_stat(fd) : stat(fd);
        int rc; do { rc = p.io_.fsync(fd); } while (rc < 0 && errno == EINTR);
        if (rc < 0) fail(Cause::Durability, "D4 required fsync", errno);
        const auto after = validate_evidence_fields ? evidence_stat(fd) : stat(fd);
        if (!inode(before, after) || before.stx_mode != after.stx_mode || before.stx_uid != after.stx_uid)
            fail(Cause::Substituted, "D4 barrier instance changed");
        validate();
        return after;
    }
    Namespace namespace_open(bool complete = false) const {
        if (!exists(p.admin_.get(), "plans")) fail(Cause::MissingPlan, "No published plan namespace");
        if (exists(p.admin_.get(), ".plans-bootstrap")) fail(Cause::BootstrapConflict, "Ambiguous bootstrap during targeted D4 access");
        auto plans = open(p.admin_.get(), "plans", false, Cause::NamespaceIncomplete, complete);
        auto prep = open(plans.fd.get(), ".preparations", false, Cause::NamespaceIncomplete, complete);
        return {std::move(plans), std::move(prep)};
    }
    void ensure() {
        phase = Phase::Bootstrap;
        acquire_evidence({}, false, true);
        // The invocation owns bootstrap pins and its completed barriers through
        // publication, including exceptional exits. No helper transfers them away.
        auto& e = evidence;
        using E = plan_detail::OperationEvidence;
        const auto root = *e.find({}, p.binding_.projection_root);
        const auto admin = *e.find(root, ".athena-activation");
        const bool published = e.find(admin, "plans").has_value();
        const bool bootstrap = e.find(admin, ".plans-bootstrap").has_value();
        if (published && bootstrap) fail(Cause::BootstrapConflict, "Both bootstrap and plans exist");
        // Original object anchors are immutable. Only the expected directory
        // observations affected by our own mkdir/rename may advance.
        auto add_directory = [&](E::ObjectId parent, const char* name, Cause conflict = Cause::BootstrapConflict) {
            auto node = mkdir(evidence_fd(e, parent), name, conflict, true);
            const auto id = e.add(parent, name, node.initial, std::move(node.fd));
            own_directory_changed(parent);
            return id;
        };
        const auto plans = published ? *e.find(admin, "plans") : bootstrap ? *e.find(admin, ".plans-bootstrap") :
            add_directory(admin, ".plans-bootstrap");
        const bool fresh = !published && !bootstrap;
        const auto existing_prep = e.find(plans, ".preparations");
        if (!fresh && !existing_prep) fail(published ? Cause::NamespaceIncomplete : Cause::BootstrapConflict, "Incomplete infrastructure; no repair");
        const auto prep = fresh ? add_directory(plans, ".preparations", Cause::PreparationConflict) : *existing_prep;
        auto collect = [&](E::ObjectId directory) { return publication_inventory(directory, E::Phase::Bootstrap); };
        auto& batches = phase_inventories;
        auto advance_phase_inventories = [&] {
            // Original batches survive; new batches use only recorded own
            // relationships and explicitly advanced directory observations.
            batches.clear();
            for (auto directory : {admin, plans, prep}) {
                auto batch = collect(directory);
                batches.push_back(e.inventories.size()); e.inventories.push_back(std::move(batch));
            }
        };
        if (fresh) advance_phase_inventories();
        else for (std::size_t index = 0; index < e.inventories.size(); ++index) {
            const auto directory = e.inventories[index].directory;
            if (directory == admin || directory == plans || directory == prep) batches.push_back(index);
        }
        if (batches.size() != 3) throw std::logic_error("Missing bootstrap inventory evidence");
        auto named_checks = [&] {
            if (state) p.state(*state);
            evidence_bindings(e, false);
            for (E::ObjectId id = 0; id < e.objects.size(); ++id) {
                const auto& object = e.objects[id];
                if (!object.pin || std::any_of(e.bindings.begin(), e.bindings.end(), [&](const auto& binding) { return binding.object == id; })) continue;
                evidence_chain(e, id);
                if (!E::same_observation(expected_observation(id), evidence_stat(evidence_fd(e, id)))) fail(Cause::Substituted, "Bootstrap retained instance changed");
            }
            if (e.objects.at(plans).name == ".plans-bootstrap") {
                if (exists(evidence_fd(e, admin), "plans")) fail(Cause::BootstrapConflict, "Bootstrap destination appeared");
                if (!e.foreign.empty()) fail(Cause::BootstrapConflict, "Nonempty bootstrap preparation");
            } else if (exists(evidence_fd(e, admin), ".plans-bootstrap")) fail(Cause::BootstrapConflict, "Bootstrap source remains");
        };
        auto validate = [&] {
            if (state) p.state(*state);
            for (const auto index : batches) {
                const auto& original = e.inventories.at(index);
                if (!e.same_inventory(original, collect(original.directory))) fail(Cause::Substituted, "Bootstrap concluding inventory changed");
            }
            evidence_bindings(e, true);
            evidence_foreign(e);
            named_checks();
        };
        auto barrier_checks = [&] {
            if (state) p.state(*state);
            evidence_bindings(e, true);
            named_checks();
        };
        auto barriers = [&] {
            // Barriers check their retained/named instances and binding. The
            // complete structural batch and foreign interiors conclude the phase.
            for (auto id : {prep, plans, admin, root}) evidence_barrier(e, E::Phase::Bootstrap, id, barrier_checks);
        };
        validate();
        if (!published) {
            barriers(); validate();
            arm(); validate();
            const auto old_name = e.objects.at(plans).name;
            if (p.io_.rename(evidence_fd(e, admin), old_name.c_str(), evidence_fd(e, admin), "plans", RENAME_NOREPLACE) < 0)
                fail(errno == EEXIST ? Cause::BootstrapConflict : errno == ENOSYS || errno == EINVAL || errno == EOPNOTSUPP ?
                    Cause::UnsupportedFilesystem : Cause::Io, "D4 NOREPLACE rename", errno);
            mutation();
            evidence_transition(e, E::TransitionKind::BootstrapPublication, E::Phase::Bootstrap, plans, admin, "plans");
            if (exists(evidence_fd(e, admin), old_name)) fail(Cause::Substituted, "Bootstrap source retained after rename");
            own_directory_changed(plans); own_directory_changed(admin);
            advance_phase_inventories();
        }
        barriers(); validate();
        const auto count = published ? 4u : 8u;
        if (e.barriers.size() != count) throw std::logic_error("Incomplete bootstrap barrier evidence");
        complete();
    }
    std::string read(const Node& file) const {
        check(file); const auto before = stat(file.fd.get());
        std::string bytes; std::array<char, 16384> buffer{};
        for (;;) {
            const auto n = p.io_.pread(file.fd.get(), buffer.data(), buffer.size(), static_cast<off_t>(bytes.size()));
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) fail(Cause::Io, "Read D4 document", errno);
            if (static_cast<std::size_t>(n) > buffer.size()) fail(Cause::Io, "Invalid D4 read count");
            if (n == 0) break;
            if (static_cast<std::size_t>(n) > max_document_size - bytes.size()) throw SchemaError(SchemaErrorCode::Bounds, "D1 read size bound");
            bytes.append(buffer.data(), static_cast<std::size_t>(n));
        }
        if (!observation(before, stat(file.fd.get())) || bytes.size() != before.stx_size)
            fail(Cause::Substituted, "D4 file EOF/size/observation mismatch");
        check(file); return bytes;
    }
    using Evidence = plan_detail::OperationEvidence;
    struct statx evidence_stat(int fd, std::string_view name = {}, bool empty = true) const {
        return stat(fd, name, empty, true); // Validate the complete mask before consuming fields.
    }
    // Stable IDs and immutable anchors belong to this evidence, not to a new scan.
    Evidence::ObjectId evidence_object(Evidence& e, Evidence::ObjectId parent,
        const std::string& name, const struct statx& value) const {
        require_evidence_fields(value);
        if (auto id = e.find(parent, name)) {
            if (!e.matches(*id, value)) fail(Cause::Substituted, "Evidence anchor changed");
            return *id;
        }
        return e.add(parent, name, value);
    }
    template<class Visit> Evidence::Inventory evidence_inventory(Evidence& e, int fd,
        Evidence::ObjectId id, std::size_t limit, Visit visit) const {
        Evidence::Inventory batch{Evidence::Phase::Inspection, id, directory_observation(fd), {}, {}, e.scope};
        entries(fd, limit, [&](const auto& name) {
            const auto child = visit(name);
            const auto& anchor = e.objects.at(child);
            if (anchor.parent != id || anchor.name != name) fail(Cause::Substituted, "Evidence inventory relationship changed");
            auto snapshot = anchor; snapshot.pin.reset();
            batch.entries.push_back({child, std::move(snapshot)});
        });
        batch.after = directory_observation(fd);
        if (!Evidence::same_observation(batch.before, batch.after)) fail(Cause::Substituted, "Evidence enumeration changed");
        std::sort(batch.entries.begin(), batch.entries.end(), [](const auto& a, const auto& b) {
            return a.observation.name < b.observation.name;
        });
        return batch;
    }
    int evidence_fd(const Evidence& e, Evidence::ObjectId id) const {
        const auto& object = e.objects.at(id);
        if (!object.pin) throw std::logic_error("Evidence object is not pinned");
        return e.pins.at(*object.pin).get();
    }
    void evidence_chain(const Evidence& e, Evidence::ObjectId id) const {
        std::vector<Evidence::ObjectId> chain;
        for (std::optional<Evidence::ObjectId> current{id}; current; current = e.objects.at(*current).parent) {
            if (chain.size() == e.objects.size()) throw std::logic_error("Cyclic evidence ancestry");
            chain.push_back(*current);
        }
        // Walk from current locators down to the target, without recursive validation.
        for (auto entry = chain.rbegin(); entry != chain.rend(); ++entry) {
            const auto& object = e.objects.at(*entry);
            require_evidence_fields(object.metadata);
            const auto held = evidence_stat(evidence_fd(e, *entry));
            struct statx named{};
            if (object.parent) {
                const int parent = evidence_fd(e, *object.parent);
                if (!exists(parent, object.name)) fail(Cause::Substituted, "Evidence named object disappeared");
                named = evidence_stat(parent, object.name, false);
            } else {
                auto reopened = [&] {
                    try { return d::open_directory(object.name); }
                    catch (const std::system_error& error) {
                        // Descriptor adoption closes its raw fd before throwing.
                        // Unwinding traversal owners can change errno as well.
                        // Preserve the structured code rather than a later errno.
                        const int number = error.code().value();
                        throw PlanPersistenceError(number == ENOENT || number == ENOTDIR || number == ELOOP ?
                            Cause::Substituted : Cause::Io, outcome(), number, error.what(), std::current_exception());
                    }
                    catch (const std::exception& error) {
                        // Logical traversal errors have no syscall code. Global errno
                        // may describe a secondary close during unwinding.
                        throw PlanPersistenceError(Cause::Io, outcome(), 0, error.what(), std::current_exception());
                    }
                }();
                named = evidence_stat(reopened.get());
            }
            for (const auto& value : {held, named}) {
                if (!Evidence::same_identity(value, object.metadata) || value.stx_mode != object.metadata.stx_mode ||
                    value.stx_uid != object.metadata.stx_uid)
                    fail(Cause::Substituted, "Evidence named chain changed");
                if (S_ISREG(value.stx_mode) && !e.matches(*entry, value))
                    fail(Cause::Substituted, "Evidence document observation changed");
            }
        }
    }
    void evidence_foreign(Evidence& e) const {
        if (state) p.state(*state);
        std::set<Evidence::ObjectId> parents;
        for (const auto& foreign : e.foreign) {
            const auto parent = e.objects.at(foreign.container).parent;
            if (!parent) throw std::logic_error("Foreign evidence lacks parent");
            parents.insert(*parent);
        }
        for (const auto parent : parents) evidence_chain(e, parent);
        for (auto& foreign : e.foreign) {
            if (e.target && foreign.container == e.target->container) continue;
            const auto& anchor = e.objects.at(foreign.container);
            if (!anchor.parent) throw std::logic_error("Foreign evidence lacks parent");
            const int parent = evidence_fd(e, *anchor.parent);
            auto current = open(parent, anchor.name, false, Cause::Substituted, true);
            if (foreign.published) {
                if (!e.matches(foreign.container, current.initial))
                    fail(Cause::Substituted, "Foreign container anchor changed");
            } else {
                // Preparation containers are intentionally opaque. Keep the
                // same pinned directory instance and its access boundary, but
                // do not treat changes to its uninspected contents as a new
                // anchor (directory size/timestamps/link count can change).
                const auto& original = anchor.metadata;
                if (!Evidence::same_identity(original, current.initial) ||
                    original.stx_mode != current.initial.stx_mode ||
                    original.stx_uid != current.initial.stx_uid)
                    fail(Cause::Substituted, "Foreign container anchor changed");
            }
            if (foreign.published) {
                if (!foreign.interior) throw std::logic_error("Foreign published evidence lacks inventory");
                auto batch = evidence_inventory(e, current.fd.get(), foreign.container, 2, [&](const auto& name) {
                    const auto id = e.find(foreign.container, name);
                    if (!id) fail(Cause::Substituted, "Foreign inventory entry appeared");
                    auto file = open(current.fd.get(), name, true, Cause::Substituted, true);
                    if (!e.matches(*id, file.initial)) fail(Cause::Substituted, "Foreign document instance changed");
                    return *id;
                });
                batch.phase = foreign.interior->phase;
                if (!e.same_inventory(*foreign.interior, batch)) fail(Cause::Substituted, "Foreign inventory changed");
                check(current, true);
                // Compare before retaining a new batch; never overwrite the original.
                foreign.rechecked = std::move(batch);
            } else check(current, true); // Private contents remain opaque.
        }
        for (const auto parent : parents) evidence_chain(e, parent);
    }
    void evidence_barrier(Evidence& e, Evidence::Phase evidence_phase, Evidence::ObjectId id, const std::function<void()>& validate = {}) {
        switch (evidence_phase) {
        case Evidence::Phase::Bootstrap: phase = Phase::Bootstrap; break;
        case Evidence::Phase::Preparation: phase = Phase::Preparation; break;
        case Evidence::Phase::Publication: phase = Phase::Publication; break;
        case Evidence::Phase::Recovery: phase = Phase::Recovery; break;
        case Evidence::Phase::Inspection: throw std::logic_error("Inspection has no durability barriers");
        }
        // A supplied validator checks State after barrier() arms uncertainty.
        if (state && !validate) p.state(*state);
        const auto instance = barrier(evidence_fd(e, id), [&] { if (validate) validate(); else evidence_chain(e, id); }, true);
        // Only a real fsync whose instance checks completed may produce evidence.
        e.record_barrier(evidence_phase, id, instance);
    }
    void evidence_transition(Evidence& e, Evidence::TransitionKind kind, Evidence::Phase evidence_phase,
        Evidence::ObjectId id, Evidence::ObjectId destination, std::string name) {
        if (state) p.state(*state);
        evidence_chain(e, destination);
        const auto instance = evidence_stat(evidence_fd(e, id));
        const auto named = evidence_stat(evidence_fd(e, destination), name, false);
        metadata(instance, false); metadata(named, false);
        if (!Evidence::same_identity(instance, named) || !Evidence::same_identity(instance, e.objects.at(id).metadata))
            fail(Cause::Substituted, "Authorized transition names another instance");
        e.record_transition(kind, evidence_phase, id, destination, std::move(name), instance);
    }
    Pair pair(int parent, const ActivationId& id, bool private_pair, bool validate_context = true) const {
        auto container = open(parent, id.value(), false, private_pair ? Cause::PreparationConflict : Cause::MissingPlan);
        exact_pair(container.fd.get(), private_pair ? Cause::PreparationConflict : Cause::NamespaceIncomplete);
        auto intent = open(container.fd.get(), "intent.toml", true), plan = open(container.fd.get(), "plan.toml", true);
        auto ib = read(intent), pb = read(plan);
        // Parse without expected ID first so a canonical but wrong requested ID
        // is IdentityMismatch, distinct from a D1 syntax/schema failure.
        auto i = parse_activation_intent(ib); auto a = parse_activation_plan(pb);
        auto digest = digest_plan_documents(ib, pb);
        if (i.activation_id != id || a.activation_id != id) fail(Cause::IdentityMismatch, "D4 container/document ID mismatch");
        bound(i);
        check(container); if (validate_context) context();
        return {std::move(container), std::move(intent), std::move(plan), std::move(ib), std::move(pb),
            {std::move(i), std::move(a), std::move(digest)}};
    }
    PlanReferences retention() const {
        context();
        const auto admin = directory_observation(p.admin_.get());
        if (exists(p.admin_.get(), ".plans-bootstrap"))
            fail(Cause::BootstrapConflict, "D5 cannot dispose of bootstrap residue");
        if (!exists(p.admin_.get(), "plans")) {
            context();
            if (!observation(admin, directory_observation(p.admin_.get())))
                fail(Cause::Substituted, "D5 administration changed");
            return {};
        }
        auto ns = namespace_open(true);
        const auto plans_before = directory_observation(ns.plans.fd.get());
        const auto prep_before = directory_observation(ns.preparations.fd.get());
        std::set<std::string> ids;
        auto reserve = [&](const std::string& name) {
            id_name(name);
            if (ids.size() == capacity) fail(Cause::AdministrativeLimit, "D5 combined container budget");
            if (!ids.insert(name).second) fail(Cause::PreparationConflict, "D5 duplicate container ID");
        };
        const auto public_names = entries(ns.plans.fd.get(), capacity + 1, [&](const auto& name) {
            if (name != ".preparations") reserve(name);
        });
        const auto private_names = entries(ns.preparations.fd.get(), capacity, reserve);
        struct ObservedPair { std::string name; bool private_pair; struct statx container, intent, plan; PlanDigest digest; };
        std::vector<ObservedPair> observed;
        PlanReferences result;
        std::size_t bytes = 0, references = 0;
        auto budget = [&](std::size_t amount, std::size_t& used, std::size_t limit) {
            if (amount > limit - used) fail(Cause::AdministrativeLimit, "D5 retention scan budget exceeded");
            used += amount;
        };
        auto legacy_root = [&](const std::string& locator) {
            // D1 already validates raw absolute locator grammar. Do not resolve
            // aliases or normalize away traversal; retention is not source use.
            const auto relative = std::filesystem::path(locator).lexically_relative(p.binding_.store_authority);
            if (relative.empty() || relative == "." || relative.is_absolute() || *relative.begin() == "..")
                fail(Cause::AuthorityMismatch, "D5 legacy source outside bound store");
            result.legacy.insert((std::filesystem::path(p.binding_.store_authority) / *relative.begin()).native());
        };
        auto consume = [&](const std::string& name, bool private_pair) {
            auto value = pair(private_pair ? ns.preparations.fd.get() : ns.plans.fd.get(), ActivationId(name), private_pair, false);
            budget(value.intent_bytes.size(), bytes, max_retention_bytes);
            budget(value.plan_bytes.size(), bytes, max_retention_bytes);
            const auto& intent = value.document.intent;
            budget(1 + (std::holds_alternative<SelectedBase>(intent.base) ? 1 : 0) + value.document.plan.sources.size(),
                   references, max_retention_references);
            result.generations.insert(intent.target_generation);
            if (const auto* base = std::get_if<SelectedBase>(&intent.base)) result.generations.insert(selected_generation(base->current));
            for (const auto& source : value.document.plan.sources) std::visit([&](const auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (!std::is_same_v<T, ArtifactSource>) legacy_root(s.locator);
                if constexpr (!std::is_same_v<T, LegacyTreeSource>) result.artifacts.insert(s.artifact_id.value());
            }, source);
            if (read(value.intent) != value.intent_bytes || read(value.plan) != value.plan_bytes)
                fail(Cause::Substituted, "D5 pair changed during scan");
            check(value.container, true); check(value.intent, true); check(value.plan, true);
            observed.push_back({name, private_pair, value.container.initial, value.intent.initial, value.plan.initial, value.document.digest});
            ++(private_pair ? result.preparations : result.published);
        };
        for (const auto& name : public_names) if (name != ".preparations") consume(name, false);
        for (const auto& name : private_names) consume(name, true);
        // Value observations bound descriptors independently of plan count. Reopen
        // each pair at conclusion: same-byte replacement cannot borrow old evidence.
        for (const auto& original : observed) {
            auto value = pair(original.private_pair ? ns.preparations.fd.get() : ns.plans.fd.get(),
                              ActivationId(original.name), original.private_pair, false);
            if (!observation(original.container, value.container.initial) || !observation(original.intent, value.intent.initial) ||
                !observation(original.plan, value.plan.initial) || original.digest != value.document.digest)
                fail(Cause::Substituted, "D5 retained pair substituted");
        }
        context(); check(ns.plans, true); check(ns.preparations, true);
        if (!observation(admin, directory_observation(p.admin_.get())) ||
            !observation(plans_before, directory_observation(ns.plans.fd.get())) ||
            !observation(prep_before, directory_observation(ns.preparations.fd.get())))
            fail(Cause::Substituted, "D5 retention namespace changed");
        return result;
    }
    std::string evidence_read(const Evidence& e, Evidence::ObjectId id) const {
        evidence_chain(e, id);
        const int fd = evidence_fd(e, id); const auto before = evidence_stat(fd);
        std::string bytes; std::array<char, 16384> buffer{};
        for (;;) {
            const auto n = p.io_.pread(fd, buffer.data(), buffer.size(), static_cast<off_t>(bytes.size()));
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) fail(Cause::Io, "Read inspection evidence", errno);
            if (static_cast<std::size_t>(n) > buffer.size()) fail(Cause::Io, "Invalid inspection read count");
            if (n == 0) break;
            if (static_cast<std::size_t>(n) > max_document_size - bytes.size()) throw SchemaError(SchemaErrorCode::Bounds, "D1 inspection read bound");
            bytes.append(buffer.data(), static_cast<std::size_t>(n));
        }
        if (!Evidence::same_observation(before, evidence_stat(fd)) || bytes.size() != before.stx_size)
            fail(Cause::Substituted, "Inspection document EOF/observations changed");
        evidence_chain(e, id); return bytes;
    }
    void evidence_bindings(const Evidence& e, bool contents) const {
        for (const auto& binding : e.bindings) {
            const auto& anchor = e.objects.at(binding.object);
            if (!anchor.parent) throw std::logic_error("Binding evidence lacks parent");
            evidence_chain(e, *anchor.parent);
            const int parent = evidence_fd(e, *anchor.parent);
            if (!exists(parent, anchor.name)) fail(Cause::AuthorityMismatch, "Inspection binding disappeared");
            auto check = [&] {
                for (const auto& current : {evidence_stat(parent, anchor.name, false), evidence_stat(evidence_fd(e, binding.object))})
                    if (!e.matches(binding.object, current)) fail(Cause::AuthorityMismatch, "Inspection binding observation changed");
            };
            check();
            if (contents && (p.record(parent, anchor.name) != binding.canonical_bytes || binding.canonical_bytes != serialize(p.binding_)))
                fail(Cause::AuthorityMismatch, "Inspection actual named binding bytes changed");
            check();
        }
    }
    void conclude_inspection(const ActivationId& id) {
        auto& e = evidence;
        if (!e.target) fail(Cause::MissingPlan, "No published inspection target");
        auto& target = *e.target;
        const auto& container = e.objects.at(target.container);
        if (!container.parent || e.objects.at(*container.parent).name != "plans") fail(Cause::MissingPlan, "Inspection cannot use private preparation");
        // One coordinated conclusion: target rereads, namespace batch, actual
        // bindings, foreign structural reopens, then named and directory checks.
        const auto ib = evidence_read(e, target.intent), pb = evidence_read(e, target.plan);
        if (ib != target.intent_bytes || pb != target.plan_bytes) fail(Cause::Substituted, "Inspection target bytes changed");
        if (digest_plan_documents(ib, pb) != target.document.digest) fail(Cause::IdentityMismatch, "Inspection current digest mismatch");
        bound(parse_activation_intent(ib));
        for (const auto& original : e.inventories) {
            auto batch = evidence_inventory(e, evidence_fd(e, original.directory), original.directory,
                original.directory == target.container ? 2 : e.objects.at(original.directory).name == "plans" ? capacity + 1 :
                e.objects.at(original.directory).name == ".athena-activation" ? 6 : capacity,
                [&](const auto& name) {
                    const auto object = e.find(original.directory, name);
                    if (!object) fail(Cause::Substituted, "Inspection inventory entry appeared");
                    const auto current = evidence_stat(evidence_fd(e, original.directory), name, false);
                    const bool binding = std::any_of(e.bindings.begin(), e.bindings.end(), [&](const auto& b) { return b.object == *object; });
                    if (!e.matches(*object, current)) fail(binding ? Cause::AuthorityMismatch : Cause::Substituted, "Inspection named inventory changed");
                    return *object;
                });
            if (!e.same_inventory(original, batch)) fail(Cause::Substituted, "Inspection namespace inventory changed");
        }
        evidence_bindings(e, true);
        evidence_foreign(e);
        evidence_bindings(e, false); // Reads/reopens have callbacks; preserve both original records.
        for (Evidence::ObjectId object = 0; object < e.objects.size(); ++object) {
            const auto& anchor = e.objects[object];
            if (!anchor.pin) continue;
            if (std::any_of(e.bindings.begin(), e.bindings.end(), [&](const auto& b) { return b.object == object; })) continue;
            evidence_chain(e, object);
            if (S_ISDIR(anchor.metadata.stx_mode) && !e.matches(object, evidence_stat(evidence_fd(e, object))))
                fail(Cause::Substituted, "Inspection retained directory changed");
        }
        if (target.document.intent.activation_id != id || target.document.plan.activation_id != id)
            fail(Cause::IdentityMismatch, "Inspection final ID mismatch");
    }
    enum class Acquisition { General, PublishedRecovery };
    void acquire_evidence(const std::optional<ActivationId>& target, bool inspection = false, bool bootstrap_operation = false,
                          Acquisition acquisition = Acquisition::General) {
        const bool recovery = acquisition == Acquisition::PublishedRecovery;
        const bool published_only = inspection || recovery;
        preflight();
        using E = plan_detail::OperationEvidence;
        if (published_only && !exists(p.admin_.get(), "plans")) fail(Cause::MissingPlan, "No published inspection namespace");
        auto& evidence = this->evidence;
        auto pin = [&](int fd) {
            const int copy = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
            if (copy < 0) fail(Cause::Io, "Pin evidence authority", errno);
            return d::Fd::adopt(copy, p.io_.durable);
        };
        const auto generation = evidence.add({}, p.binding_.generation_authority, evidence_stat(p.generation_.get()), pin(p.generation_.get()));
        evidence.add({}, p.binding_.store_authority, evidence_stat(p.store_.get()), pin(p.store_.get()));
        const auto root = evidence.add({}, p.binding_.projection_root, evidence_stat(p.root_.get()), pin(p.root_.get()));
        const auto admin = evidence.add(root, ".athena-activation", evidence_stat(p.admin_.get()), pin(p.admin_.get()));
        auto record = [&](int parent, E::ObjectId parent_id, const char* name) {
            auto node = binding_record(parent, name);
            auto bytes = p.record(parent, name); check_binding(node);
            if (bytes != serialize(p.binding_)) fail(Cause::AuthorityMismatch, "Evidence binding changed");
            const auto id = evidence.add(parent_id, name, node.initial, std::move(node.fd));
            evidence.bindings.push_back({id, std::move(bytes)});
        };
        record(p.generation_.get(), generation, ".athena-projection-authority.toml");
        record(p.admin_.get(), admin, "authority.toml");
        // All enumeration descriptions and foreign child descriptors are scratch.
        // Objects are stored by stable indexes; no foreign fd survives this loop.
        auto inventory = [&](int fd, E::ObjectId id, std::size_t limit, const auto& visit) {
            auto batch = evidence_inventory(evidence, fd, id, limit, visit);
            if (recovery) batch.phase = E::Phase::Recovery;
            return batch;
        };
        if (published_only || bootstrap_operation) {
            auto batch = inventory(evidence_fd(evidence, admin), admin, 6, [&](const auto& name) {
                if (auto id = evidence.find(admin, name)) return *id;
                auto value = evidence_stat(evidence_fd(evidence, admin), name, false);
                metadata(value, false);
                return evidence_object(evidence, admin, name, value);
            });
            if (bootstrap_operation) batch.phase = E::Phase::Bootstrap;
            evidence.inventories.push_back(std::move(batch));
        }
        if (!exists(p.admin_.get(), "plans")) {
            if (exists(p.admin_.get(), ".plans-bootstrap")) {
                auto boot = open(p.admin_.get(), ".plans-bootstrap", false, Cause::NamespaceIncomplete, bootstrap_operation);
                evidence_object(evidence, admin, boot.name, boot.initial);
                const auto b = evidence.add(admin, boot.name, boot.initial, std::move(boot.fd));
                const int fd = evidence.pins[*evidence.objects[b].pin].get();
                auto batch = inventory(fd, b, 1, [&](const auto& name) {
                    if (name != ".preparations") fail(Cause::BootstrapConflict, "Evidence bootstrap child");
                    auto child = open(fd, name, false, Cause::NamespaceIncomplete, bootstrap_operation);
                    const auto c = evidence.add(b, name, child.initial, std::move(child.fd));
                    auto empty = inventory(evidence.pins[*evidence.objects[c].pin].get(), c, 0,
                        [&](const auto&) -> E::ObjectId { fail(Cause::BootstrapConflict, "Nonempty bootstrap evidence"); });
                    empty.phase = E::Phase::Bootstrap; evidence.inventories.push_back(std::move(empty)); return c;
                });
                batch.phase = E::Phase::Bootstrap; evidence.inventories.push_back(std::move(batch));
            }
            return;
        }
        E::ObjectId plans, prep;
        if (recovery) {
            // Recovery acquires directly into its sole owner; no parallel Namespace.
            if (exists(p.admin_.get(), ".plans-bootstrap")) fail(Cause::BootstrapConflict, "Ambiguous bootstrap during recovery");
            auto node = open(evidence_fd(evidence, admin), "plans", false, Cause::NamespaceIncomplete, true);
            evidence_object(evidence, admin, "plans", node.initial);
            plans = evidence.add(admin, "plans", node.initial, std::move(node.fd));
            auto child = open(evidence_fd(evidence, plans), ".preparations", false, Cause::NamespaceIncomplete, true);
            prep = evidence.add(plans, ".preparations", child.initial, std::move(child.fd));
        } else {
            auto ns = namespace_open(published_only || bootstrap_operation);
            evidence_object(evidence, admin, "plans", ns.plans.initial);
            plans = evidence.add(admin, "plans", ns.plans.initial, std::move(ns.plans.fd));
            prep = evidence.add(plans, ".preparations", ns.preparations.initial, std::move(ns.preparations.fd));
        }
        const int plans_fd = evidence.pins[*evidence.objects[plans].pin].get();
        const int prep_fd = evidence.pins[*evidence.objects[prep].pin].get();
        std::set<std::string> ids;
        auto container = [&](int parent, E::ObjectId parent_id, const std::string& name, bool published) {
            id_name(name);
            if (ids.size() == capacity) fail(Cause::AdministrativeLimit, "Evidence combined capacity");
            if (!ids.insert(name).second) fail(Cause::PreparationConflict, "Duplicate evidence container");
            if (target && target->value() == name && (!published_only || published)) {
                if (published_only) {
                    auto node = open(parent, name, false, Cause::MissingPlan, true);
                    const auto c = evidence.add(parent_id, name, node.initial, std::move(node.fd));
                    const int fd = evidence_fd(evidence, c);
                    auto batch = inventory(fd, c, 2, [&](const auto& leaf) {
                        if (leaf != "intent.toml" && leaf != "plan.toml") fail(Cause::NamespaceIncomplete, "Inspection target shape");
                        auto file = open(fd, leaf, true, Cause::NamespaceIncomplete, true);
                        return evidence.add(c, leaf, file.initial, std::move(file.fd));
                    });
                    if (batch.entries.size() != 2) fail(Cause::NamespaceIncomplete, "Incomplete inspection pair");
                    evidence.inventories.push_back(std::move(batch));
                    const auto i = *evidence.find(c, "intent.toml"), a = *evidence.find(c, "plan.toml");
                    auto ib = evidence_read(evidence, i), pb = evidence_read(evidence, a);
                    auto intent = parse_activation_intent(ib); auto plan = parse_activation_plan(pb);
                    auto digest = digest_plan_documents(ib, pb);
                    if (intent.activation_id != *target || plan.activation_id != *target) fail(Cause::IdentityMismatch, "Inspection document/container ID mismatch");
                    bound(intent);
                    evidence.target.emplace(E::Target{c, i, a, std::move(ib), std::move(pb),
                        {std::move(intent), std::move(plan), std::move(digest)}, {}});
                    return c;
                }
                auto pair = this->pair(parent, *target, !published, false);
                const auto c = evidence.add(parent_id, name, pair.container.initial, std::move(pair.container.fd));
                const auto i = evidence.add(c, "intent.toml", pair.intent.initial, std::move(pair.intent.fd));
                const auto a = evidence.add(c, "plan.toml", pair.plan.initial, std::move(pair.plan.fd));
                evidence.target.emplace(E::Target{c, i, a, std::move(pair.intent_bytes), std::move(pair.plan_bytes), std::move(pair.document), {}});
                return c;
            }
            auto node = open(parent, name, false, Cause::NamespaceIncomplete, true);
            const auto c = evidence_object(evidence, parent_id, name, node.initial);
            E::ForeignContainer foreign{c, published, {}, {}};
            if (published) {
                foreign.interior = inventory(node.fd.get(), c, 2, [&](const auto& leaf) {
                    if (leaf != "intent.toml" && leaf != "plan.toml") fail(Cause::NamespaceIncomplete, "Foreign evidence shape");
                    auto file = open(node.fd.get(), leaf, true, Cause::NamespaceIncomplete, true);
                    return evidence_object(evidence, c, leaf, file.initial);
                });
                if (bootstrap_operation) foreign.interior->phase = E::Phase::Bootstrap;
                if (foreign.interior->entries.size() != 2) fail(Cause::NamespaceIncomplete, "Incomplete foreign evidence pair");
            }
            check(node, true); evidence.foreign.push_back(std::move(foreign)); return c;
        };
        auto public_batch = inventory(plans_fd, plans, capacity + 1, [&](const auto& name) {
            return name == ".preparations" ? prep : container(plans_fd, plans, name, true);
        });
        auto private_batch = inventory(prep_fd, prep, capacity, [&](const auto& name) { return container(prep_fd, prep, name, false); });
        if (bootstrap_operation) public_batch.phase = private_batch.phase = E::Phase::Bootstrap;
        evidence.inventories.push_back(std::move(public_batch)); evidence.inventories.push_back(std::move(private_batch));
        return;
    }
    const struct statx& expected_observation(Evidence::ObjectId id) const {
        const auto found = own_observations.find(id);
        return found == own_observations.end() ? evidence.objects.at(id).metadata : found->second;
    }
    void own_directory_changed(Evidence::ObjectId id) {
        evidence_chain(evidence, id);
        const auto value = evidence_stat(evidence_fd(evidence, id)); metadata(value, false);
        if (!Evidence::same_identity(value, evidence.objects.at(id).metadata))
            fail(Cause::Substituted, "Own publication transition replaced anchor");
        own_observations[id] = value;
    }
    Evidence::Inventory publication_inventory(Evidence::ObjectId directory, Evidence::Phase stage) {
        auto& e = evidence;
        const auto& object = e.objects.at(directory);
        const auto limit = object.name == ".athena-activation" ? 6u : object.name == ".plans-bootstrap" ? 1u : object.name == "plans" ? capacity + 1 :
            e.target && directory == e.target->container ? 2u : capacity;
        auto batch = evidence_inventory(e, evidence_fd(e, directory), directory, limit, [&](const auto& name) {
            const auto id = e.find(directory, name);
            if (!id) fail(Cause::Substituted, "Publication inventory acquired an unexpected entry");
            const auto value = evidence_stat(evidence_fd(e, directory), name, false);
            const bool binding = std::any_of(e.bindings.begin(), e.bindings.end(), [&](const auto& b) { return b.object == *id; });
            if (!Evidence::same_observation(expected_observation(*id), value))
                fail(binding ? Cause::AuthorityMismatch : Cause::Substituted, "Publication named inventory changed");
            return *id;
        });
        batch.phase = stage;
        if (batch.entries.size() != static_cast<std::size_t>(std::count_if(e.objects.begin(), e.objects.end(),
            [&](const auto& child) { return child.parent == directory; })))
            fail(Cause::Substituted, "Publication inventory lost an entry");
        for (auto& entry : batch.entries) entry.observation.metadata = expected_observation(entry.object);
        if (!Evidence::same_observation(expected_observation(directory), batch.before))
            fail(Cause::Substituted, "Publication directory observation changed");
        return batch;
    }
    void advance_publication_inventories(Evidence::Phase stage) {
        auto& e = evidence;
        const auto root = *e.find({}, p.binding_.projection_root), admin = *e.find(root, ".athena-activation");
        const auto plans = *e.find(admin, "plans"), prep = *e.find(plans, ".preparations");
        phase_inventories.clear();
        for (auto id : {admin, plans, prep, e.target->container}) {
            auto batch = publication_inventory(id, stage);
            phase_inventories.push_back(e.inventories.size()); e.inventories.push_back(std::move(batch));
        }
    }
    void publication_named_checks() const {
        const auto& e = evidence;
        if (state) p.state(*state);
        evidence_bindings(e, false);
        for (Evidence::ObjectId id = 0; id < e.objects.size(); ++id) {
            const auto& object = e.objects[id];
            if (!object.pin || std::any_of(e.bindings.begin(), e.bindings.end(), [&](const auto& b) { return b.object == id; })) continue;
            evidence_chain(e, id);
            if (!Evidence::same_observation(expected_observation(id), evidence_stat(evidence_fd(e, id))))
                fail(Cause::Substituted, "Publication retained instance changed");
        }
    }
    void validate_publication() {
        auto& e = evidence; auto& target = *e.target;
        publication_named_checks();
        const auto ib = evidence_read(e, target.intent), pb = evidence_read(e, target.plan);
        if (ib != target.intent_bytes || pb != target.plan_bytes) fail(Cause::Substituted, "Publication target bytes changed");
        const auto digest = digest_plan_documents(ib, pb);
        if (digest != target.document.digest) fail(Cause::IdentityMismatch, "Publication target digest changed");
        if (target.independent_expected && digest != target.independent_expected->digest)
            fail(Cause::IdentityMismatch, "Independent concluding target digest mismatch");
        bound(parse_activation_intent(ib));
        for (const auto index : phase_inventories) {
            const auto& original = e.inventories.at(index);
            if (!e.same_inventory(original, publication_inventory(original.directory, original.phase)))
                fail(Cause::Substituted, "Publication concluding inventory changed");
        }
        evidence_bindings(e, true); evidence_foreign(e); publication_named_checks();
    }
    void verify_barrier_sequence(std::size_t first, Evidence::Phase stage,
                                 std::initializer_list<Evidence::ObjectId> objects) const {
        if (evidence.barriers.size() != first + objects.size()) throw std::logic_error("Incomplete publication barrier registry");
        for (const auto object : objects) {
            const auto& recorded = evidence.barriers.at(first++);
            if (recorded.phase != stage || recorded.object != object ||
                !Evidence::same_identity(recorded.instance, evidence.objects.at(object).metadata))
                throw std::logic_error("Publication barrier belongs to another phase/instance");
        }
    }
    void publication_barrier_checks() const {
        const auto& e = evidence; const auto& target = *e.target;
        publication_named_checks();
        if (evidence_read(e, target.intent) != target.intent_bytes || evidence_read(e, target.plan) != target.plan_bytes)
            fail(Cause::Substituted, "Publication barrier target bytes changed");
        evidence_bindings(e, true); publication_named_checks();
        // Full inventories and sequential foreign reopens conclude each phase;
        // their retained observations are not discarded by these instance checks.
    }
    Evidence::ObjectId publication_file(Evidence::ObjectId container, const char* name, std::string_view bytes) {
        const int parent = evidence_fd(evidence, container);
        open_how how{}; how.flags = O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
        how.mode = 0600; how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV;
        const int raw = p.io_.open(parent, name, how);
        if (raw < 0) {
            const int number = errno;
            try { if (exists(parent, name)) arm(); } catch (...) { arm(); throw; }
            fail(number == ENOSYS ? Cause::UnsupportedFilesystem : Cause::Io, "Exclusive D4 document creation", number);
        }
        mutation(); auto fd = d::Fd::adopt(raw, p.io_.durable);
        const auto before = evidence_stat(fd.get()); metadata(before, true);
        if (!Evidence::same_identity(before, evidence_stat(parent, name, false))) fail(Cause::Substituted, "Created D4 file substituted");
        d::complete_write(fd.get(), bytes, p.io_.durable);
        const auto after = evidence_stat(fd.get()); metadata(after, true);
        if (!Evidence::same_identity(before, after) || after.stx_size != bytes.size()) fail(Cause::Substituted, "D4 write instance/size mismatch");
        const auto id = evidence.add(container, name, after, std::move(fd));
        own_directory_changed(container);
        evidence_barrier(evidence, Evidence::Phase::Preparation, id);
        return id;
    }
    PlanPersistenceResult publish(const ActivationIntent& intent, const ActivationPlan& plan) {
        const auto ib = serialize(intent), pb = serialize(plan);
        const auto digest = digest_plan_documents(ib, pb); bound(intent);
        const auto id = intent.activation_id;
        ensure(); // Keeps the same evidence, anchors, inventories and barrier registry.
        auto& e = evidence;
        const auto root = *e.find({}, p.binding_.projection_root), admin = *e.find(root, ".athena-activation");
        const auto plans = *e.find(admin, "plans"), prep = *e.find(plans, ".preparations");
        const auto existing_public = e.find(plans, id.value()), existing_private = e.find(prep, id.value());
        if (existing_public && existing_private) fail(Cause::PreparationConflict, "D4 activation in both locations");
        const bool published = existing_public.has_value(), prepared = existing_private.has_value();
        phase = published ? Phase::Recovery : Phase::Preparation;
        Evidence::ObjectId container, i, a;
        if (!published && !prepared) {
            if (e.foreign.size() >= capacity) fail(Cause::AdministrativeLimit, "No D4 container capacity before mkdir");
            auto node = mkdir(evidence_fd(e, prep), id.value(), Cause::PreparationConflict, true);
            container = e.add(prep, id.value(), node.initial, std::move(node.fd));
            own_directory_changed(prep);
            i = publication_file(container, "intent.toml", ib);
            a = publication_file(container, "plan.toml", pb);
        } else {
            try {
                const auto parent = published ? plans : prep;
                const auto original = published ? *existing_public : *existing_private;
                auto node = open(evidence_fd(e, parent), id.value(), false, Cause::PreparationConflict, true);
                if (!e.matches(original, node.initial)) fail(Cause::Substituted, "Publication target anchor changed");
                container = e.add(parent, id.value(), node.initial, std::move(node.fd));
                exact_pair(evidence_fd(e, container), published ? Cause::NamespaceIncomplete : Cause::PreparationConflict);
                auto acquire_file = [&](const char* name) {
                    auto file = open(evidence_fd(e, container), name, true, Cause::PreparationConflict, true);
                    if (auto original_file = e.find(container, name); original_file && !e.matches(*original_file, file.initial))
                        fail(Cause::Substituted, "Publication target document anchor changed");
                    return e.add(container, name, file.initial, std::move(file.fd));
                };
                i = acquire_file("intent.toml"); a = acquire_file("plan.toml");
                const auto actual_ib = evidence_read(e, i), actual_pb = evidence_read(e, a);
                // Preserve private reuse's schema/identity/binding classification.
                try {
                    auto actual_intent = parse_activation_intent(actual_ib); auto actual_plan = parse_activation_plan(actual_pb);
                    (void)digest_plan_documents(actual_ib, actual_pb);
                    if (actual_intent.activation_id != id || actual_plan.activation_id != id) fail(Cause::IdentityMismatch, "D4 target ID mismatch");
                    bound(actual_intent);
                } catch (const SchemaError& error) {
                    throw PlanPersistenceError(published ? Cause::InvalidDocument : Cause::PreparationConflict, outcome(), 0, error.what(), std::current_exception());
                } catch (const PlanPersistenceError& error) {
                    if (!published && (error.cause == Cause::IdentityMismatch || error.cause == Cause::AuthorityMismatch))
                        throw PlanPersistenceError(Cause::PreparationConflict, outcome(), error.error, error.what(), std::current_exception());
                    throw;
                }
                if (actual_ib != ib || actual_pb != pb) fail(published ? Cause::PublicationConflict : Cause::PreparationConflict, "Existing D4 bytes differ from request");
            } catch (const SchemaError& error) {
                // Bounds/read failures belong to the same reuse classification
                // as canonical parsing, before the common preparation barriers.
                throw PlanPersistenceError(published ? Cause::InvalidDocument : Cause::PreparationConflict, outcome(), 0, error.what(), std::current_exception());
            }
        }
        e.target = Evidence::Target{container, i, a, ib, pb, {intent, plan, digest}, PlanIdentity{id, digest}};
        advance_publication_inventories(published ? Evidence::Phase::Recovery : Evidence::Phase::Preparation);
        validate_publication();
        const auto stage = published ? Evidence::Phase::Recovery : Evidence::Phase::Preparation;
        const auto preparation_barriers = e.barriers.size();
        for (auto object : {i, a, container, published ? plans : prep, published ? prep : plans, admin, root})
            evidence_barrier(e, stage, object, [&] { publication_barrier_checks(); });
        validate_publication();
        verify_barrier_sequence(preparation_barriers, stage, {i, a, container, published ? plans : prep, published ? prep : plans, admin, root});
        complete();
        if (!published) {
            phase = Phase::Publication; arm(); validate_publication();
            if (p.io_.rename(evidence_fd(e, prep), id.value().c_str(), evidence_fd(e, plans), id.value().c_str(), RENAME_NOREPLACE) < 0)
                fail(errno == EEXIST ? Cause::PublicationConflict : errno == ENOSYS || errno == EINVAL || errno == EOPNOTSUPP ?
                    Cause::UnsupportedFilesystem : Cause::Io, "D4 NOREPLACE rename", errno);
            mutation();
            evidence_transition(e, Evidence::TransitionKind::PlanPublication, Evidence::Phase::Publication, container, plans, id.value());
            if (exists(evidence_fd(e, prep), id.value())) fail(Cause::Substituted, "Publication source retained after rename");
            own_directory_changed(container); own_directory_changed(prep); own_directory_changed(plans);
            advance_publication_inventories(Evidence::Phase::Publication);
            const auto publication_barriers = e.barriers.size();
            for (auto object : {container, plans, prep, admin, root})
                evidence_barrier(e, Evidence::Phase::Publication, object, [&] { publication_barrier_checks(); });
            validate_publication();
            verify_barrier_sequence(publication_barriers, Evidence::Phase::Publication, {container, plans, prep, admin, root});
            complete();
        }
        return {e.target->document, published ? PlanPublication::AlreadyPublished : PlanPublication::Published, outcome()};
    }

};

plan_detail::OperationEvidence PlanAccess::acquire_evidence(const ProjectionContext& p,
    const ProjectionState& state, const std::optional<ActivationId>& target) {
    Operation op{p, &state}; return op.run([&] {
        op.acquire_evidence(target); return std::move(op.evidence);
    });
}

// Internal evidence test boundaries; production inspection uses the same Operation.
void PlanAccess::compare_foreign(const ProjectionContext& p, const ProjectionState& state,
    plan_detail::OperationEvidence& evidence) {
    Operation op{p, &state}; op.run([&] { op.evidence_foreign(evidence); });
}
void PlanAccess::evidence_barrier(const ProjectionContext& p, const ProjectionState& state,
    plan_detail::OperationEvidence& evidence, plan_detail::OperationEvidence::Phase phase,
    plan_detail::OperationEvidence::ObjectId id) {
    Operation op{p, &state}; op.run([&] { op.evidence_barrier(evidence, phase, id); });
}
void PlanAccess::evidence_transition(const ProjectionContext& p, const ProjectionState& state,
    plan_detail::OperationEvidence& evidence, plan_detail::OperationEvidence::TransitionKind kind,
    plan_detail::OperationEvidence::Phase phase, plan_detail::OperationEvidence::ObjectId id,
    plan_detail::OperationEvidence::ObjectId destination, std::string name) {
    Operation op{p, &state}; op.run([&] { op.evidence_transition(evidence, kind, phase, id, destination, std::move(name)); });
}

std::vector<std::string> PlanAccess::administration_entries(const ProjectionContext& p) {
    Operation op{p}; return op.entries(p.admin_.get(), 6, [&](const auto& name) {
        if (name != "authority.toml" && name != "witnesses" && name != "work" &&
            name != "adoptions" && name != "plans" && name != ".plans-bootstrap")
            throw ProjectionError(ProjectionCause::ProjectionConflict, Outcome::NotMutated, 0, "Unexpected administration entries");
    });
}
void PlanAccess::namespace_shape(const ProjectionContext& p) {
    Operation op{p}; op.run([&] { op.shape(); });
}
PlanReferences PlanAccess::retention(const ProjectionContext& p) {
    Operation op{p}; return op.run([&] { return op.retention(); });
}
ProjectionOutcome PlanAccess::ensure(ProjectionContext& p, const ProjectionState& state) {
    Operation op{p, &state}; return op.run([&] { op.ensure(); return op.outcome(); });
}
InspectedActivationPlan PlanAccess::inspect(const ProjectionContext& p, const ProjectionState& state, const ActivationId& id) {
    Operation op{p, &state}; return op.run([&] {
        op.acquire_evidence(id, true);
        op.conclude_inspection(id);
        return std::move(op.evidence.target->document);
    });
}
PlanPersistenceResult PlanAccess::recover(ProjectionContext& p, const ProjectionState& state, const PlanIdentity& expected) {
    return recover_with_evidence(p, state, expected, nullptr);
}
PlanPersistenceResult PlanAccess::recover_with_evidence(ProjectionContext& p, const ProjectionState& state,
    const PlanIdentity& expected, plan_detail::OperationEvidence* observed) {
    if (observed && (!observed->objects.empty() || !observed->pins.empty()))
        throw std::logic_error("Recovery evidence output must be empty");
    Operation op{p, &state};
    try {
        auto result = op.run([&] {
            using E = plan_detail::OperationEvidence;
            op.phase = Operation::Phase::Recovery;
            op.acquire_evidence(expected.activation_id, false, false, Operation::Acquisition::PublishedRecovery);
            auto& e = op.evidence;
            if (!e.target) op.fail(Cause::MissingPlan, "No published recovery target");
            e.target->independent_expected = expected;
            if (e.target->document.digest != expected.digest)
                op.fail(Cause::IdentityMismatch, "Independent expected D4 digest mismatch");
            for (std::size_t n = 0; n < e.inventories.size(); ++n) op.phase_inventories.push_back(n);
            auto validate = [&] {
                op.validate_publication();
                if (e.target->document.intent.activation_id != expected.activation_id ||
                    e.target->document.plan.activation_id != expected.activation_id ||
                    e.target->document.digest != e.target->independent_expected->digest)
                    op.fail(Cause::IdentityMismatch, "Independent final recovery identity mismatch");
            };
            const auto root = *e.find({}, p.binding_.projection_root), admin = *e.find(root, ".athena-activation");
            const auto plans = *e.find(admin, "plans"), prep = *e.find(plans, ".preparations");
            const auto container = e.target->container, intent = e.target->intent, plan = e.target->plan;
            validate();
            for (auto id : {intent, plan, container, plans, prep, admin, root})
                op.evidence_barrier(e, E::Phase::Recovery, id, [&] { op.publication_barrier_checks(); });
            validate();
            op.verify_barrier_sequence(0, E::Phase::Recovery, {intent, plan, container, plans, prep, admin, root});
            op.complete();
            return PlanPersistenceResult{e.target->document, PlanPublication::AlreadyPublished, op.outcome()};
        });
        if (observed) *observed = std::move(op.evidence);
        return result;
    } catch (...) {
        if (observed) *observed = std::move(op.evidence);
        throw;
    }
}
PlanPersistenceResult PlanAccess::publish(ProjectionContext& p, const ProjectionState& state,
    const ActivationIntent& intent, const ActivationPlan& plan) {
    return publish_with_evidence(p, state, intent, plan, nullptr);
}
PlanPersistenceResult PlanAccess::publish_with_evidence(ProjectionContext& p, const ProjectionState& state,
    const ActivationIntent& intent, const ActivationPlan& plan, plan_detail::OperationEvidence* observed) {
    if (observed && (!observed->objects.empty() || !observed->pins.empty()))
        throw std::logic_error("Publication evidence output must be empty");
    Operation op{p, &state};
    // Export by move only after the real invocation exits. No test callback,
    // borrowed references or duplicate ownership can escape Operation's lifetime.
    try {
        auto result = op.run([&] { return op.publish(intent, plan); });
        if (observed) *observed = std::move(op.evidence);
        return result;
    } catch (...) {
        if (observed) *observed = std::move(op.evidence);
        throw;
    }
}
ProjectionOutcome ensure_plan_namespace(ProjectionContext& p, const ProjectionState& s) { return PlanAccess::ensure(p, s); }
InspectedActivationPlan inspect_activation_plan(const ProjectionContext& p, const ProjectionState& s, const ActivationId& id) { return PlanAccess::inspect(p, s, id); }
PlanPersistenceResult publish_activation_plan(ProjectionContext& p, const ProjectionState& s, const ActivationIntent& i, const ActivationPlan& a) { return PlanAccess::publish(p, s, i, a); }
PlanPersistenceResult recover_activation_plan(ProjectionContext& p, const ProjectionState& s, const PlanIdentity& e) { return PlanAccess::recover(p, s, e); }
}
