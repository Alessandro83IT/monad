// SPDX-License-Identifier: GPL-3.0-or-later
#include "identity.hpp"
#include "../artifact/model.hpp"
#include <algorithm>

namespace athena::activation {
bool lowercase_hex(std::string_view bytes) noexcept {
    return std::all_of(bytes.begin(), bytes.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
bool sha256_text(std::string_view text) noexcept {
    return text.size() == PlanDigest::encoded_size && text.starts_with("sha256:") && lowercase_hex(text.substr(7));
}
std::string encode_hex(std::string_view bytes) {
    if (bytes.size() > max_document_size / 2)
        throw SchemaError(SchemaErrorCode::Bounds, "Hex encoding exceeds document bound");
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) { result += digits[c >> 4]; result += digits[c & 15]; }
    return result;
}
std::string decode_hex(std::string_view hex, std::size_t limit) {
    if (hex.size() % 2 || !lowercase_hex(hex))
        throw SchemaError(SchemaErrorCode::Schema, "Expected even-length lowercase hex");
    if (hex.size() / 2 > limit || hex.size() / 2 > max_document_size)
        throw SchemaError(SchemaErrorCode::Bounds, "Decoded bytes exceed bound");
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    std::string result;
    result.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2)
        result += static_cast<char>((digit(hex[i]) << 4) | digit(hex[i + 1]));
    return result;
}
void validate_relative_path(std::string_view bytes) {
    if (bytes.size() > max_path_size)
        throw SchemaError(SchemaErrorCode::Bounds, "Path exceeds byte bound");
    try { artifact::validate_path(bytes); }
    catch (const artifact::Error& e) { throw SchemaError(SchemaErrorCode::Path, e.what()); }
    std::size_t start = 0;
    for (;;) {
        const auto end = bytes.find('/', start);
        const auto length = (end == bytes.npos ? bytes.size() : end) - start;
        if (length > max_component_size)
            throw SchemaError(SchemaErrorCode::Bounds, "Path component exceeds byte bound");
        if (end == bytes.npos) break;
        start = end + 1;
    }
}
void validate_absolute_locator(std::string_view bytes) {
    if (bytes.size() > max_path_size)
        throw SchemaError(SchemaErrorCode::Bounds, "Locator exceeds byte bound");
    if (bytes.empty() || bytes.front() != '/')
        throw SchemaError(SchemaErrorCode::Path, "Locator must be canonical and absolute");
    if (bytes != "/") validate_relative_path(bytes.substr(1));
}
}
