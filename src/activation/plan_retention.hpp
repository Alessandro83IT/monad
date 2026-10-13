// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "projection_fs.hpp"
#include <optional>
#include <set>

namespace athena::activation {
struct PlanReferences {
    std::set<std::uint64_t> generations;
    std::set<std::string> artifacts; // Full ArtifactId spelling.
    std::set<std::string> legacy; // Top-level entries under the bound store.
    std::size_t published = 0, preparations = 0;
};
inline constexpr std::size_t max_retention_bytes = 64 * 1024 * 1024;
inline constexpr std::size_t max_retention_references = 262144;
// Internal, read-only snapshot. The namespace-derived State lock must remain held
// through its consumer's decision/mutation. Shared State suffices for scanning;
// discard supplies exclusive State, GC also holds StoreGc. No locks acquired here.
// Missing binding leaf alone means no registered projection. All other failures
// propagate, never becoming empty references. No fsync/execution/cleanup authority.
PlanReferences scan_plan_references(const std::filesystem::path& generations,
    const core::FileLock& state, const std::optional<std::filesystem::path>& expected_store = {},
    ProjectionIo io = {});
}
