#pragma once

#include "schema.hpp"
#include "types.hpp"
#include "dependency.hpp"

#include <optional>
#include <map>
#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include <stdexcept>

namespace athena::core {

// Opaque, stable repository revision. Its interpretation belongs to the
// repository backend (for example a commit, database revision, or serial).
class RepositoryRevision {
public:
    explicit RepositoryRevision(std::string value) : value_(std::move(value))
    {
        if (value_.empty()) throw TypeError("RepositoryRevision cannot be empty");
    }
    const std::string& value() const noexcept { return value_; }
    auto operator<=>(const RepositoryRevision&) const = default;
private:
    std::string value_;
};

// Opaque metadata locator. It is a repository-relative key or an explicit URI,
// never a path implicitly resolved against the process working directory.
class MetadataReference {
public:
    explicit MetadataReference(std::string value) : value_(std::move(value))
    {
        if (value_.empty()) throw TypeError("MetadataReference cannot be empty");
    }
    const std::string& value() const noexcept { return value_; }
    auto operator<=>(const MetadataReference&) const = default;
private:
    std::string value_;
};

struct RepositorySnapshotRef {
    RepositoryId repository_id;
    RepositoryRevision revision;
    SchemaVersion schema_version{};
    std::optional<ContentDigest> metadata_digest;
    std::optional<SnapshotId> snapshot_id;

    void validate() const;
    bool operator==(const RepositorySnapshotRef&) const = default;
};

struct RepositoryProvenance {
    RepositoryId repository_id;
    std::optional<RepositoryRevision> revision;
    std::optional<MetadataReference> metadata_reference;
    std::optional<RepositorySnapshotRef> snapshot;

    void validate() const;
    bool operator==(const RepositoryProvenance&) const = default;
};

// The minimal catalog identity exposed to M1. Recipe and binary payload
// schemas deliberately remain separate from these common package facts.
struct PackageMetadata {
    PackageId package_id;
    PackageVersion version;
    SchemaVersion schema_version{};
    std::string display_name;
    std::string description;
    // Expressions use the existing Athena dependency text grammar. This keeps
    // the private AST as the sole semantic model and serializes through its
    // existing parser/serializer.
    std::vector<std::string> dependencies;
    std::vector<std::string> capabilities;
    std::vector<std::string> features;
    std::vector<std::string> conflicts;
    std::vector<std::string> platforms;
    std::vector<std::string> architectures;
    std::vector<std::string> abis;
    // Canonical dependency expressions activated by configuring each feature.
    // Appended to preserve existing aggregate initializers.
    std::map<std::string, std::vector<DependencyMetadata>> feature_dependencies;

    void validate() const;
    bool operator==(const PackageMetadata&) const = default;
};

// Typed boundaries for future payload models. M1.1 identifies their references
// without implementing recipe parsing, binary import, or artifact storage.
struct SourceRecipe {
    SchemaVersion schema_version{};
    PackageId package_id;
    PackageVersion version;
    struct Source {
        MetadataReference reference;
        std::optional<ContentDigest> digest;
        std::optional<std::uint64_t> size_bytes;
        bool operator==(const Source&) const = default;
    };
    std::vector<Source> sources;
    std::vector<BuildInputId> build_inputs;
    std::string build_backend;
    std::map<std::string, std::string> build_parameters;
    std::optional<MetadataReference> build_ir_reference;

    void validate() const;
    bool operator==(const SourceRecipe&) const = default;
};

enum class ImportedBinaryFormat { ArchPackage, GenericArchive };

struct ImportedBinaryMetadata {
    SchemaVersion schema_version{};
    PackageMetadata package;
    MetadataReference archive_reference;
    ImportedBinaryFormat format = ImportedBinaryFormat::GenericArchive;
    // Transport archive digest, never the normalized ArtifactId.
    std::optional<ContentDigest> original_digest;
    std::optional<std::uint64_t> size_bytes;
    std::optional<RepositoryProvenance> repository_origin;
    // Preserve ecosystem-specific metadata without interpreting it as a
    // SourceRecipe or dropping fields unknown to Athena.
    std::map<std::string, std::vector<std::string>> ecosystem_fields;

    void validate() const;
    bool operator==(const ImportedBinaryMetadata&) const = default;
};

struct ArtifactMetadata {
    SchemaVersion schema_version{};
    ArtifactId artifact_id;
    ContentDigest output_digest;
    ManifestDigest manifest_digest;
    Provenance provenance;
    // Required for BuiltFromAthenaIR; absent for AmbientSourceBuild and
    // ImportedBinary. Producer attestations require a future contract.
    // Identity values remain opaque here; validation is not local verification.
    std::optional<BuildInputId> build_input_id;

    void validate() const;
    bool operator==(const ArtifactMetadata&) const = default;
};

// Deterministic TOML codecs for the versioned metadata documents. They parse
// from and return values only; callers own all file and repository I/O.
std::string serialize(const PackageMetadata& metadata);
PackageMetadata deserialize_package_metadata(const std::string& document);
std::string serialize(const SourceRecipe& recipe);
SourceRecipe deserialize_source_recipe(const std::string& document);
std::string serialize(const ImportedBinaryMetadata& metadata);
ImportedBinaryMetadata deserialize_imported_binary_metadata(const std::string& document);
std::string serialize(const ArtifactMetadata& metadata);
ArtifactMetadata deserialize_artifact_metadata(const std::string& document);

void validate_digest(const ContentDigest& digest);
void validate_digest(const ManifestDigest& digest);

// A repository result that can safely be passed to later planning stages.
// Package name alone is not its identity: package_id + version + repository
// provenance identify the selected repository record.
struct RepositoryCandidate {
    PackageMetadata package;
    RepositoryProvenance provenance;
    std::optional<SourceRecipe> source_recipe;
    std::optional<ImportedBinaryMetadata> imported_binary;
    std::optional<ArtifactMetadata> artifact;

    void validate() const;
    bool operator==(const RepositoryCandidate&) const = default;
};

class RepositorySnapshot {
public:
    // Builds, canonicalizes and validates a complete value. Empty candidate
    // collections are valid (an empty repository is still a repository state).
    static RepositorySnapshot create(
        RepositoryId repository_id,
        RepositoryRevision revision,
        std::vector<RepositoryCandidate> candidates,
        SchemaVersion schema_version = SchemaVersion{}
    );

    const RepositoryId& repository_id() const noexcept { return repository_id_; }
    const RepositoryRevision& revision() const noexcept { return revision_; }
    SchemaVersion schema_version() const noexcept { return schema_version_; }
    const std::vector<RepositoryCandidate>& candidates() const noexcept { return candidates_; }
    // Returns detached candidates annotated with this snapshot reference; the
    // stored snapshot candidates themselves remain free of self-references.
    std::vector<RepositoryCandidate> candidates_with_snapshot_reference() const;
    // SHA-256 of the canonical candidate metadata, independent of repository
    // ID and revision; id() commits to this digest plus those identity fields.
    const ContentDigest& metadata_digest() const noexcept { return metadata_digest_; }
    // SnapshotId is a distinct type from ArtifactId and BuildInputId.
    const SnapshotId& id() const noexcept { return id_; }
    RepositorySnapshotRef reference() const;
    void validate() const;
    bool operator==(const RepositorySnapshot&) const = default;

private:
    RepositorySnapshot(
        RepositoryId repository_id,
        RepositoryRevision revision,
        SchemaVersion schema_version,
        std::vector<RepositoryCandidate> candidates,
        ContentDigest metadata_digest,
        SnapshotId id
    );

    RepositoryId repository_id_;
    RepositoryRevision revision_;
    SchemaVersion schema_version_;
    std::vector<RepositoryCandidate> candidates_;
    ContentDigest metadata_digest_;
    SnapshotId id_;
};

std::string serialize(const RepositorySnapshot& snapshot);
RepositorySnapshot deserialize_repository_snapshot(const std::string& document);

// Selection input only. Ordering is significant and preserved: repository_ids
// expresses caller selection order, while package_ids expresses request order.
// Candidate selection and all repository I/O are implemented in later tasks.
struct RepositoryRequest {
    SchemaVersion schema_version{};
    std::vector<RepositoryId> repository_ids;
    std::vector<PackageId> package_ids;
    std::optional<TargetEnvironment> target;

    void validate() const;
    bool operator==(const RepositoryRequest&) const = default;
};

enum class RepositoryErrorCode {
    InvalidConfiguration, MetadataMissing, MetadataInvalid,
    UnsupportedSchema, InvalidCandidate, SnapshotFailure
};

class RepositoryError : public std::runtime_error {
public:
    RepositoryError(RepositoryErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}
    RepositoryErrorCode code() const noexcept { return code_; }
private:
    RepositoryErrorCode code_;
};

// Source-level repository contract. It returns immutable snapshots; query
// never performs dependency solving or chooses a preferred version.
class Repository {
public:
    virtual ~Repository() = default;
    virtual const RepositoryId& repository_id() const noexcept = 0;
    virtual RepositoryRevision revision() const = 0;
    virtual RepositorySnapshot load_snapshot() const = 0;
    virtual std::vector<RepositoryCandidate> query(const RepositoryRequest&) const = 0;
};

struct LocalRepositoryConfig {
    RepositoryId repository_id;
    std::filesystem::path metadata_directory;
    void validate() const;
};

class LocalRepository final : public Repository {
public:
    explicit LocalRepository(LocalRepositoryConfig config);
    const RepositoryId& repository_id() const noexcept override;
    RepositoryRevision revision() const override;
    RepositorySnapshot load_snapshot() const override;
    std::vector<RepositoryCandidate> query(const RepositoryRequest&) const override;
private:
    LocalRepositoryConfig config_;
};

struct ArchRepositoryConfig {
    RepositoryId repository_id;
    std::string repository_name;
    std::filesystem::path database_file;
    std::string package_base_url;
    std::string target_architecture;
    void validate() const;
};

// Reads an explicitly configured local Arch repository database. Remote
// synchronization and authentication policy remain separate milestones.
class ArchRepository final : public Repository {
public:
    explicit ArchRepository(ArchRepositoryConfig config);
    const RepositoryId& repository_id() const noexcept override;
    RepositoryRevision revision() const override;
    RepositorySnapshot load_snapshot() const override;
    std::vector<RepositoryCandidate> query(const RepositoryRequest&) const override;
private:
    ArchRepositoryConfig config_;
};

} // namespace athena::core
