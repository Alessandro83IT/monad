// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "plan.hpp"
#include "projection_binding.hpp"
#include "../core/detail/filesystem.hpp"
#include "../store/object_store.hpp"
#include <athena/core/locking.hpp>
#include <functional>
#include <dirent.h>
#include <linux/openat2.h>
#include <linux/stat.h>
namespace athena::activation {
enum class ProjectionOutcome { NotMutated, MutatedDurably, OutcomeUncertain };
enum class ProjectionCause {
    BindingConflict, ProjectionConflict, OwnershipMismatch, ForeignEntry,
    MissingWitness, MissingManaged, UnsafeAncestor, MountCrossing,
    UnsupportedWitnessKind, DurabilityFailure, IoFailure
};
struct ProjectionError : std::runtime_error {
    ProjectionOutcome outcome;
    ProjectionCause cause;
    int error;
    ProjectionError(ProjectionCause, ProjectionOutcome, int, std::string_view);
};
// Owned callbacks: operation-local, no globals, no borrowed callback state.
// Callbacks obey errno semantics and the supported local filesystem contract.
struct ProjectionIo {
    core::detail::Syscalls durable = core::detail::linux_syscalls();
    std::function<int(int, const char*, const open_how&)> open;
    std::function<int(int, const char*, int, unsigned, struct statx*)> stat;
    std::function<int(const char*, int, const char*)> symlink;
    std::function<int(int, const char*, int, const char*, int)> link;
    std::function<ssize_t(int, const char*, char*, size_t)> readlink;
    std::function<int(int)> fsync;
    std::function<int(int, const char*, int, const char*, unsigned)> rename;
    std::function<int(int, const char*, mode_t)> mkdir;
    std::function<int(int, const char*, int)> unlink;
    // D4 bounded reads/enumerations, owned by this context. errno conventions;
    // callbacks never throw. Enumeration uses a fresh open-file description.
    std::function<ssize_t(int, void*, size_t, off_t)> pread;
    std::function<struct dirent*(DIR*)> readdir;
    ProjectionIo();
};
void validate_projection_path(std::string_view);
// Explicit existing hierarchy authority; acquire after TransactionLease and
// before StoreGc. Public manager methods must not reacquire this lock.
class ProjectionState {
public:
    explicit ProjectionState(std::string generation_authority);
    const std::string& generations() const { return generations_; }
    void revalidate() const;
private:
    friend class LegacyAccess;
    friend class ProjectionContext;
    std::string generations_;
    core::detail::Fd generation_;
    core::FileLock state_;
};
// Caller serializes Athena operations with State. Equivalent-authority external
// writers must be quiescent for mutation; this module acquires no locks.
class ProjectionContext {
public:
    static ProjectionContext open(const ProjectionBinding&, const store::ObjectStore&, ProjectionIo = {});
    // Explicit fresh-only initialization. No finish/recovery API in D2. Any
    // partial namespace/binding is recovery-required, never automatically adopted.
    static ProjectionContext create_binding(const ProjectionBinding&, const store::ObjectStore&, const ProjectionState&, ProjectionIo = {});
    ProjectionContext(ProjectionContext&&) noexcept = default;
    ProjectionContext& operator=(ProjectionContext&&) noexcept = default;
    const ProjectionBinding& binding() const { return binding_; }
    void revalidate() const;
    ProjectionOutcome create_symlink(const ProjectionState&, std::string_view path, std::string_view target, const WitnessReference&);
    void validate_owned_symlink(std::string_view path, std::string_view target, const WitnessReference&) const;
    ProjectionOutcome succeed_witness(const ProjectionState&, std::string_view path, std::string_view target,
        const WitnessReference& old_witness, const WitnessReference& successor);
    ProjectionOutcome remove_symlink(const ProjectionState&, std::string_view path, std::string_view target,
        const WitnessReference&);
    ProjectionOutcome replace_symlink(const ProjectionState&, std::string_view path,
        std::string_view old_target, const WitnessReference& old_witness,
        std::string_view new_target, const WitnessReference& new_witness);
    ProjectionOutcome ensure_directory(const ProjectionState&, std::string_view path);
    // Recognizes only an exact owned final link and repairs namespace barriers.
    ProjectionOutcome recover_symlink(const ProjectionState&, std::string_view path,
        std::string_view target, const WitnessReference&);
private:
    ProjectionContext(ProjectionBinding, ProjectionIo);
    friend class LegacyAccess;
    friend class PlanAccess;
    friend class ExecutionAccess;
    using Fd = core::detail::Fd;
    ProjectionBinding binding_;
    ProjectionIo io_;
    Fd generation_, store_, root_, admin_, witnesses_, work_;
    struct statx mount_{};
    void state(const ProjectionState&) const;
    void open_roots(const store::ObjectStore&);
    Fd directory(int, std::string_view, bool private_mode = false) const;
    struct statx identity(int, std::string_view = {}, bool empty = true) const;
    void same_mount(const struct statx&) const;
    void unchanged(int, std::string_view, int) const;
    void private_directory(int) const;
    std::string record(int, std::string_view) const;
    void namespace_shape(bool d4_errors = false) const;
    void revalidate_impl(bool d4_errors) const;
    std::pair<Fd, std::string> parent(std::string_view) const;
    Fd container(int, const WitnessReference&, bool create, bool* created = nullptr) const;
    std::string witness_name(const WitnessReference&) const;
    std::string literal(int, std::string_view) const;
    void link_identity(int, std::string_view, std::string_view, ProjectionCause) const;
    void barrier(int, bool mutated) const;
    Fd mkdir_private(int, std::string_view) const;
    ProjectionOutcome publish_symlink(const ProjectionState&, std::string_view, std::string_view,
        const WitnessReference&, const std::optional<std::pair<std::string, WitnessReference>>&);
};
}
