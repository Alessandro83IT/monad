// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace athena::core {

enum class SchemaKind {
    Configuration, PackageMetadata, SourceRecipe, ImportedBinaryMetadata,
    ArtifactMetadata, RepositorySnapshot, RepositoryRequest, BuildIR,
    Manifest, Provenance, Generation, Journal
};

inline constexpr int current_schema_version = 1;

struct SchemaVersion {
    int value = current_schema_version;
    explicit SchemaVersion(int version = current_schema_version) : value(version)
    {
        if (value < 1) throw std::invalid_argument("schema_version must be positive");
    }
    bool operator==(const SchemaVersion&) const = default;
};

std::string_view schema_name(SchemaKind kind) noexcept;
void validate_schema_version(SchemaKind kind, int version);

} // namespace athena::core
