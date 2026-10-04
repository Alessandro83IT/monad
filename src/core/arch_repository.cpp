#include <athena/core/repository.hpp>

#include "../dependency/serializer.hpp"
#include "../dependency/parser.hpp"
#include "../repository/arch_repository.hpp"
#include "../version/version.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <set>

namespace athena::core {
namespace {

bool has_space(const std::string& value)
{
    return std::any_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
}

std::string first(const std::map<std::string, std::vector<std::string>>& fields,
                  const std::string& key)
{
    const auto it = fields.find(key);
    return it == fields.end() || it->second.empty() ? std::string{} : it->second.front();
}

std::vector<std::string> values(
    const std::map<std::string, std::vector<std::string>>& fields,
    const std::string& key)
{
    const auto it = fields.find(key);
    return it == fields.end() ? std::vector<std::string>{} : it->second;
}

std::uint64_t parse_size(const std::string& value)
{
    std::uint64_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) {
        throw RepositoryError(RepositoryErrorCode::InvalidCandidate,
                              "Invalid Arch package compressed size");
    }
    return result;
}

std::string stable_metadata_reference(const ArchRepositoryConfig& config,
                                      const std::string& package,
                                      const std::string& version)
{
    return "arch/" + config.repository_name + "/" + package + "/" + version + "/desc";
}

RepositoryCandidate convert_arch_package(const athena::package::Package& source,
                                         const ArchRepositoryConfig& config)
{
    const auto& fields = source.arch_metadata;
    const auto architecture = first(fields, "ARCH");
    const auto filename = first(fields, "FILENAME");
    if (source.name.empty() || source.version.empty() || architecture.empty() || filename.empty()) {
        throw RepositoryError(RepositoryErrorCode::InvalidCandidate,
                              "Arch repository entry is missing NAME, VERSION, ARCH or FILENAME");
    }
    if (architecture != "any" && architecture != config.target_architecture) {
        throw RepositoryError(RepositoryErrorCode::InvalidCandidate,
                              "Arch candidate is incompatible with configured target architecture");
    }

    PackageMetadata package{
        PackageId(source.name), PackageVersion(source.version), SchemaVersion(1),
        source.name, source.description
    };
    package.architectures.push_back(architecture);

    // PackageMetadata currently has an untyped dependency list. Only Arch
    // runtime dependencies enter it; the other roles stay in the imported
    // binary's raw ecosystem metadata until M1.7-M1.9 define role-aware APIs.
    for (const auto& dependency : source.dependencies) {
        if (dependency.kind == athena::dependency::DependencyKind::Runtime) {
            package.dependencies.push_back(
                athena::dependency::serialize_dependency(dependency));
        }
    }
    package.capabilities = source.provides.capabilities;
    for (const auto& raw : values(fields, "CONFLICTS")) {
        try {
            package.conflicts.push_back(athena::dependency::serialize_dependency(
                athena::dependency::parse_dependency(raw)));
        } catch (const std::exception& error) {
            throw RepositoryError(RepositoryErrorCode::InvalidCandidate,
                                  "Invalid Arch conflict metadata: " + std::string(error.what()));
        }
    }
    package.validate();

    auto arch_fields = fields;
    arch_fields["ATHENA_REPOSITORY"] = {config.repository_name};
    ImportedBinaryMetadata imported{
        SchemaVersion(1), package, MetadataReference(source.source),
        ImportedBinaryFormat::ArchPackage
    };
    if (!source.sha256.empty()) {
        const auto digest = source.sha256.starts_with("sha256:")
            ? source.sha256 : "sha256:" + source.sha256;
        imported.original_digest = ContentDigest(digest);
    }
    const auto size = first(fields, "CSIZE");
    if (!size.empty()) imported.size_bytes = parse_size(size);
    imported.repository_origin = RepositoryProvenance{
        config.repository_id, std::nullopt,
        MetadataReference(stable_metadata_reference(config, source.name, source.version)),
        std::nullopt
    };
    imported.ecosystem_fields = std::move(arch_fields);
    imported.validate();

    RepositoryCandidate candidate{
        std::move(package),
        RepositoryProvenance{
            config.repository_id, std::nullopt,
            MetadataReference(stable_metadata_reference(config, source.name, source.version)),
            std::nullopt
        },
        std::nullopt, std::move(imported), std::nullopt
    };
    candidate.validate();
    return candidate;
}

} // namespace

void ArchRepositoryConfig::validate() const
{
    if (repository_id.empty() || repository_name.empty() || has_space(repository_name)) {
        throw RepositoryError(RepositoryErrorCode::InvalidConfiguration,
                              "Arch repository requires a valid RepositoryId and repository name");
    }
    if (database_file.empty() || !database_file.is_absolute()) {
        throw RepositoryError(RepositoryErrorCode::InvalidConfiguration,
                              "Arch database_file must be an explicit absolute path");
    }
    if (target_architecture.empty() || has_space(target_architecture)) {
        throw RepositoryError(RepositoryErrorCode::InvalidConfiguration,
                              "Arch target architecture is missing or invalid");
    }
    const bool valid_url = package_base_url.starts_with("https://") ||
                           package_base_url.starts_with("http://") ||
                           package_base_url.starts_with("file://");
    if (!valid_url || has_space(package_base_url)) {
        throw RepositoryError(RepositoryErrorCode::InvalidConfiguration,
                              "Arch package_base_url must be an explicit http(s) or file URL");
    }
}

ArchRepository::ArchRepository(ArchRepositoryConfig config) : config_(std::move(config))
{
    config_.validate();
}

const RepositoryId& ArchRepository::repository_id() const noexcept
{
    return config_.repository_id;
}

RepositorySnapshot ArchRepository::load_snapshot() const
{
    std::error_code ec;
    if (!std::filesystem::exists(config_.database_file, ec) || ec) {
        throw RepositoryError(RepositoryErrorCode::MetadataMissing,
                              "Arch repository database is unavailable");
    }
    if (!std::filesystem::is_regular_file(config_.database_file, ec) || ec) {
        throw RepositoryError(RepositoryErrorCode::InvalidConfiguration,
                              "Arch repository database path is not a regular file");
    }
    const auto filename = config_.database_file.filename().string();
    const bool supported_format = filename.ends_with(".db") ||
        filename.ends_with(".db.tar") || filename.ends_with(".db.tar.zst") ||
        filename.ends_with(".db.tar.gz") || filename.ends_with(".db.tar.xz");
    if (!supported_format) {
        throw RepositoryError(RepositoryErrorCode::UnsupportedSchema,
                              "Unsupported Arch repository database format");
    }

    try {
        athena::repository::ArchRepository legacy(config_.database_file,
                                                   config_.package_base_url);
        legacy.load();
        std::vector<RepositoryCandidate> candidates;
        std::set<std::pair<std::string, std::string>> identities;
        for (const auto& item : legacy.index().packages()) {
            const auto arch = first(item.arch_metadata, "ARCH");
            if (arch.empty()) {
                throw RepositoryError(RepositoryErrorCode::InvalidCandidate,
                                      "Arch repository entry is missing ARCH");
            }
            if (arch != "any" && arch != config_.target_architecture) continue;
            if (!identities.emplace(item.name, item.version).second) {
                throw RepositoryError(RepositoryErrorCode::InvalidCandidate,
                                      "Duplicate Arch package identity and version");
            }
            try {
                candidates.push_back(convert_arch_package(item, config_));
            } catch (const RepositoryError&) {
                throw;
            } catch (const std::exception& error) {
                throw RepositoryError(RepositoryErrorCode::InvalidCandidate,
                                      "Invalid Arch package metadata: " + std::string(error.what()));
            }
        }

        // Arch repository desc records have no native catalog revision in
        // this backend. The canonical candidate digest is the deterministic
        // local revision; it excludes physical database paths and timestamps.
        RepositorySnapshot seed = [&] {
            try {
                return RepositorySnapshot::create(
                    config_.repository_id, RepositoryRevision("arch:pending"), candidates);
            } catch (const std::exception& error) {
                throw RepositoryError(RepositoryErrorCode::SnapshotFailure, error.what());
            }
        }();
        const RepositoryRevision revision("arch:" + seed.metadata_digest().value());
        for (auto& candidate : candidates) candidate.provenance.revision = revision;
        try {
            return RepositorySnapshot::create(config_.repository_id, revision,
                                              std::move(candidates));
        } catch (const std::exception& error) {
            throw RepositoryError(RepositoryErrorCode::SnapshotFailure, error.what());
        }
    } catch (const RepositoryError&) {
        throw;
    } catch (const athena::repository::ArchPackageMetadataError& error) {
        throw RepositoryError(RepositoryErrorCode::InvalidCandidate,
                              "Invalid Arch package metadata: " + std::string(error.what()));
    } catch (const std::exception& error) {
        throw RepositoryError(RepositoryErrorCode::MetadataInvalid,
                              "Invalid Arch repository metadata: " + std::string(error.what()));
    }
}

RepositoryRevision ArchRepository::revision() const
{
    return load_snapshot().revision();
}

std::vector<RepositoryCandidate> ArchRepository::query(const RepositoryRequest& request) const
{
    try {
        request.validate();
    } catch (const std::exception& error) {
        throw RepositoryError(RepositoryErrorCode::InvalidConfiguration,
                              "Invalid repository request: " + std::string(error.what()));
    }
    if (std::find(request.repository_ids.begin(), request.repository_ids.end(),
                  config_.repository_id) == request.repository_ids.end()) return {};

    const auto snapshot = load_snapshot();
    auto candidates = snapshot.candidates_with_snapshot_reference();
    std::erase_if(candidates, [&](const RepositoryCandidate& candidate) {
        return std::find(request.package_ids.begin(), request.package_ids.end(),
                         candidate.package.package_id) == request.package_ids.end();
    });
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        if (left.package.package_id != right.package.package_id)
            return left.package.package_id < right.package.package_id;
        return athena::version::less(left.package.version.value(), right.package.version.value());
    });
    return candidates;
}

} // namespace athena::core
