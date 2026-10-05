#include "repository_index.hpp"

#include "../version/version.hpp"

#include <algorithm>
#include <tuple>

namespace athena::repository {
namespace {

auto identity_key(const ResolverCandidate& candidate)
{
    if (!candidate.identity)
        return std::tuple{false, std::string{}, std::string{}, std::string{}, std::string{}};
    const auto& identity = *candidate.identity;
    return std::tuple{
        true,
        identity.repository.value(),
        identity.snapshot.metadata_digest ? identity.snapshot.metadata_digest->value() : std::string{},
        identity.snapshot.snapshot_id ? identity.snapshot.snapshot_id->value() : std::string{},
        identity.provenance.metadata_reference ? identity.provenance.metadata_reference->value() : std::string{}
    };
}

bool candidate_less(const ResolverCandidate& left, const ResolverCandidate& right)
{
    if (left.name != right.name) return left.name < right.name;
    if (left.version != right.version)
        return athena::version::less(left.version, right.version);
    return identity_key(left) < identity_key(right);
}

template <typename Predicate>
std::vector<ResolverCandidate> matching(const std::vector<ResolverCandidate>& candidates,
                                        Predicate predicate)
{
    std::vector<ResolverCandidate> result;
    for (const auto& candidate : candidates)
        if (predicate(candidate)) result.push_back(candidate);
    return result;
}

} // namespace

void RepositoryIndex::add(const athena::package::Package& package)
{
    add_candidate(ResolverCandidate::legacy(package));
}

void RepositoryIndex::add_candidate(ResolverCandidate candidate)
{
    candidate.validate();
    if (candidate.identity) {
        const auto existing = std::find_if(candidates_.begin(), candidates_.end(), [&](const auto& value) {
            return value.identity && *value.identity == *candidate.identity;
        });
        if (existing != candidates_.end()) {
            if (existing->public_candidate != candidate.public_candidate ||
                existing->snapshot != candidate.snapshot) {
                throw ResolverCandidateError(ResolverCandidateErrorCode::InvalidCandidate,
                    "Conflicting metadata for an existing resolver candidate identity");
            }
            return;
        }
    }
    candidates_.push_back(std::move(candidate));
    std::sort(candidates_.begin(), candidates_.end(), candidate_less);
}

void RepositoryIndex::add_public_candidate(
    const athena::core::RepositoryCandidate& candidate,
    const athena::core::RepositorySnapshotRef& snapshot)
{
    add_candidate(ResolverCandidate::from_public(candidate, snapshot));
}

std::vector<ResolverCandidate> RepositoryIndex::find_candidates(const std::string& name) const
{
    return matching(candidates_, [&name](const auto& candidate) { return candidate.name == name; });
}

std::vector<ResolverCandidate> RepositoryIndex::find_capability_candidates(const std::string& capability) const
{
    return matching(candidates_, [&capability](const auto& candidate) {
        return std::find(candidate.provides.capabilities.begin(), candidate.provides.capabilities.end(), capability)
            != candidate.provides.capabilities.end();
    });
}

std::vector<ResolverCandidate> RepositoryIndex::find_feature_candidates(const std::string& feature) const
{
    return matching(candidates_, [&feature](const auto& candidate) {
        return std::find(candidate.provides.features.begin(), candidate.provides.features.end(), feature)
            != candidate.provides.features.end();
    });
}

std::vector<athena::package::Package> RepositoryIndex::find(const std::string& name) const
{
    std::vector<athena::package::Package> result;
    for (const auto& candidate : find_candidates(name)) result.push_back(candidate);
    return result;
}

std::vector<athena::package::Package> RepositoryIndex::find_capability(const std::string& capability) const
{
    std::vector<athena::package::Package> result;
    for (const auto& candidate : find_capability_candidates(capability)) result.push_back(candidate);
    return result;
}

std::vector<athena::package::Package> RepositoryIndex::find_feature(const std::string& feature) const
{
    std::vector<athena::package::Package> result;
    for (const auto& candidate : find_feature_candidates(feature)) result.push_back(candidate);
    return result;
}

bool RepositoryIndex::contains(const std::string& name) const
{
    return std::any_of(candidates_.begin(), candidates_.end(), [&name](const auto& candidate) {
        return candidate.name == name;
    });
}

std::vector<athena::package::Package> RepositoryIndex::packages() const
{
    std::vector<athena::package::Package> result;
    result.reserve(candidates_.size());
    for (const auto& candidate : candidates_) result.push_back(candidate);
    return result;
}

} // namespace athena::repository
