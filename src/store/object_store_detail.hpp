#pragma once
#include "../artifact/model.hpp"
#include <filesystem>

namespace athena::store::object_detail {
// Internal mechanics shared with focused tests. Neither establishes identity
// nor offers an authoritative publication API. Caller owns a private root.
void materialize(const std::filesystem::path& root, const artifact::NormalizedArtifact& model);
void finalize(const std::filesystem::path& root, const artifact::NormalizedArtifact& model);
// True means won; false means destination exists. Other errno values fail.
bool rename_no_replace(int source_parent, const char* source,
                       int destination_parent, const char* destination);
// Caller must own this private container. Returns secondary error detail.
std::string cleanup_private(const std::filesystem::path& container) noexcept;
}
