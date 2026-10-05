#pragma once

#include "install_plan.hpp"

namespace athena::core
{

enum class ResolutionErrorCode
{
    CapabilityUnsatisfied,
    NoProvider,
    Conflict,
    ProviderConflict,
    FeatureUnsatisfied,
    InvalidRequest,
    InvalidSelector,
    UnknownPackageRoot,
    NoCapabilityProvider,
    NoFeatureProvider,
    IncompatibleRootVersion,
    InvalidRepositoryRule,
    UnsatisfiedSystemPolicy,
    IncompatibleRoots,
    ResolutionFailure,
    InvalidSnapshot,
    InvalidCandidate,
    InvalidDependencyMetadata,
    CandidateSnapshotMismatch
};

class ResolutionError : public std::runtime_error
{
  public:
    ResolutionError(ResolutionErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code)
    {
    }
    ResolutionErrorCode code() const noexcept { return code_; }

  private:
    ResolutionErrorCode code_;
};

// Pure resolution over supplied immutable values. No installation or repository I/O.
ResolutionResult resolve_request(const InstallRequest& request);

} // namespace athena::core
