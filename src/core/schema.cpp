#include "../../include/athena/core/schema.hpp"

namespace athena::core {

std::string_view schema_name(SchemaKind kind) noexcept
{
    switch (kind) {
    case SchemaKind::Configuration: return "configuration";
    case SchemaKind::PackageMetadata: return "package_metadata";
    case SchemaKind::SourceRecipe: return "source_recipe";
    case SchemaKind::ImportedBinaryMetadata: return "imported_binary_metadata";
    case SchemaKind::ArtifactMetadata: return "artifact_metadata";
    case SchemaKind::RepositorySnapshot: return "repository_snapshot";
    case SchemaKind::RepositoryRequest: return "repository_request";
    case SchemaKind::BuildIR: return "build_ir";
    case SchemaKind::Manifest: return "manifest";
    case SchemaKind::Provenance: return "provenance";
    case SchemaKind::Generation: return "generation";
    case SchemaKind::Journal: return "journal";
    }
    return "unknown";
}

void validate_schema_version(SchemaKind kind, int version)
{
    if (version != current_schema_version) {
        throw std::invalid_argument("Unsupported schema_version " +
            std::to_string(version) + " for " + std::string(schema_name(kind)));
    }
}

} // namespace athena::core
