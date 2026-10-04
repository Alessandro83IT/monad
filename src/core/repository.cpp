#include "../../include/athena/core/repository.hpp"

#include "../dependency/parser.hpp"
#include "../dependency/serializer.hpp"
#include "../version/version.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>
#include <toml++/toml.hpp>
#include <openssl/evp.h>

namespace athena::core {
namespace {

std::string required_string(const toml::table& table, const char* key)
{
    const auto value = table[key].value<std::string>();
    if (!value || value->empty()) {
        throw TypeError(std::string("Missing or empty required metadata field: ") + key);
    }
    return *value;
}

int required_schema(const toml::table& table, SchemaKind kind)
{
    const auto raw = table["schema_version"].value<std::int64_t>();
    if (!raw || *raw < 1 || *raw > std::numeric_limits<int>::max()) {
        throw TypeError("Missing or invalid required metadata field: schema_version");
    }
    validate_schema_version(kind, static_cast<int>(*raw));
    return static_cast<int>(*raw);
}

std::optional<std::string> optional_string(const toml::table& table, const char* key)
{
    const auto* node = table.get(key);
    if (!node) return std::nullopt;
    const auto value = node->value<std::string>();
    if (!value) throw TypeError(std::string("Invalid metadata field type: ") + key);
    return *value;
}

std::optional<std::int64_t> optional_integer(const toml::table& table, const char* key)
{
    const auto* node = table.get(key);
    if (!node) return std::nullopt;
    const auto value = node->value<std::int64_t>();
    if (!value) throw TypeError(std::string("Invalid metadata field type: ") + key);
    return *value;
}

void reject_unknown_keys(
    const toml::table& table,
    std::initializer_list<std::string_view> accepted
)
{
    for (const auto& [key, value] : table) {
        (void)value;
        const auto name = key.str();
        if (std::find(accepted.begin(), accepted.end(), name) == accepted.end()) {
            throw TypeError("Unknown field in versioned metadata: " + std::string(name));
        }
    }
}

std::vector<std::string> string_array(
    const toml::table& table,
    const char* key,
    bool required = false
)
{
    const auto* array = table[key].as_array();
    if (!array) {
        if (required) throw TypeError(std::string("Missing required metadata array: ") + key);
        return {};
    }

    std::vector<std::string> result;
    for (const auto& node : *array) {
        const auto value = node.value<std::string>();
        if (!value || value->empty()) {
            throw TypeError(std::string("Invalid value in metadata array: ") + key);
        }
        result.push_back(*value);
    }
    return result;
}

void validate_names(const std::vector<std::string>& names, const char* field)
{
    std::set<std::string> unique;
    for (const auto& name : names) {
        if (name.empty() || name.find_first_of(" \t\n\r") != std::string::npos) {
            throw TypeError(std::string("Invalid name in metadata field: ") + field);
        }
        if (!unique.insert(name).second) {
            throw TypeError(std::string("Duplicate name in metadata field: ") + field);
        }
    }
}

std::string canonical_expression(const std::string& expression)
{
    try {
        return athena::dependency::serialize_dependency(
            athena::dependency::parse_dependency(expression)
        );
    } catch (const std::exception&) {
        throw TypeError("Invalid or unsupported dependency expression in package metadata");
    }
}

std::vector<std::string> canonical_expressions(const std::vector<std::string>& expressions)
{
    std::vector<std::string> result;
    result.reserve(expressions.size());
    for (const auto& expression : expressions) {
        result.push_back(canonical_expression(expression));
    }
    return result;
}

toml::array to_array(const std::vector<std::string>& values)
{
    toml::array result;
    for (const auto& value : values) result.push_back(value);
    return result;
}

void add_package_fields(toml::table& table, const PackageMetadata& metadata)
{
    table.insert("package_id", metadata.package_id.value());
    table.insert("version", metadata.version.value());
    table.insert("schema_version", metadata.schema_version.value);
    if (!metadata.display_name.empty()) table.insert("display_name", metadata.display_name);
    if (!metadata.description.empty()) table.insert("description", metadata.description);
    table.insert("dependencies", to_array(canonical_expressions(metadata.dependencies)));
    table.insert("capabilities", to_array(metadata.capabilities));
    table.insert("features", to_array(metadata.features));
    table.insert("conflicts", to_array(canonical_expressions(metadata.conflicts)));
    table.insert("platforms", to_array(metadata.platforms));
    table.insert("architectures", to_array(metadata.architectures));
    table.insert("abis", to_array(metadata.abis));
}

PackageMetadata read_package_fields(const toml::table& table)
{
    reject_unknown_keys(table, {
        "package_id", "version", "schema_version", "display_name", "description",
        "dependencies", "capabilities", "features", "conflicts", "platforms",
        "architectures", "abis"
    });
    PackageMetadata result{
        PackageId(required_string(table, "package_id")),
        PackageVersion(required_string(table, "version")),
        SchemaVersion(required_schema(table, SchemaKind::PackageMetadata))
    };
    result.display_name = optional_string(table, "display_name").value_or(std::string{});
    result.description = optional_string(table, "description").value_or(std::string{});
    result.dependencies = string_array(table, "dependencies", true);
    result.capabilities = string_array(table, "capabilities", true);
    result.features = string_array(table, "features", true);
    result.conflicts = string_array(table, "conflicts", true);
    result.platforms = string_array(table, "platforms", true);
    result.architectures = string_array(table, "architectures", true);
    result.abis = string_array(table, "abis", true);
    result.validate();
    return result;
}

std::string origin_name(ArtifactOrigin origin)
{
    switch (origin) {
    case ArtifactOrigin::BuiltFromAthenaIR: return "built_from_athena_ir";
    case ArtifactOrigin::ImportedBinary: return "imported_binary";
    }
    throw TypeError("Invalid ArtifactOrigin");
}

ArtifactOrigin read_origin(const std::string& value)
{
    if (value == "built_from_athena_ir") return ArtifactOrigin::BuiltFromAthenaIR;
    if (value == "imported_binary") return ArtifactOrigin::ImportedBinary;
    throw TypeError("Unsupported artifact origin");
}

std::string format_name(ImportedBinaryFormat format)
{
    switch (format) {
    case ImportedBinaryFormat::ArchPackage: return "arch_package";
    case ImportedBinaryFormat::GenericArchive: return "generic_archive";
    }
    throw TypeError("Invalid ImportedBinaryFormat");
}

ImportedBinaryFormat read_format(const std::string& value)
{
    if (value == "arch_package") return ImportedBinaryFormat::ArchPackage;
    if (value == "generic_archive") return ImportedBinaryFormat::GenericArchive;
    throw TypeError("Unsupported imported binary format");
}

void add_provenance(toml::table& table, const Provenance& provenance)
{
    table.insert("origin", origin_name(provenance.origin));
    if (!provenance.repository.empty()) table.insert("repository", provenance.repository);
    if (!provenance.repository_snapshot.empty()) {
        table.insert("repository_snapshot", provenance.repository_snapshot);
    }
    if (!provenance.source_reference.empty()) {
        table.insert("source_reference", provenance.source_reference);
    }
    if (!provenance.signer.empty()) table.insert("signer", provenance.signer);
}

Provenance read_provenance(const toml::table& table)
{
    reject_unknown_keys(table, {
        "origin", "repository", "repository_snapshot", "source_reference", "signer"
    });
    Provenance result;
    result.origin = read_origin(required_string(table, "origin"));
    result.repository = optional_string(table, "repository").value_or(std::string{});
    result.repository_snapshot = optional_string(table, "repository_snapshot").value_or(std::string{});
    result.source_reference = optional_string(table, "source_reference").value_or(std::string{});
    result.signer = optional_string(table, "signer").value_or(std::string{});
    return result;
}

toml::table parse_document(const std::string& document);
std::string format_toml(const toml::table& table);

toml::table candidate_table(const RepositoryCandidate& candidate)
{
    toml::table package;
    add_package_fields(package, candidate.package);
    toml::table result{{"package", std::move(package)}};
    if (candidate.provenance.metadata_reference) {
        result.insert("metadata_reference", candidate.provenance.metadata_reference->value());
    }
    if (candidate.source_recipe) {
        result.insert("source_recipe", parse_document(serialize(*candidate.source_recipe)));
    }
    if (candidate.imported_binary) {
        result.insert("imported_binary", parse_document(serialize(*candidate.imported_binary)));
    }
    if (candidate.artifact) {
        result.insert("artifact", parse_document(serialize(*candidate.artifact)));
    }
    return result;
}

RepositoryCandidate read_candidate(
    const toml::table& table,
    const RepositoryId& repository_id,
    const RepositoryRevision& revision
)
{
    reject_unknown_keys(table, {
        "package", "metadata_reference", "source_recipe", "imported_binary", "artifact"
    });
    const auto* package_table = table["package"].as_table();
    if (!package_table) throw TypeError("Repository candidate requires a package table");

    RepositoryProvenance provenance{repository_id, revision};
    if (const auto reference = optional_string(table, "metadata_reference")) {
        provenance.metadata_reference.emplace(*reference);
    }

    RepositoryCandidate candidate{
        read_package_fields(*package_table), std::move(provenance)
    };
    if (const auto* recipe = table["source_recipe"].as_table()) {
        candidate.source_recipe = deserialize_source_recipe(format_toml(*recipe));
    }
    if (const auto* imported = table["imported_binary"].as_table()) {
        candidate.imported_binary = deserialize_imported_binary_metadata(format_toml(*imported));
    }
    if (const auto* artifact = table["artifact"].as_table()) {
        candidate.artifact = deserialize_artifact_metadata(format_toml(*artifact));
    }
    candidate.validate();
    return candidate;
}

toml::array candidate_array(const std::vector<RepositoryCandidate>& candidates)
{
    toml::array result;
    for (const auto& candidate : candidates) result.push_back(candidate_table(candidate));
    return result;
}

std::string sha256(std::string_view bytes)
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0;
    if (EVP_Digest(
            bytes.data(), bytes.size(), digest.data(), &digest_size,
            EVP_sha256(), nullptr) != 1) {
        throw TypeError("SHA-256 digest calculation failed");
    }
    std::ostringstream encoded;
    encoded << "sha256:" << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_size; ++i) {
        encoded << std::setw(2) << static_cast<unsigned int>(digest[i]);
    }
    return encoded.str();
}

std::string candidates_material(const std::vector<RepositoryCandidate>& candidates)
{
    toml::table table{{"candidates", candidate_array(candidates)}};
    return format_toml(table);
}

std::string identity_material(
    RepositoryId const& repository_id,
    RepositoryRevision const& revision,
    SchemaVersion schema_version,
    const ContentDigest& metadata_digest
)
{
    toml::table table{
        {"schema_version", schema_version.value},
        {"repository_id", repository_id.value()},
        {"revision", revision.value()},
        {"metadata_digest", metadata_digest.value()}
    };
    return format_toml(table);
}

bool candidate_less(const RepositoryCandidate& left, const RepositoryCandidate& right)
{
    const auto left_reference = left.provenance.metadata_reference
        ? left.provenance.metadata_reference->value() : std::string{};
    const auto right_reference = right.provenance.metadata_reference
        ? right.provenance.metadata_reference->value() : std::string{};
    return std::tie(left.package.package_id.value(), left.package.version.value(),
                    left.provenance.repository_id.value(), left_reference) <
           std::tie(right.package.package_id.value(), right.package.version.value(),
                    right.provenance.repository_id.value(), right_reference);
}

void canonicalize_candidate(RepositoryCandidate& candidate)
{
    auto canonicalize_expressions = [](std::vector<std::string>& expressions) {
        expressions = canonical_expressions(expressions);
        std::sort(expressions.begin(), expressions.end());
    };
    auto sort_values = [](std::vector<std::string>& values) {
        std::sort(values.begin(), values.end());
    };
    canonicalize_expressions(candidate.package.dependencies);
    canonicalize_expressions(candidate.package.conflicts);
    sort_values(candidate.package.capabilities);
    sort_values(candidate.package.features);
    sort_values(candidate.package.platforms);
    sort_values(candidate.package.architectures);
    sort_values(candidate.package.abis);
}

toml::table parse_document(const std::string& document)
{
    try {
        return toml::parse(document);
    } catch (const toml::parse_error&) {
        throw TypeError("Malformed TOML metadata document");
    }
}

std::string format_toml(const toml::table& table)
{
    std::ostringstream output;
    output << table;
    return output.str();
}

void validate_version(const PackageVersion& version)
{
    try {
        (void)athena::version::less(version.value(), version.value());
    } catch (const std::exception&) {
        throw TypeError("Invalid package version");
    }
}

void validate_sha256_digest(std::string_view value, const char* label)
{
    if (!value.starts_with("sha256:") || value.size() != 7 + 64) {
        throw TypeError(std::string("Invalid SHA-256 ") + label);
    }
    validate_digest(ContentDigest(std::string(value)));
}

} // namespace

void validate_digest(const ContentDigest& digest)
{
    const auto& value = digest.value();
    const auto separator = value.find(':');
    if (separator == std::string::npos || separator == 0 || separator + 1 == value.size()) {
        throw TypeError("Malformed content digest");
    }
    const auto algorithm = value.substr(0, separator);
    const auto hex = value.substr(separator + 1);
    if (algorithm.empty() || !std::islower(static_cast<unsigned char>(algorithm.front())) ||
        !std::all_of(algorithm.begin(), algorithm.end(), [](unsigned char ch) {
            return std::islower(ch) || std::isdigit(ch) || ch == '-';
        }) || hex.size() % 2 != 0 ||
        !std::all_of(hex.begin(), hex.end(), [](unsigned char ch) {
            return std::isdigit(ch) || (ch >= 'a' && ch <= 'f');
        })) {
        throw TypeError("Malformed content digest");
    }
}

void validate_digest(const ManifestDigest& digest)
{
    validate_digest(ContentDigest(digest.value()));
}

void RepositorySnapshotRef::validate() const
{
    if (repository_id.empty()) throw TypeError("RepositorySnapshotRef requires a RepositoryId");
    validate_schema_version(SchemaKind::RepositorySnapshot, schema_version.value);
    if (metadata_digest) validate_sha256_digest(metadata_digest->value(), "metadata digest");
    if (snapshot_id) {
        if (!metadata_digest) {
            throw TypeError("Snapshot identity requires its metadata digest reference");
        }
        validate_sha256_digest(snapshot_id->value(), "snapshot identity");
    }
}

void RepositoryProvenance::validate() const
{
    if (repository_id.empty()) throw TypeError("RepositoryProvenance requires a RepositoryId");
    if (snapshot) {
        snapshot->validate();
        if (snapshot->repository_id != repository_id) {
            throw TypeError("Candidate repository differs from its snapshot repository");
        }
        if (revision && *revision != snapshot->revision) {
            throw TypeError("Candidate revision differs from its snapshot revision");
        }
    }
}

void PackageMetadata::validate() const
{
    if (package_id.empty()) throw TypeError("PackageMetadata requires a PackageId");
    validate_schema_version(SchemaKind::PackageMetadata, schema_version.value);
    validate_version(version);
    validate_names(capabilities, "capabilities");
    validate_names(features, "features");
    validate_names(platforms, "platforms");
    validate_names(architectures, "architectures");
    validate_names(abis, "abis");
    (void)canonical_expressions(dependencies);
    (void)canonical_expressions(conflicts);
}

void SourceRecipe::validate() const
{
    validate_schema_version(SchemaKind::SourceRecipe, schema_version.value);
    if (package_id.empty()) throw TypeError("SourceRecipe requires a PackageId");
    validate_version(version);
    if (sources.empty()) throw TypeError("SourceRecipe requires at least one source");
    for (const auto& source : sources) {
        if (source.digest) validate_digest(*source.digest);
        if (source.size_bytes && *source.size_bytes >
                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            throw TypeError("Source size exceeds the metadata format limit");
        }
    }
    for (const auto& input : build_inputs) {
        if (input.empty()) throw TypeError("SourceRecipe contains an empty BuildInputId");
    }
    if (build_backend.empty()) throw TypeError("SourceRecipe requires a build backend");
    for (const auto& [key, value] : build_parameters) {
        (void)value;
        if (key.empty()) throw TypeError("SourceRecipe contains an empty build parameter name");
    }
    if (build_ir_reference && build_ir_reference->value().empty()) {
        throw TypeError("SourceRecipe Build IR reference cannot be empty");
    }
}

void ImportedBinaryMetadata::validate() const
{
    validate_schema_version(SchemaKind::ImportedBinaryMetadata, schema_version.value);
    package.validate();
    if (archive_reference.value().empty()) {
        throw TypeError("ImportedBinaryMetadata requires an archive reference");
    }
    if (original_digest) validate_digest(*original_digest);
    if (size_bytes && *size_bytes >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        throw TypeError("Imported archive size exceeds the metadata format limit");
    }
    if (repository_origin) repository_origin->validate();
    switch (format) {
    case ImportedBinaryFormat::ArchPackage:
    case ImportedBinaryFormat::GenericArchive:
        break;
    default:
        throw TypeError("Unsupported imported binary format");
    }
}

void ArtifactMetadata::validate() const
{
    validate_schema_version(SchemaKind::ArtifactMetadata, schema_version.value);
    if (artifact_id.empty()) throw TypeError("ArtifactMetadata requires an ArtifactId");
    validate_digest(output_digest);
    validate_digest(manifest_digest);
    if (build_input_id && build_input_id->empty()) {
        throw TypeError("ArtifactMetadata BuildInputId cannot be empty");
    }
    if (provenance.origin != ArtifactOrigin::BuiltFromAthenaIR &&
        provenance.origin != ArtifactOrigin::ImportedBinary) {
        throw TypeError("Unsupported artifact origin");
    }
    if (provenance.origin == ArtifactOrigin::BuiltFromAthenaIR && !build_input_id) {
        throw TypeError("Athena-built ArtifactMetadata requires a BuildInputId");
    }
    if (provenance.origin == ArtifactOrigin::ImportedBinary && build_input_id) {
        throw TypeError("Imported binary ArtifactMetadata cannot have a BuildInputId");
    }
}

void RepositoryCandidate::validate() const
{
    package.validate();
    provenance.validate();
    if (source_recipe && imported_binary) {
        throw TypeError("RepositoryCandidate cannot be both source and imported binary");
    }
    if (source_recipe) {
        source_recipe->validate();
        if (source_recipe->package_id != package.package_id ||
            source_recipe->version != package.version) {
            throw TypeError("RepositoryCandidate SourceRecipe identity does not match PackageMetadata");
        }
        if (artifact && artifact->provenance.origin != ArtifactOrigin::BuiltFromAthenaIR) {
            throw TypeError("SourceRecipe candidate references a non-Athena-built artifact");
        }
    }
    if (imported_binary) {
        imported_binary->validate();
        if (imported_binary->package.package_id != package.package_id ||
            imported_binary->package.version != package.version) {
            throw TypeError("RepositoryCandidate binary identity does not match PackageMetadata");
        }
        if (artifact && artifact->provenance.origin != ArtifactOrigin::ImportedBinary) {
            throw TypeError("Imported binary candidate references a non-imported artifact");
        }
    }
    if (artifact) artifact->validate();
}

RepositorySnapshot::RepositorySnapshot(
    RepositoryId repository_id,
    RepositoryRevision revision,
    SchemaVersion schema_version,
    std::vector<RepositoryCandidate> candidates,
    ContentDigest metadata_digest,
    SnapshotId id
)
    : repository_id_(std::move(repository_id)),
      revision_(std::move(revision)),
      schema_version_(schema_version),
      candidates_(std::move(candidates)),
      metadata_digest_(std::move(metadata_digest)),
      id_(std::move(id))
{
}

RepositorySnapshot RepositorySnapshot::create(
    RepositoryId repository_id,
    RepositoryRevision revision,
    std::vector<RepositoryCandidate> candidates,
    SchemaVersion schema_version
)
{
    for (auto& candidate : candidates) canonicalize_candidate(candidate);
    std::sort(candidates.begin(), candidates.end(), candidate_less);
    const ContentDigest metadata_digest(sha256(candidates_material(candidates)));
    const SnapshotId id(sha256(identity_material(
        repository_id, revision, schema_version, metadata_digest)));
    RepositorySnapshot result(
        std::move(repository_id), std::move(revision), schema_version,
        std::move(candidates), metadata_digest, id);
    result.validate();
    return result;
}

RepositorySnapshotRef RepositorySnapshot::reference() const
{
    return RepositorySnapshotRef{
        repository_id_, revision_, schema_version_, metadata_digest_, id_
    };
}

std::vector<RepositoryCandidate> RepositorySnapshot::candidates_with_snapshot_reference() const
{
    auto result = candidates_;
    const auto snapshot_reference = reference();
    for (auto& candidate : result) {
        candidate.provenance.snapshot = snapshot_reference;
    }
    return result;
}

void RepositorySnapshot::validate() const
{
    if (repository_id_.empty()) throw TypeError("RepositorySnapshot requires a RepositoryId");
    validate_schema_version(SchemaKind::RepositorySnapshot, schema_version_.value);

    std::set<std::tuple<std::string, std::string, std::string>> identities;
    for (const auto& candidate : candidates_) {
        candidate.validate();
        if (candidate.provenance.repository_id != repository_id_) {
            throw TypeError("RepositorySnapshot candidate has a mismatched RepositoryId");
        }
        if (candidate.provenance.revision &&
            *candidate.provenance.revision != revision_) {
            throw TypeError("RepositorySnapshot candidate has a mismatched revision");
        }
        if (candidate.provenance.snapshot) {
            throw TypeError("RepositorySnapshot candidate cannot recursively reference a snapshot");
        }
        const auto key = std::make_tuple(
            candidate.package.package_id.value(),
            candidate.package.version.value(),
            candidate.provenance.repository_id.value());
        if (!identities.insert(key).second) {
            throw TypeError("RepositorySnapshot contains a duplicate package identity and version");
        }
    }

    validate_sha256_digest(metadata_digest_.value(), "metadata digest");
    validate_sha256_digest(id_.value(), "snapshot identity");
    const ContentDigest expected_metadata(sha256(candidates_material(candidates_)));
    if (metadata_digest_ != expected_metadata) {
        throw TypeError("RepositorySnapshot metadata digest does not match its candidates");
    }
    const SnapshotId expected_id(sha256(identity_material(
        repository_id_, revision_, schema_version_, expected_metadata)));
    if (id_ != expected_id) {
        throw TypeError("RepositorySnapshot identity does not match its content and revision");
    }
}

std::string serialize(const RepositorySnapshot& snapshot)
{
    snapshot.validate();
    toml::table table{
        {"schema_version", snapshot.schema_version().value},
        {"repository_id", snapshot.repository_id().value()},
        {"revision", snapshot.revision().value()},
        {"metadata_digest", snapshot.metadata_digest().value()},
        {"snapshot_id", snapshot.id().value()},
        {"candidates", candidate_array(snapshot.candidates())}
    };
    return format_toml(table);
}

RepositorySnapshot deserialize_repository_snapshot(const std::string& document)
{
    const auto table = parse_document(document);
    reject_unknown_keys(table, {
        "schema_version", "repository_id", "revision", "metadata_digest",
        "snapshot_id", "candidates"
    });
    const RepositoryId repository_id(required_string(table, "repository_id"));
    const RepositoryRevision revision(required_string(table, "revision"));
    const SchemaVersion schema_version(required_schema(table, SchemaKind::RepositorySnapshot));
    const ContentDigest expected_metadata(required_string(table, "metadata_digest"));
    const SnapshotId expected_id(required_string(table, "snapshot_id"));
    const auto* candidates = table["candidates"].as_array();
    if (!candidates) throw TypeError("Missing required metadata array: candidates");

    std::vector<RepositoryCandidate> entries;
    entries.reserve(candidates->size());
    for (const auto& node : *candidates) {
        const auto* candidate = node.as_table();
        if (!candidate) throw TypeError("Invalid candidate entry in RepositorySnapshot");
        entries.push_back(read_candidate(*candidate, repository_id, revision));
    }
    auto result = RepositorySnapshot::create(
        repository_id, revision, std::move(entries), schema_version);
    if (result.metadata_digest() != expected_metadata) {
        throw TypeError("RepositorySnapshot metadata digest verification failed");
    }
    if (result.id() != expected_id) {
        throw TypeError("RepositorySnapshot identity verification failed");
    }
    return result;
}

void RepositoryRequest::validate() const
{
    validate_schema_version(SchemaKind::RepositoryRequest, schema_version.value);
    if (repository_ids.empty()) throw TypeError("RepositoryRequest requires at least one RepositoryId");
    if (package_ids.empty()) throw TypeError("RepositoryRequest requires at least one PackageId");
    std::set<RepositoryId> repositories;
    for (const auto& id : repository_ids) {
        if (id.empty() || !repositories.insert(id).second) {
            throw TypeError("RepositoryRequest has an empty or duplicate RepositoryId");
        }
    }
    std::set<PackageId> packages;
    for (const auto& id : package_ids) {
        if (id.empty() || !packages.insert(id).second) {
            throw TypeError("RepositoryRequest has an empty or duplicate PackageId");
        }
    }
}

std::string serialize(const PackageMetadata& metadata)
{
    metadata.validate();
    toml::table table;
    add_package_fields(table, metadata);
    return format_toml(table);
}

PackageMetadata deserialize_package_metadata(const std::string& document)
{
    return read_package_fields(parse_document(document));
}

std::string serialize(const SourceRecipe& recipe)
{
    recipe.validate();
    toml::table table{
        {"schema_version", recipe.schema_version.value},
        {"package_id", recipe.package_id.value()},
        {"version", recipe.version.value()},
        {"build_backend", recipe.build_backend}
    };
    toml::array sources;
    for (const auto& source : recipe.sources) {
        toml::table entry{{"reference", source.reference.value()}};
        if (source.digest) entry.insert("digest", source.digest->value());
        if (source.size_bytes) entry.insert("size_bytes", static_cast<std::int64_t>(*source.size_bytes));
        sources.push_back(std::move(entry));
    }
    table.insert("sources", std::move(sources));
    toml::array inputs;
    for (const auto& input : recipe.build_inputs) inputs.push_back(input.value());
    table.insert("build_inputs", std::move(inputs));
    toml::table parameters;
    for (const auto& [key, value] : recipe.build_parameters) parameters.insert(key, value);
    table.insert("build_parameters", std::move(parameters));
    if (recipe.build_ir_reference) {
        table.insert("build_ir_reference", recipe.build_ir_reference->value());
    }
    return format_toml(table);
}

SourceRecipe deserialize_source_recipe(const std::string& document)
{
    const auto table = parse_document(document);
    reject_unknown_keys(table, {
        "schema_version", "package_id", "version", "build_backend", "sources",
        "build_inputs", "build_parameters", "build_ir_reference"
    });
    SourceRecipe recipe{
        SchemaVersion(required_schema(table, SchemaKind::SourceRecipe)),
        PackageId(required_string(table, "package_id")),
        PackageVersion(required_string(table, "version"))
    };
    recipe.build_backend = required_string(table, "build_backend");
    const auto* sources = table["sources"].as_array();
    if (!sources) throw TypeError("Missing required metadata array: sources");
    for (const auto& node : *sources) {
        const auto* entry = node.as_table();
        if (!entry) throw TypeError("Invalid source entry in SourceRecipe");
        reject_unknown_keys(*entry, {"reference", "digest", "size_bytes"});
        SourceRecipe::Source source{MetadataReference(required_string(*entry, "reference"))};
        if (const auto value = optional_string(*entry, "digest")) {
            source.digest.emplace(*value);
        }
        if (const auto value = optional_integer(*entry, "size_bytes")) {
            if (*value < 0) throw TypeError("Invalid source size in SourceRecipe");
            source.size_bytes = static_cast<std::uint64_t>(*value);
        }
        recipe.sources.push_back(std::move(source));
    }
    for (const auto& input : string_array(table, "build_inputs", true)) {
        recipe.build_inputs.emplace_back(input);
    }
    if (const auto* params = table["build_parameters"].as_table()) {
        for (const auto& [key, value] : *params) {
            const auto text = value.value<std::string>();
            if (!text) throw TypeError("Build parameters must be strings");
            recipe.build_parameters.emplace(std::string(key.str()), *text);
        }
    } else {
        throw TypeError("Missing required metadata table: build_parameters");
    }
    if (const auto reference = optional_string(table, "build_ir_reference")) {
        recipe.build_ir_reference.emplace(*reference);
    }
    recipe.validate();
    return recipe;
}

std::string serialize(const ImportedBinaryMetadata& metadata)
{
    metadata.validate();
    toml::table package;
    add_package_fields(package, metadata.package);
    toml::table table{
        {"schema_version", metadata.schema_version.value},
        {"archive_reference", metadata.archive_reference.value()},
        {"format", format_name(metadata.format)},
        {"package", std::move(package)}
    };
    if (metadata.original_digest) table.insert("original_digest", metadata.original_digest->value());
    if (metadata.size_bytes) table.insert("size_bytes", static_cast<std::int64_t>(*metadata.size_bytes));
    toml::table fields;
    for (const auto& [key, values] : metadata.ecosystem_fields) {
        fields.insert(key, to_array(values));
    }
    table.insert("ecosystem_fields", std::move(fields));
    if (metadata.repository_origin) {
        toml::table provenance{
            {"repository_id", metadata.repository_origin->repository_id.value()}
        };
        if (metadata.repository_origin->revision) {
            provenance.insert("revision", metadata.repository_origin->revision->value());
        }
        if (metadata.repository_origin->metadata_reference) {
            provenance.insert("metadata_reference", metadata.repository_origin->metadata_reference->value());
        }
        if (metadata.repository_origin->snapshot) {
            const auto& snapshot = *metadata.repository_origin->snapshot;
            toml::table snapshot_table{
                {"repository_id", snapshot.repository_id.value()},
                {"revision", snapshot.revision.value()},
                {"schema_version", snapshot.schema_version.value}
            };
            if (snapshot.metadata_digest) {
                snapshot_table.insert("metadata_digest", snapshot.metadata_digest->value());
            }
            if (snapshot.snapshot_id) {
                snapshot_table.insert("snapshot_id", snapshot.snapshot_id->value());
            }
            provenance.insert("snapshot", std::move(snapshot_table));
        }
        table.insert("repository_origin", std::move(provenance));
    }
    return format_toml(table);
}

ImportedBinaryMetadata deserialize_imported_binary_metadata(const std::string& document)
{
    const auto table = parse_document(document);
    reject_unknown_keys(table, {
        "schema_version", "archive_reference", "format", "package", "original_digest",
        "size_bytes", "ecosystem_fields", "repository_origin"
    });
    const auto* package_table = table["package"].as_table();
    if (!package_table) throw TypeError("Missing required metadata table: package");
    ImportedBinaryMetadata result{
        SchemaVersion(required_schema(table, SchemaKind::ImportedBinaryMetadata)),
        read_package_fields(*package_table),
        MetadataReference(required_string(table, "archive_reference")),
        read_format(required_string(table, "format"))
    };
    if (const auto digest = optional_string(table, "original_digest")) {
        result.original_digest.emplace(*digest);
    }
    if (const auto size = optional_integer(table, "size_bytes")) {
        if (*size < 0) throw TypeError("Invalid imported archive size");
        result.size_bytes = static_cast<std::uint64_t>(*size);
    }
    if (const auto* fields = table["ecosystem_fields"].as_table()) {
        for (const auto& [key, value] : *fields) {
            const auto* array = value.as_array();
            if (!array) throw TypeError("Imported ecosystem fields must be string arrays");
            std::vector<std::string> values;
            for (const auto& item : *array) {
                const auto text = item.value<std::string>();
                if (!text) throw TypeError("Imported ecosystem fields must contain strings");
                values.push_back(*text);
            }
            result.ecosystem_fields.emplace(std::string(key.str()), std::move(values));
        }
    } else {
        throw TypeError("Missing required metadata table: ecosystem_fields");
    }
    if (const auto* origin = table["repository_origin"].as_table()) {
        reject_unknown_keys(*origin, {"repository_id", "revision", "metadata_reference", "snapshot"});
        RepositoryProvenance provenance{
            RepositoryId(required_string(*origin, "repository_id"))
        };
        if (const auto revision = optional_string(*origin, "revision")) {
            provenance.revision.emplace(*revision);
        }
        if (const auto reference = optional_string(*origin, "metadata_reference")) {
            provenance.metadata_reference.emplace(*reference);
        }
        if (const auto* snapshot = (*origin)["snapshot"].as_table()) {
            reject_unknown_keys(*snapshot, {
                "repository_id", "revision", "schema_version", "metadata_digest", "snapshot_id"
            });
            RepositorySnapshotRef ref{
                RepositoryId(required_string(*snapshot, "repository_id")),
                RepositoryRevision(required_string(*snapshot, "revision")),
                SchemaVersion(required_schema(*snapshot, SchemaKind::RepositorySnapshot))
            };
            if (const auto digest = optional_string(*snapshot, "metadata_digest")) {
                ref.metadata_digest.emplace(*digest);
            }
            if (const auto id = optional_string(*snapshot, "snapshot_id")) {
                ref.snapshot_id.emplace(*id);
            }
            provenance.snapshot = std::move(ref);
        }
        result.repository_origin = std::move(provenance);
    }
    result.validate();
    return result;
}

std::string serialize(const ArtifactMetadata& metadata)
{
    metadata.validate();
    toml::table table{
        {"schema_version", metadata.schema_version.value},
        {"artifact_id", metadata.artifact_id.value()},
        {"output_digest", metadata.output_digest.value()},
        {"manifest_digest", metadata.manifest_digest.value()},
        {"provenance", toml::table{}}
    };
    add_provenance(*table["provenance"].as_table(), metadata.provenance);
    if (metadata.build_input_id) table.insert("build_input_id", metadata.build_input_id->value());
    return format_toml(table);
}

ArtifactMetadata deserialize_artifact_metadata(const std::string& document)
{
    const auto table = parse_document(document);
    reject_unknown_keys(table, {
        "schema_version", "artifact_id", "output_digest", "manifest_digest",
        "provenance", "build_input_id"
    });
    const auto* provenance_table = table["provenance"].as_table();
    if (!provenance_table) throw TypeError("Missing required metadata table: provenance");
    ArtifactMetadata result{
        SchemaVersion(required_schema(table, SchemaKind::ArtifactMetadata)),
        ArtifactId(required_string(table, "artifact_id")),
        ContentDigest(required_string(table, "output_digest")),
        ManifestDigest(required_string(table, "manifest_digest")),
        read_provenance(*provenance_table)
    };
    if (const auto input = optional_string(table, "build_input_id")) {
        result.build_input_id.emplace(*input);
    }
    result.validate();
    return result;
}

} // namespace athena::core
