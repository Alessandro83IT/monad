// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "artifact/encoding.hpp"
#include "artifact_golden_vectors.hpp"
#include "artifact_test_support.hpp"
#include <limits>
using namespace athena::artifact;
int main()
{
    for (const auto& vector : golden_vectors) {
        const auto before = vector.model;
        const auto bytes = canonical_bytes(vector.model);
        require(bytes == unhex(vector.canonical_hex), vector.name);
        require(bytes.size() == vector.length, "canonical length mismatch");
        require(before == vector.model, "encoding mutated input");
    }
    using namespace encoding_detail;
    std::string bytes;
    append_u32(bytes, 0); append_u32(bytes, 0x80ff0102); append_u32(bytes, UINT32_MAX);
    append_u64(bytes, 0); append_u64(bytes, 0x80ff010203040506ULL); append_u64(bytes, UINT64_MAX);
    require(bytes == unhex("0000000080ff0102ffffffff000000000000000080ff010203040506ffffffffffffffff"), "big endian boundary mismatch");
    require(checked_length(0) == 0, "zero conversion");
    require(checked_length(std::numeric_limits<std::size_t>::max()) == std::numeric_limits<std::size_t>::max(), "size conversion");
    require(checked_add(UINT64_MAX, 0) == UINT64_MAX, "maximum addition");
    require(checked_add(UINT64_MAX - 1, 1) == UINT64_MAX, "boundary addition");
    try { (void)checked_add(UINT64_MAX, 1); }
    catch (const Error& error) { require(error.code() == ErrorCode::LengthOverflow, "overflow category"); return 0; }
    throw std::runtime_error("overflow accepted");
}
