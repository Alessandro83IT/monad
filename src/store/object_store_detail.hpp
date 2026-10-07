// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "../artifact/model.hpp"
#include "../core/detail/filesystem.hpp"
#include <filesystem>
#include <linux/stat.h>

namespace athena::store::object_detail {
// Internal mechanics shared with focused tests; no authoritative publication
// or removal API. Materialization/finalization callers own a private root.
// Publication namespace setup shared with production; performs preflight and barriers.
core::detail::Fd publication_namespace(const std::filesystem::path&,
                                      const core::detail::Syscalls& = core::detail::linux_syscalls());
void materialize(const std::filesystem::path& root, const artifact::NormalizedArtifact& model,
                 const core::detail::Syscalls& = core::detail::linux_syscalls());
void synchronize(const std::filesystem::path&, const artifact::NormalizedArtifact&,
                 const core::detail::Syscalls& = core::detail::linux_syscalls());
void finalize(const std::filesystem::path& root, const artifact::NormalizedArtifact& model);
// True means won; false means destination exists. Other errno values fail.
bool rename_no_replace(int source_parent, const char* source,
                       int destination_parent, const char* destination);
// Internal GC ownership decision, also tested without mount privileges.
// Missing mount IDs and different mounts (including same-device bind mounts) fail closed.
void require_same_gc_mount(const struct statx& expected, const struct statx& actual);
// Caller must own this private container. Returns secondary error detail.
std::string cleanup_private(const std::filesystem::path& container) noexcept;
}
