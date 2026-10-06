#include <athena/core/repository.hpp>

#include "../repository/repository.hpp"
#include "../package/package_loader.hpp"
#include "../dependency/serializer.hpp"

#include <algorithm>
#include <set>
#include <system_error>

namespace athena::core {

void LocalRepositoryConfig::validate() const
{
    if (repository_id.empty()) {
        throw RepositoryError(RepositoryErrorCode::InvalidConfiguration,
                              "Local repository requires a RepositoryId");
    }
    if (metadata_directory.empty() || !metadata_directory.is_absolute()) {
        throw RepositoryError(RepositoryErrorCode::InvalidConfiguration,
                              "Local repository metadata directory must be an explicit absolute path");
    }
}

LocalRepository::LocalRepository(LocalRepositoryConfig config)
    : config_(std::move(config))
{
    config_.validate();
}

const RepositoryId& LocalRepository::repository_id() const noexcept
{
    return config_.repository_id;
}

namespace {

std::string stable_metadata_key(const std::filesystem::path& root,
                                const std::filesystem::path& file)
{
    const auto relative = std::filesystem::relative(file, root).generic_string();
    if (relative.empty() || relative.starts_with("../")) {
        throw RepositoryError(RepositoryErrorCode::InvalidConfiguration,
                              "Package metadata path escapes repository root");
    }
    return relative;
}

RepositoryCandidate convert_package(const athena::package::Package& legacy,
                                    const std::string& metadata_key,
                                    const RepositoryId& repository_id)
{
    PackageMetadata metadata{
        PackageId(legacy.name), PackageVersion(legacy.version), SchemaVersion(1),
        legacy.name, legacy.description
    };
    for (const auto& dependency : legacy.dependencies) {
        metadata.dependencies.push_back(
            athena::dependency::serialize_dependency(dependency));
    }
    metadata.capabilities = legacy.provides.capabilities;
    metadata.features = legacy.provides.features;
    for (const auto& conflict : legacy.conflicts)
        metadata.conflicts.push_back(athena::dependency::serialize_dependency(conflict));
    for (const auto& [feature, dependencies] : legacy.feature_dependencies) {
        auto& entries = metadata.feature_dependencies[feature];
        for (const auto& dependency : dependencies)
            entries.push_back({{athena::dependency::serialize_expression(dependency.expression)},
                static_cast<DependencyRole>(dependency.kind),
                static_cast<DependencyTargetContext>(dependency.context), dependency.enabled});
    }
    metadata.platforms = legacy.platforms;
    metadata.architectures = legacy.architectures;
    metadata.abis = legacy.abis;
    // Public capability providers currently use the package version. Never
    // silently discard a distinct legacy capability version during migration.
    for (const auto& [capability, version] : legacy.provides.capability_versions)
        if (version != legacy.version)
            throw RepositoryError(RepositoryErrorCode::InvalidCandidate,
                "Unsupported capability-specific version for '" + capability + "'");
    metadata.validate();

    RepositoryCandidate candidate{
        metadata,
        RepositoryProvenance{repository_id, std::nullopt,
                             MetadataReference(metadata_key), std::nullopt}
    };

    if (!legacy.source.empty()) {
        SourceRecipe recipe{SchemaVersion(1), metadata.package_id, metadata.version};
        SourceRecipe::Source source{MetadataReference(legacy.source), std::nullopt, std::nullopt};
        if (!legacy.sha256.empty()) {
            std::string digest = legacy.sha256;
            if (!digest.starts_with("sha256:")) digest = "sha256:" + digest;
            source.digest = ContentDigest(std::move(digest));
        }
        recipe.sources.push_back(std::move(source));
        recipe.build_backend = legacy.build_system.empty() ? "autotools" : legacy.build_system;
        recipe.validate();
        candidate.source_recipe = std::move(recipe);
    }
    candidate.validate();
    return candidate;
}

} // namespace

RepositorySnapshot LocalRepository::load_snapshot() const
{
    std::error_code ec;
    const bool exists = std::filesystem::exists(config_.metadata_directory, ec);
    if (ec) {
        throw RepositoryError(RepositoryErrorCode::InvalidConfiguration,
                              "Cannot inspect local repository directory: " + ec.message());
    }
    if (!exists) {
        throw RepositoryError(RepositoryErrorCode::MetadataMissing,
                              "Local repository metadata directory does not exist");
    }
    if (!std::filesystem::is_directory(config_.metadata_directory, ec) || ec) {
        throw RepositoryError(RepositoryErrorCode::InvalidConfiguration,
                              "Local repository metadata path is not a readable directory");
    }

    try {
        athena::repository::Repository legacy;
        std::vector<std::filesystem::path> metadata_files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(config_.metadata_directory)) {
            if (entry.is_regular_file() && entry.path().filename() == "package.toml") {
                metadata_files.push_back(entry.path());
            }
        }
        std::sort(metadata_files.begin(), metadata_files.end());
        for (const auto& file : metadata_files) legacy.load_package(file);
        std::vector<RepositoryCandidate> candidates;
        candidates.reserve(legacy.index().packages().size());
        std::set<std::pair<std::string, std::string>> identities;

        // The legacy index currently retains package values but not source
        // paths. Deterministic metadata references therefore use a stable
        // package key, independent of physical repository location.
        for (const auto& package : legacy.index().packages()) {
            if (!identities.emplace(package.name, package.version).second) {
                throw RepositoryError(RepositoryErrorCode::InvalidCandidate,
                                      "Duplicate package identity and version in local repository");
            }
            const auto key = "packages/" + package.name + "/" + package.version + "/package.toml";
            try {
                candidates.push_back(convert_package(package, key, config_.repository_id));
            } catch (const RepositoryError&) {
                throw;
            } catch (const std::exception& error) {
                throw RepositoryError(RepositoryErrorCode::InvalidCandidate,
                                      "Invalid repository candidate '" + package.name + "': " + error.what());
            }
        }

        // Snapshot metadata digest excludes revision and physical paths. Use
        // it as the local deterministic revision, then build the final value.
        RepositorySnapshot seed = [&] {
            try {
                return RepositorySnapshot::create(
                    config_.repository_id, RepositoryRevision("local:pending"), candidates);
            } catch (const std::exception& error) {
                throw RepositoryError(RepositoryErrorCode::SnapshotFailure, error.what());
            }
        }();
        const RepositoryRevision revision("local:" + seed.metadata_digest().value());
        for (auto& candidate : candidates) candidate.provenance.revision = revision;
        try {
            return RepositorySnapshot::create(config_.repository_id, revision, std::move(candidates));
        } catch (const std::exception& error) {
            throw RepositoryError(RepositoryErrorCode::SnapshotFailure, error.what());
        }
    } catch (const RepositoryError&) {
        throw;
    } catch (const athena::package::PackageSchemaError& error) {
        throw RepositoryError(RepositoryErrorCode::UnsupportedSchema, error.what());
    } catch (const std::filesystem::filesystem_error& error) {
        throw RepositoryError(RepositoryErrorCode::MetadataMissing, error.what());
    } catch (const std::exception& error) {
        const std::string message = error.what();
        if (message.find("Invalid or unsupported") != std::string::npos ||
            message.find("schema") != std::string::npos) {
            throw RepositoryError(RepositoryErrorCode::UnsupportedSchema, message);
        }
        if (message.find("Invalid repository candidate") != std::string::npos) {
            throw RepositoryError(RepositoryErrorCode::InvalidCandidate, message);
        }
        throw RepositoryError(RepositoryErrorCode::MetadataInvalid, message);
    }
}

RepositoryRevision LocalRepository::revision() const
{
    return load_snapshot().revision();
}

std::vector<RepositoryCandidate> LocalRepository::query(const RepositoryRequest& request) const
{
    try {
        request.validate();
    } catch (const std::exception& error) {
        throw RepositoryError(RepositoryErrorCode::InvalidConfiguration,
                              std::string("Invalid repository request: ") + error.what());
    }
    if (std::find(request.repository_ids.begin(), request.repository_ids.end(),
                  config_.repository_id) == request.repository_ids.end()) {
        return {};
    }
    const auto snapshot = load_snapshot();
    const auto referenced = snapshot.candidates_with_snapshot_reference();
    std::vector<RepositoryCandidate> result;
    for (const auto& candidate : referenced) {
        if (std::find(request.package_ids.begin(), request.package_ids.end(),
                      candidate.package.package_id) != request.package_ids.end()) {
            result.push_back(candidate);
        }
    }
    return result;
}

} // namespace athena::core
