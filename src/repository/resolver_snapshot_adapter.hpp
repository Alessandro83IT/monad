#pragma once

#include "repository_index.hpp"
#include "../dependency/environment.hpp"
#include "../../include/athena/core/install_plan.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace athena::repository {

enum class ResolverSnapshotAdapterErrorCode {
    InvalidSnapshot,
    InvalidCandidate
};

class ResolverSnapshotAdapterError : public std::invalid_argument {
public:
    ResolverSnapshotAdapterError(ResolverSnapshotAdapterErrorCode code, std::string message)
        : std::invalid_argument(std::move(message)), code_(code) {}
    ResolverSnapshotAdapterErrorCode code() const noexcept { return code_; }
private:
    ResolverSnapshotAdapterErrorCode code_;
};

// Builds a transient resolver view from immutable public snapshots. The
// returned index owns adapted values only for the duration of resolution;
// each value retains its source RepositoryCandidate and exact snapshot ref.
RepositoryIndex adapt_snapshots(const std::vector<athena::core::RepositorySnapshot>& snapshots);

// Explicit public-to-private environment mapping for the later request-level
// resolver entry point. TargetEnvironment::triple has no private equivalent.
athena::dependency::Environments adapt_environments(
    const athena::core::TargetEnvironment& target,
    const athena::core::ResolutionEnvironments& environments);

} // namespace athena::repository
