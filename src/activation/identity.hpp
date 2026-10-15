// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <compare>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace athena::activation {
inline constexpr std::size_t max_current_size = 4 * 1024;
inline constexpr std::size_t max_document_size = 16 * 1024 * 1024;
inline constexpr std::size_t max_sources = 65536, max_operations = 65536;
inline constexpr std::size_t max_path_size = 4095, max_component_size = 255;
inline constexpr std::uint64_t max_generation = INT64_MAX;

enum class SchemaErrorCode { Schema, Noncanonical, Bounds, Identity, Path, Reference, Ordering, Conflict, Metadata };
class SchemaError : public std::invalid_argument {
public:
    SchemaError(SchemaErrorCode code, const std::string& message)
        : std::invalid_argument(message), code_(code) {}
    SchemaErrorCode code() const noexcept { return code_; }
private:
    SchemaErrorCode code_;
};
bool lowercase_hex(std::string_view bytes) noexcept;
bool sha256_text(std::string_view text) noexcept;

template<class Tag> class HexId {
public:
    static constexpr std::size_t encoded_size = 32;
    explicit HexId(std::string value) : value_(std::move(value)) {
        validate();
    }
    void validate() const {
        if (value_.size() != encoded_size || !lowercase_hex(value_))
            throw SchemaError(SchemaErrorCode::Identity, "Expected 32 lowercase hex ID");
    }
    const std::string& value() const noexcept { return value_; }
    auto operator<=>(const HexId&) const = default;
private:
    std::string value_;
};
template<class Tag> class Sha256Digest {
public:
    static constexpr std::size_t encoded_size = 71;
    explicit Sha256Digest(std::string value) : value_(std::move(value)) {
        validate();
    }
    void validate() const {
        if (!sha256_text(value_))
            throw SchemaError(SchemaErrorCode::Identity, "Expected canonical SHA-256 digest");
    }
    const std::string& value() const noexcept { return value_; }
    auto operator<=>(const Sha256Digest&) const = default;
private:
    std::string value_;
};
using ActivationId = HexId<struct ActivationIdTag>;
using ProjectionId = HexId<struct ProjectionIdTag>;
using AdoptionId = HexId<struct AdoptionIdTag>;
using PlanDigest = Sha256Digest<struct PlanDigestTag>;
using LegacySealDigest = Sha256Digest<struct LegacySealDigestTag>;

// Byte encoding only: no filesystem access or Unicode normalization.
std::string encode_hex(std::string_view bytes);
std::string decode_hex(std::string_view hex, std::size_t decoded_limit);
void validate_relative_path(std::string_view bytes);
void validate_absolute_locator(std::string_view bytes);
}
