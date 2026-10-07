// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Unsafe test-only access to private trash mechanics. Kept under tests/ so
// production sources and installed headers cannot call these operations.
#include "store/gc_trash.hpp"

namespace athena::store {
struct UnsafeGcTrashTestAccess {
    static GcTrash create(const std::filesystem::path& source, bool modern,
        const core::detail::Syscalls& ops = core::detail::linux_syscalls()) {
        return GcTrash(source, modern, ops);
    }
    static GcTrash open(const std::filesystem::path& source, bool modern, std::string id) {
        return GcTrash(source, modern, std::move(id));
    }
    static const std::string& id(const GcTrash& value) { return value.id(); }
    static void detach(GcTrash& value, std::string_view name,
        const core::detail::Syscalls& ops = core::detail::linux_syscalls()) { value.detach_locked(name, ops); }
    static void confirm(GcTrash& value,
        const core::detail::Syscalls& ops = core::detail::linux_syscalls()) { value.confirm_locked(ops); }
    static void reclaim(GcTrash& value, const AuthorityContext& authority,
        const core::detail::Syscalls& ops = core::detail::linux_syscalls()) { value.reclaim(authority, ops); }
};
}
