#pragma once
#include "model.hpp"
#include <athena/core/types.hpp>
namespace athena::artifact {
// Derives v1 identity from strictly validated, already sorted input. Does not
// prove filesystem inspection; callers requiring that evidence must scan first.
core::ArtifactId derive_artifact_id(const NormalizedArtifact& artifact);
}
