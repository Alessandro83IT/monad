#pragma once

#include "repository.hpp"

#include <map>
#include <vector>

namespace athena::core {

// Version ordering belongs to a repository ecosystem. Unknown is deliberately
// representable so deserialized/future policies fail with a typed diagnostic.
enum class VersionScheme { Athena, Arch, Unsupported };

struct RepositorySelectionRule {
    VersionScheme version_scheme = VersionScheme::Athena;
    int priority = 0; // Smaller values have higher priority.
    bool operator==(const RepositorySelectionRule&) const = default;
};

struct RepositorySelectionRequest {
    std::vector<PackageId> package_ids;
    std::vector<RepositorySnapshot> snapshots;
    std::map<RepositoryId, RepositorySelectionRule> repositories;
    TargetEnvironment target;
    bool operator==(const RepositorySelectionRequest&) const = default;
};

enum class SelectionErrorCode {
    UnsupportedSchema, InvalidSnapshot, InvalidCandidate,
    UnsupportedVersionScheme, IncompatibleTarget,
    DuplicateCandidate, ConflictingMetadata, NoEligibleCandidate
};

class SelectionError : public std::runtime_error {
public:
    SelectionError(SelectionErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}
    SelectionErrorCode code() const noexcept { return code_; }
private:
    SelectionErrorCode code_;
};

struct RepositorySelectionResponse {
    std::vector<RepositoryCandidate> selected; // In request package order.
    bool operator==(const RepositorySelectionResponse&) const = default;
};

// Shared eligibility and deterministic ranking used by selection and resolution.
bool candidate_matches_target(const RepositoryCandidate&, const TargetEnvironment&);
bool candidate_precedes(const RepositoryCandidate&, const RepositorySnapshotRef&,
                        const RepositorySelectionRule&,
                        const RepositoryCandidate&, const RepositorySnapshotRef&,
                        const RepositorySelectionRule&);
void validate_repository_rule(const RepositorySelectionRule&);

// Selects the highest version per requested PackageId; repository priority
// breaks equivalent-version ties. No I/O or process state is consulted.
RepositorySelectionResponse select_candidates(const RepositorySelectionRequest& request);

} // namespace athena::core
