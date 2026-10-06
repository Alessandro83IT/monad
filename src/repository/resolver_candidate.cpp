// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "resolver_candidate.hpp"

#include "../dependency/parser.hpp"

namespace athena::repository {
namespace {

athena::dependency::DependencyKind to_internal(athena::core::DependencyRole role)
{
    using Public = athena::core::DependencyRole;
    using Internal = athena::dependency::DependencyKind;
    switch (role) {
    case Public::Runtime: return Internal::Runtime;
    case Public::Build: return Internal::Build;
    case Public::Test: return Internal::Test;
    case Public::Optional: return Internal::Optional;
    case Public::Development: return Internal::Development;
    case Public::Propagated: return Internal::Propagated;
    }
    throw ResolverCandidateError(ResolverCandidateErrorCode::InvalidMetadata,
                                 "Unknown public dependency role");
}

athena::dependency::DependencyContext to_internal(athena::core::DependencyTargetContext context)
{
    using Public = athena::core::DependencyTargetContext;
    using Internal = athena::dependency::DependencyContext;
    switch (context) {
    case Public::Host: return Internal::Host;
    case Public::Build: return Internal::Build;
    case Public::Target: return Internal::Target;
    }
    throw ResolverCandidateError(ResolverCandidateErrorCode::InvalidMetadata,
                                 "Unknown public dependency context");
}

athena::dependency::Dependency parse_dependency(const std::string& expression)
{
    try {
        return athena::dependency::parse_dependency(expression);
    } catch (const std::exception& error) {
        throw ResolverCandidateError(ResolverCandidateErrorCode::InvalidMetadata,
            "Invalid repository dependency expression: " + std::string(error.what()));
    }
}

} // namespace

ResolverCandidate ResolverCandidate::legacy(const athena::package::Package& package)
{
    ResolverCandidate result;
    static_cast<athena::package::Package&>(result) = package;
    return result;
}

ResolverCandidate ResolverCandidate::from_public(
    const athena::core::RepositoryCandidate& candidate,
    const athena::core::RepositorySnapshotRef& snapshot_ref)
{
    try {
        candidate.validate();
        snapshot_ref.validate();
    } catch (const std::exception& error) {
        throw ResolverCandidateError(ResolverCandidateErrorCode::InvalidCandidate, error.what());
    }

    if (candidate.provenance.repository_id != snapshot_ref.repository_id ||
        (candidate.provenance.revision && *candidate.provenance.revision != snapshot_ref.revision) ||
        (candidate.provenance.snapshot && *candidate.provenance.snapshot != snapshot_ref) ||
        !snapshot_ref.snapshot_id) {
        throw ResolverCandidateError(ResolverCandidateErrorCode::SnapshotMismatch,
            "Repository candidate identity does not match its snapshot reference");
    }

    ResolverCandidate result;
    auto& package = static_cast<athena::package::Package&>(result);
    const auto& metadata = candidate.package;
    package.name = metadata.package_id.value();
    package.version = metadata.version.value();
    package.description = metadata.description;
    package.provides.capabilities = metadata.capabilities;
    package.provides.features = metadata.features;
    package.platforms = metadata.platforms;
    package.architectures = metadata.architectures;
    package.abis = metadata.abis;

    for (const auto& expression : metadata.dependencies)
        package.dependencies.push_back(parse_dependency(expression));
    for (const auto& expression : metadata.conflicts)
        package.conflicts.push_back(parse_dependency(expression));
    for (const auto& [feature, dependencies] : metadata.feature_dependencies) {
        auto& translated = package.feature_dependencies[feature];
        translated.reserve(dependencies.size());
        for (const auto& dependency : dependencies) {
            const auto parsed = parse_dependency(dependency.expression.text);
            translated.push_back({parsed.expression,
                                  to_internal(dependency.role),
                                  to_internal(dependency.context),
                                  dependency.enabled});
        }
    }

    result.public_candidate = candidate;
    result.snapshot = snapshot_ref;
    result.identity = athena::core::CandidateIdentity{
        metadata.package_id,
        metadata.version,
        candidate.provenance.repository_id,
        snapshot_ref,
        candidate.provenance
    };
    result.validate();
    return result;
}

void ResolverCandidate::validate() const
{
    if (!identity) {
        if (public_candidate || snapshot)
            throw ResolverCandidateError(ResolverCandidateErrorCode::InvalidCandidate,
                "Legacy resolver candidate cannot contain partial public identity");
        return;
    }
    if (!public_candidate || !snapshot)
        throw ResolverCandidateError(ResolverCandidateErrorCode::InvalidCandidate,
            "Identity-qualified resolver candidate is missing its source candidate or snapshot");

    try {
        public_candidate->validate();
        snapshot->validate();
    } catch (const std::exception& error) {
        throw ResolverCandidateError(ResolverCandidateErrorCode::InvalidCandidate, error.what());
    }

    const auto& source = *public_candidate;
    if (name != identity->package.value() || version != identity->version.value() ||
        source.package.package_id != identity->package || source.package.version != identity->version ||
        source.provenance.repository_id != identity->repository ||
        snapshot->repository_id != identity->repository ||
        !snapshot->snapshot_id || identity->snapshot != *snapshot ||
        identity->provenance != source.provenance ||
        (source.provenance.revision && *source.provenance.revision != snapshot->revision) ||
        (source.provenance.snapshot && *source.provenance.snapshot != *snapshot)) {
        throw ResolverCandidateError(ResolverCandidateErrorCode::SnapshotMismatch,
            "Resolver candidate fields disagree with its complete repository identity");
    }
}

} // namespace athena::repository
