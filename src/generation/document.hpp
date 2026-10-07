// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "generation.hpp"
#include "../activation/identity.hpp"
#include <functional>
#include <unistd.h>

namespace athena::generation {
using GenerationDocumentDigest = activation::Sha256Digest<struct GenerationDocumentDigestTag>;
struct ValidatedGenerationDocument {
    Generation generation;
    std::string bytes;
    GenerationDocumentDigest digest;
};
// Validate and hash the same exact bytes, preserving historical codec provenance.
ValidatedGenerationDocument validate_generation_document(std::string_view, std::uint64_t expected_id);
// Read-only, bounded, no-follow, named-inode and mutation checks. No locks are acquired;
// the caller supplies State/lifetime serialization when reading live authority.
// Operation-local read seam, also allowing deterministic substitution tests.
using GenerationRead = std::function<ssize_t(int, void*, std::size_t)>;
ValidatedGenerationDocument read_generation_document(const std::filesystem::path& authority, std::uint64_t id,
    const GenerationRead& read = ::read);
}
