#pragma once
#include "../artifact/model.hpp"
#include <filesystem>
#include <linux/stat.h>

namespace athena::store::object_detail {
// Internal mechanics shared with focused tests; no authoritative publication
// or removal API. Materialization/finalization callers own a private root.
void materialize(const std::filesystem::path& root, const artifact::NormalizedArtifact& model);
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
