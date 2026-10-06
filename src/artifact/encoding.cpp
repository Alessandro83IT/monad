// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "encoding.hpp"
#include <limits>
namespace athena::artifact {
namespace encoding_detail {
std::uint64_t checked_length(std::size_t size)
{
    if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
        if (size > std::numeric_limits<std::uint64_t>::max())
            throw Error(ErrorCode::LengthOverflow, {}, "byte length exceeds U64");
    }
    return static_cast<std::uint64_t>(size);
}
std::uint64_t checked_add(std::uint64_t left, std::uint64_t right)
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        throw Error(ErrorCode::LengthOverflow, {}, "canonical length exceeds U64");
    return left + right;
}
void append_u32(std::string& output, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        output.push_back(static_cast<char>((value >> shift) & 0xff));
}
void append_u64(std::string& output, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
        output.push_back(static_cast<char>((value >> shift) & 0xff));
}
}
std::string canonical_bytes(const NormalizedArtifact& artifact)
{
    validate(artifact);
    using namespace encoding_detail;
    // Calculate the complete framed length before emitting anything. This
    // traversal can later drive a streaming sink without changing the grammar.
    std::uint64_t length = 33;
    const auto add_bytes = [&](std::string_view bytes) {
        length = checked_add(length, checked_add(8, checked_length(bytes.size())));
    };
    for (const auto& entry : artifact.entries) {
        length = checked_add(length, 1);
        add_bytes(entry.path);
        if (const auto* file = std::get_if<RegularFile>(&entry.value)) {
            length = checked_add(length, 1);
            add_bytes(file->content);
        } else if (const auto* link = std::get_if<SymbolicLink>(&entry.value)) add_bytes(link->target);
    }
    std::string output;
    if (length > output.max_size())
        throw Error(ErrorCode::LengthOverflow, {}, "canonical bytes exceed local string limit");
    output.reserve(static_cast<std::size_t>(length));
    output.append("athena-artifact-tree\0", 21);
    append_u32(output, artifact.schema_version);
    append_u64(output, checked_length(artifact.entries.size()));
    const auto append_bytes = [&](std::string_view bytes) {
        append_u64(output, checked_length(bytes.size()));
        output.append(bytes);
    };
    for (const auto& entry : artifact.entries) {
        if (std::holds_alternative<Directory>(entry.value)) {
            output.push_back('\x01'); append_bytes(entry.path);
        } else if (const auto* file = std::get_if<RegularFile>(&entry.value)) {
            output.push_back('\x02'); append_bytes(entry.path);
            output.push_back(file->executable ? '\x01' : '\x00');
            append_bytes(file->content);
        } else {
            output.push_back('\x03'); append_bytes(entry.path);
            append_bytes(std::get<SymbolicLink>(entry.value).target);
        }
    }
    return output;
}
}
