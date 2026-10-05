#include "resolver_snapshot_adapter.hpp"

namespace athena::repository {

RepositoryIndex adapt_snapshots(
    const std::vector<athena::core::RepositorySnapshot>& snapshots)
{
    RepositoryIndex result;
    for (const auto& snapshot : snapshots) {
        try {
            snapshot.validate();
        } catch (const std::exception& error) {
            throw ResolverSnapshotAdapterError(
                ResolverSnapshotAdapterErrorCode::InvalidSnapshot,
                "Invalid repository snapshot: " + std::string(error.what()));
        }

        const auto snapshot_ref = snapshot.reference();
        for (const auto& candidate : snapshot.candidates()) {
            try {
                result.add_candidate(ResolverCandidate::from_public(candidate, snapshot_ref));
            } catch (const std::exception& error) {
                throw ResolverSnapshotAdapterError(
                    ResolverSnapshotAdapterErrorCode::InvalidCandidate,
                    "Invalid candidate in repository snapshot " + snapshot.id().value() +
                    ": " + std::string(error.what()));
            }
        }
    }
    return result;
}

athena::dependency::Environments adapt_environments(
    const athena::core::TargetEnvironment& target,
    const athena::core::ResolutionEnvironments& environments)
{
    const auto convert = [](const athena::core::TargetEnvironment& environment) {
        return athena::dependency::Environment{
            environment.platform, environment.architecture, environment.abi};
    };
    return {
        convert(environments.host),
        convert(environments.build),
        convert(target)
    };
}

} // namespace athena::repository
