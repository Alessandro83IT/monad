#pragma once
#include "model.hpp"
namespace athena::artifact {
// Strict validation, no sorting or mutation. Output is binary, not text.
std::string canonical_bytes(const NormalizedArtifact& artifact);
namespace encoding_detail {
// Pure framing primitives shared by tree and identity framing.
std::uint64_t checked_length(std::size_t size);
std::uint64_t checked_add(std::uint64_t left, std::uint64_t right);
void append_u32(std::string& output, std::uint32_t value);
void append_u64(std::string& output, std::uint64_t value);
}
}
