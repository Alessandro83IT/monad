// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../core/detail/filesystem.hpp"
#include <optional>
#include <cstdint>
#include <limits>
#include <vector>
namespace athena::generation::publication {
// The generation codec persists nonnegative signed 64-bit TOML integers.
inline constexpr std::uint64_t max_id = std::numeric_limits<std::int64_t>::max();
std::optional<std::uint64_t> canonical_id(std::string_view);
// Read-only, no-follow enumeration. Missing namespace is empty; other errors propagate.
std::vector<std::uint64_t> existing_ids(const std::filesystem::path&);
// Caller owns exclusive namespace-derived State lock. No implicit current selection.
// Collision is an error, never overwrite or reinterpret a generation.
void persist(const std::filesystem::path&, std::uint64_t, std::string_view,
             const core::detail::Syscalls& = core::detail::linux_syscalls());
}
