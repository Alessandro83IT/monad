// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "resolver_snapshot_adapter.hpp"
#include "../dependency/parser.hpp"

namespace athena::repository
{

RepositoryIndex adapt_snapshots(const std::vector<athena::core::RepositorySnapshot>& snapshots)
{
    RepositoryIndex result;
    for (const auto& snapshot : snapshots)
    {
        try
        {
            snapshot.validate();
        }
        catch (const std::exception& error)
        {
            // Preserve precise input diagnostics when snapshot validation failed.
            // A corrupt digest remains InvalidSnapshot; malformed declarations
            // and mismatched provenance have their own structured categories.
            for (const auto& candidate : snapshot.candidates())
            {
                const auto& provenance = candidate.provenance;
                if (provenance.repository_id != snapshot.repository_id() ||
                    (provenance.revision && *provenance.revision != snapshot.revision()) ||
                    provenance.snapshot)
                    throw ResolverSnapshotAdapterError(
                        ResolverSnapshotAdapterErrorCode::SnapshotMismatch, error.what());
                try
                {
                    for (const auto& text : candidate.package.dependencies)
                        (void)athena::dependency::parse_dependency(text);
                    for (const auto& text : candidate.package.conflicts)
                        (void)athena::dependency::parse_dependency(text);
                    for (const auto& [feature, dependencies] :
                         candidate.package.feature_dependencies)
                    {
                        (void)feature;
                        for (const auto& d : dependencies)
                            (void)athena::dependency::parse_dependency(d.expression.text);
                    }
                }
                catch (const std::exception& metadata_error)
                {
                    throw ResolverSnapshotAdapterError(
                        ResolverSnapshotAdapterErrorCode::InvalidMetadata, metadata_error.what());
                }
            }
            throw ResolverSnapshotAdapterError(ResolverSnapshotAdapterErrorCode::InvalidSnapshot,
                                               "Invalid repository snapshot: " +
                                                   std::string(error.what()));
        }

        const auto snapshot_ref = snapshot.reference();
        for (const auto& candidate : snapshot.candidates())
        {
            try
            {
                result.add_candidate(ResolverCandidate::from_public(candidate, snapshot_ref));
            }
            catch (const ResolverCandidateError& error)
            {
                const auto code = error.code() == ResolverCandidateErrorCode::SnapshotMismatch
                                      ? ResolverSnapshotAdapterErrorCode::SnapshotMismatch
                                  : error.code() == ResolverCandidateErrorCode::InvalidMetadata
                                      ? ResolverSnapshotAdapterErrorCode::InvalidMetadata
                                      : ResolverSnapshotAdapterErrorCode::InvalidCandidate;
                throw ResolverSnapshotAdapterError(code, error.what());
            }
            catch (const std::exception& error)
            {
                throw ResolverSnapshotAdapterError(
                    ResolverSnapshotAdapterErrorCode::InvalidCandidate,
                    "Invalid candidate in repository snapshot " + snapshot.id().value() + ": " +
                        std::string(error.what()));
            }
        }
    }
    return result;
}

athena::dependency::Environments
adapt_environments(const athena::core::TargetEnvironment& target,
                   const athena::core::ResolutionEnvironments& environments)
{
    const auto convert = [](const athena::core::TargetEnvironment& environment)
    {
        return athena::dependency::Environment{environment.platform, environment.architecture,
                                               environment.abi};
    };
    return {convert(environments.host), convert(environments.build), convert(target)};
}

} // namespace athena::repository
