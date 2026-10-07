// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "object_store.hpp"
#include <athena/core/locking.hpp>
#include <memory>
#include <set>

namespace athena::generation { class GenerationManager; struct Generation; }
namespace athena::store {
namespace administrative {
inline constexpr std::size_t max_record_size = 16 * 1024 * 1024;
bool hex_name(std::string_view, std::size_t);
std::filesystem::path locator(const std::filesystem::path&);
std::vector<std::string> entries(int);
void private_directory(int);
void private_file(int);
void unchanged(int parent, std::string_view name, int fd);
std::string read_file(int parent, std::string_view name);
core::detail::Fd ensure_directory(int parent, std::string_view name,
    const core::detail::Syscalls& = core::detail::linux_syscalls());
std::string random_id(const core::detail::Syscalls& = core::detail::linux_syscalls());
void same_mount(int, int);
void rename_detach(int, std::string_view, int, std::string_view,
    const core::detail::Syscalls& = core::detail::linux_syscalls());
}
// Explicit binding to the exact generation namespace, never a state-parent guess.
class AuthorityContext {
public:
    AuthorityContext(std::filesystem::path store, std::filesystem::path generations);
    const std::filesystem::path& store() const { return store_; }
    const std::filesystem::path& generations() const { return generations_; }
    std::filesystem::path objects() const { return store_ / "objects/v1/sha256"; }
    std::filesystem::path registry() const { return objects() / ".athena-transactions"; }
    // Requires StoreGc. Creation is permitted only at explicit registration/publication boundaries.
    void validate_binding(bool create, const core::detail::Syscalls& = core::detail::linux_syscalls()) const;
private:
    std::filesystem::path store_, generations_;
};
// A typed proof of StoreGc ownership. It cannot be forged or copied.
class StoreGcLock {
public:
    explicit StoreGcLock(const std::filesystem::path& store)
        : store_(administrative::locator(store)), lock_(core::store_gc_lock_path(store_), core::LockKind::StoreGc) {}
    const std::filesystem::path& store() const { return store_; }
private:
    std::filesystem::path store_;
    core::FileLock lock_;
};
struct PinSnapshot { std::set<std::string> artifacts; bool transactions = false; };
// Caller holds StoreGc. Lease state is deliberately never probed.
PinSnapshot scan_pins(const AuthorityContext&);
std::string serialize_pins(std::string_view id, const std::set<std::string>&);
std::set<std::string> parse_pins(std::string_view document, std::string_view id);

class TransactionPins {
public:
    explicit TransactionPins(AuthorityContext, const core::detail::Syscalls& = core::detail::linux_syscalls());
    ~TransactionPins() = default; // NEVER retires durable state on exception/destruction.
    TransactionPins(const TransactionPins&) = delete;
    TransactionPins& operator=(const TransactionPins&) = delete;
    const AuthorityContext& authority() const { return authority_; }
    const std::string& id() const { return id_; }
    const std::set<std::string>& artifacts() const { return artifacts_; }
    void add(const core::ArtifactId&, const core::detail::Syscalls& = core::detail::linux_syscalls());
    // Internal callers already hold StoreGc; does not acquire any additional lock.
    void add_locked(const core::ArtifactId&, const StoreGcLock&, const core::detail::Syscalls& = core::detail::linux_syscalls());
    ObjectPublication publish(const ObjectStore&, class PreparedObject&,
        const core::detail::Syscalls& = core::detail::linux_syscalls());
    // State -> StoreGc, verifies durable persisted membership before retirement.
    void handoff(std::uint64_t generation, const core::detail::Syscalls& = core::detail::linux_syscalls());
    // Explicit abandonment only; no continuing recovery obligation is permitted.
    void abandon(const core::detail::Syscalls& = core::detail::linux_syscalls());
private:
    friend class generation::GenerationManager;
    void validate_live() const;
    void retire_locked(const core::detail::Syscalls&);
    AuthorityContext authority_;
    std::string id_;
    core::detail::Fd directory_, lease_identity_;
    std::unique_ptr<core::FileLock> lease_;
    std::set<std::string> artifacts_;
    bool stopped_ = false, retired_ = false;
};
}
