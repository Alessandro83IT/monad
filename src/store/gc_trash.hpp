// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "transaction_pins.hpp"

namespace athena::store {
class GarbageCollector;
struct UnsafeGcTrashTestAccess;
// One claimed container; its rank-3 lease is acquired before authority locks.
class GcTrash {
public:
    static std::vector<std::string> containers(const std::filesystem::path&);
private:
    friend class GarbageCollector;
    friend struct UnsafeGcTrashTestAccess;
    GcTrash(const std::filesystem::path& source_namespace, bool modern,
            const core::detail::Syscalls& = core::detail::linux_syscalls());
    GcTrash(const std::filesystem::path& source_namespace, bool modern, std::string id);
    // Caller holds State -> StoreGc. Collision is always an error.
    void detach_locked(std::string_view source, const core::detail::Syscalls& = core::detail::linux_syscalls());
    // Restart re-establishes both parent barriers under authority locks before this call.
    void confirm_locked(const core::detail::Syscalls& = core::detail::linux_syscalls());
    void reclaim(const AuthorityContext&, const core::detail::Syscalls& = core::detail::linux_syscalls());
    const std::string& id() const { return id_; }
    void validate() const;
    std::filesystem::path source_;
    bool modern_, durable_ = false;
    std::string id_;
    core::detail::Fd source_fd_, root_, container_, lease_identity_;
    std::unique_ptr<core::FileLock> lease_;
};
}
