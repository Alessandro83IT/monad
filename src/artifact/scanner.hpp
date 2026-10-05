#pragma once

#include "model.hpp"
#include <filesystem>

namespace athena::artifact {

// Read-only Linux scanner. The caller supplies a private, quiescent staging
// tree. No symlink component of the root locator is followed. Observed changes
// fail; this is not an atomic filesystem snapshot against hostile writers.
// Metadata inspection must be complete, including normally hidden trusted.*
// xattrs. Missing inspection privileges/support fail closed.
NormalizedArtifact scan(const std::filesystem::path& root);

} // namespace athena::artifact
