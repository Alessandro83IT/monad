// SPDX-License-Identifier: GPL-3.0-or-later
#include "codec_detail.hpp"
#include <algorithm>
#include <charconv>

namespace athena::activation::codec_detail {
toml::table parse(std::string_view bytes, std::size_t limit) {
    if (bytes.size() > limit) throw SchemaError(SchemaErrorCode::Bounds, "Document exceeds bound");
    try { return toml::parse(bytes); }
    catch (const toml::parse_error& e) { throw SchemaError(SchemaErrorCode::Schema, e.what()); }
}
void keys(const toml::table& table, std::initializer_list<std::string_view> expected) {
    if (table.size() != expected.size()) throw SchemaError(SchemaErrorCode::Schema, "Incorrect key set");
    for (const auto& [key, value] : table) {
        (void)value;
        if (std::find(expected.begin(), expected.end(), key.str()) == expected.end())
            throw SchemaError(SchemaErrorCode::Schema, "Unknown key: " + std::string(key.str()));
    }
}
std::string string(const toml::table& table, std::string_view key) {
    if (!table[key].is_string()) throw SchemaError(SchemaErrorCode::Schema, "Expected string: " + std::string(key));
    return *table[key].value<std::string>();
}
std::uint64_t integer(const toml::table& table, std::string_view key) {
    if (!table[key].is_integer()) throw SchemaError(SchemaErrorCode::Schema, "Expected integer: " + std::string(key));
    const auto value = table[key].value<std::int64_t>();
    if (!value || *value < 0) throw SchemaError(SchemaErrorCode::Schema, "Expected nonnegative int64: " + std::string(key));
    return static_cast<std::uint64_t>(*value);
}
void version(const toml::table& table) {
    if (integer(table, "schema_version") != 1) throw SchemaError(SchemaErrorCode::Schema, "Unsupported schema version");
}
void canonical(std::string_view supplied, std::string_view written) {
    if (supplied != written) throw SchemaError(SchemaErrorCode::Noncanonical, "Noncanonical document bytes");
}
void generation(std::uint64_t id) {
    if (id > max_generation) throw SchemaError(SchemaErrorCode::Bounds, "Generation exceeds int64 range");
}
std::uint64_t decimal(std::string_view bytes) {
    if (bytes.empty() || (bytes.size() > 1 && bytes.front() == '0'))
        throw SchemaError(SchemaErrorCode::Schema, "Invalid canonical decimal");
    for (char c : bytes) if (c < '0' || c > '9')
        throw SchemaError(SchemaErrorCode::Schema, "Invalid canonical decimal");
    std::uint64_t result;
    const auto [end, error] = std::from_chars(bytes.data(), bytes.data() + bytes.size(), result);
    if (error != std::errc{} || end != bytes.data() + bytes.size() || result > max_generation)
        throw SchemaError(SchemaErrorCode::Bounds, "Decimal exceeds int64 range");
    return result;
}
void append_u64(std::string& out, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) out += static_cast<char>((value >> shift) & 255);
}
void Writer::append(std::string_view bytes) {
    if (bytes.size() > limit_ - bytes_.size()) throw SchemaError(SchemaErrorCode::Bounds, "Serialized document exceeds bound");
    bytes_.append(bytes);
}
void Writer::text(std::string_view key, std::string_view value) {
    // All values supplied here are validated ASCII grammars, never native paths.
    if (value.find_first_of("\"\\\r\n") != value.npos)
        throw SchemaError(SchemaErrorCode::Schema, "Unsafe canonical string field");
    append(key); append(" = \""); append(value); append("\"\n");
}
void Writer::number(std::string_view key, std::uint64_t value) {
    generation(value); append(key); append(" = "); append(std::to_string(value)); append("\n");
}
}
