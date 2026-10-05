#include <athena/core/selection.hpp>

#include "../version/version.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <tuple>

namespace athena::core {
namespace {

bool matches(const std::vector<std::string>& values, const std::string& value)
{
    return values.empty() || std::find(values.begin(), values.end(), value) != values.end();
}

// Athena native scheme compares dot-separated numeric components, with a
// lexical fallback for explicitly supported alphanumeric native versions.
int compare_native(const std::string& a, const std::string& b)
{
    auto numeric_parts = [](const std::string& s) {
        std::vector<std::string> out;
        std::size_t start = 0;
        while (start <= s.size()) {
            const auto end = s.find('.', start);
            const auto part = s.substr(start, end == std::string::npos ? end : end - start);
            if (part.empty()) throw std::invalid_argument("Malformed native version");
            if (!std::all_of(part.begin(), part.end(), [](unsigned char c) { return std::isdigit(c); }))
                return std::vector<std::string>{};
            auto first = part.find_first_not_of('0');
            out.push_back(first == std::string::npos ? "0" : part.substr(first));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return out;
    };
    const auto x = numeric_parts(a), y = numeric_parts(b);
    if (x.empty() || y.empty()) return a < b ? -1 : a > b ? 1 : 0;
    const auto n = std::max(x.size(), y.size());
    for (std::size_t i = 0; i < n; ++i) {
        const std::string p = i < x.size() ? x[i] : "0";
        const std::string q = i < y.size() ? y[i] : "0";
        if (p.size() != q.size()) return p.size() < q.size() ? -1 : 1;
        if (p != q) return p < q ? -1 : 1;
    }
    return 0;
}

int compare_version(VersionScheme scheme, const PackageVersion& a, const PackageVersion& b)
{
    switch (scheme) {
    case VersionScheme::Athena: return compare_native(a.value(), b.value());
    case VersionScheme::Arch:
        if (athena::version::less(a.value(), b.value())) return -1;
        if (athena::version::less(b.value(), a.value())) return 1;
        return 0;
    case VersionScheme::Unsupported:
        throw SelectionError(SelectionErrorCode::UnsupportedVersionScheme, "Unsupported repository version scheme");
    }
    throw SelectionError(SelectionErrorCode::UnsupportedVersionScheme, "Unknown repository version scheme");
}

using Identity = std::tuple<std::string, std::string, std::string>;

} // namespace

RepositorySelectionResponse select_candidates(const RepositorySelectionRequest& request)
{
    if (request.package_ids.empty() || request.snapshots.empty())
        throw SelectionError(SelectionErrorCode::NoEligibleCandidate, "Selection requires packages and snapshots");
    std::set<std::string> requested;
    for (const auto& id : request.package_ids) {
        if (id.empty() || !requested.insert(id.value()).second)
            throw SelectionError(SelectionErrorCode::InvalidCandidate, "Selection contains an empty or duplicate PackageId");
    }

    struct Entry { RepositoryCandidate candidate; const RepositorySnapshot* snapshot; RepositorySelectionRule rule; };
    std::map<std::string, std::map<Identity, Entry>> by_package;
    std::set<std::string> target_rejected;
    std::map<RepositoryId, const RepositorySnapshot*> seen_snapshots;
    for (const auto& snapshot : request.snapshots) {
        if (snapshot.schema_version().value != current_schema_version)
            throw SelectionError(SelectionErrorCode::UnsupportedSchema, "Unsupported repository snapshot schema");
        try { snapshot.validate(); }
        catch (const std::exception& e) { throw SelectionError(SelectionErrorCode::InvalidSnapshot, e.what()); }
        const auto rule_it = request.repositories.find(snapshot.repository_id());
        if (rule_it == request.repositories.end())
            throw SelectionError(SelectionErrorCode::InvalidSnapshot, "Snapshot has no repository selection rule");
        if (rule_it->second.version_scheme == VersionScheme::Unsupported)
            throw SelectionError(SelectionErrorCode::UnsupportedVersionScheme, "Unsupported repository version scheme");
        const auto prior_snapshot = seen_snapshots.find(snapshot.repository_id());
        if (prior_snapshot != seen_snapshots.end() && prior_snapshot->second->id() == snapshot.id()) continue;
        seen_snapshots[snapshot.repository_id()] = &snapshot;

        for (const auto& candidate : snapshot.candidates()) {
            try { candidate.validate(); }
            catch (const std::exception& e) { throw SelectionError(SelectionErrorCode::InvalidCandidate, e.what()); }
            if (candidate.provenance.repository_id != snapshot.repository_id() ||
                (candidate.provenance.revision && *candidate.provenance.revision != snapshot.revision()) ||
                (candidate.provenance.snapshot && *candidate.provenance.snapshot != snapshot.reference()))
                throw SelectionError(SelectionErrorCode::InvalidCandidate, "Candidate provenance disagrees with source snapshot");
            if (!requested.contains(candidate.package.package_id.value())) continue;
            if (!matches(candidate.package.platforms, request.target.platform) ||
                !matches(candidate.package.architectures, request.target.architecture) ||
                !matches(candidate.package.abis, request.target.abi)) {
                target_rejected.insert(candidate.package.package_id.value());
                continue;
            }
            const Identity identity{candidate.package.package_id.value(), candidate.package.version.value(),
                                    candidate.provenance.repository_id.value()};
            auto& identities = by_package[candidate.package.package_id.value()];
            auto existing = identities.find(identity);
            if (existing != identities.end()) {
                if (existing->second.candidate != candidate)
                    throw SelectionError(SelectionErrorCode::ConflictingMetadata, "Conflicting metadata for canonical candidate identity");
                continue; // Exact duplicate from repeated/equivalent snapshot.
            }
            identities.emplace(identity, Entry{candidate, &snapshot, rule_it->second});
        }
    }

    RepositorySelectionResponse response;
    for (const auto& package_id : request.package_ids) {
        auto it = by_package.find(package_id.value());
        if ((it == by_package.end() || it->second.empty()) && target_rejected.contains(package_id.value()))
            throw SelectionError(SelectionErrorCode::IncompatibleTarget, "Candidates are incompatible with target for " + package_id.value());
        if (it == by_package.end() || it->second.empty())
            throw SelectionError(SelectionErrorCode::NoEligibleCandidate, "No eligible candidate for " + package_id.value());
        std::vector<const Entry*> entries;
        for (const auto& [identity, entry] : it->second) { (void)identity; entries.push_back(&entry); }
        std::sort(entries.begin(), entries.end(), [](const Entry* a, const Entry* b) {
            // Versions from different ecosystems are not comparable. Give
            // schemes a fixed enum order, then compare only within a scheme.
            if (a->rule.version_scheme != b->rule.version_scheme)
                return a->rule.version_scheme < b->rule.version_scheme;
            const auto version_order = compare_version(a->rule.version_scheme, a->candidate.package.version, b->candidate.package.version);
            if (version_order != 0) return version_order > 0;
            if (a->rule.priority != b->rule.priority) return a->rule.priority < b->rule.priority;
            if (a->candidate.provenance.repository_id != b->candidate.provenance.repository_id)
                return a->candidate.provenance.repository_id < b->candidate.provenance.repository_id;
            if (a->snapshot->metadata_digest() != b->snapshot->metadata_digest())
                return a->snapshot->metadata_digest() < b->snapshot->metadata_digest();
            return a->snapshot->id() < b->snapshot->id();
        });
        response.selected.push_back(entries.front()->candidate);
    }
    return response;
}

} // namespace athena::core
