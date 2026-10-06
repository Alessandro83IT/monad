// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <athena/core/install_plan.hpp>

namespace athena::core::detail
{
// Field-delimited keys retain recursive constraints and full immutable identity.
using StableKey = std::vector<std::string>;
inline void append(StableKey& a, const StableKey& b) { a.insert(a.end(), b.begin(), b.end()); }
inline StableKey key(const VersionConstraint& c)
{
    StableKey k{std::to_string(static_cast<int>(c.kind)),
                std::to_string(static_cast<int>(c.comparison)), c.version,
                std::to_string(c.operands.size())};
    for (const auto& child : c.operands)
        append(k, key(child));
    return k;
}
inline StableKey key(const DependencySelector& s)
{
    StableKey k{std::to_string(static_cast<int>(s.kind)), s.name};
    append(k, key(s.constraint));
    return k;
}
inline StableKey key(const RepositorySnapshotRef& s)
{
    return {s.repository_id.value(),
            s.revision.value(),
            std::to_string(s.schema_version.value),
            s.metadata_digest ? "1" : "0",
            s.metadata_digest ? s.metadata_digest->value() : "",
            s.snapshot_id ? "1" : "0",
            s.snapshot_id ? s.snapshot_id->value() : ""};
}
inline StableKey key(const CandidateIdentity& c)
{
    StableKey k{c.package.value(), c.version.value(), c.repository.value()};
    append(k, key(c.snapshot));
    append(k, {c.provenance.repository_id.value(), c.provenance.revision ? "1" : "0",
               c.provenance.revision ? c.provenance.revision->value() : "",
               c.provenance.metadata_reference ? "1" : "0",
               c.provenance.metadata_reference ? c.provenance.metadata_reference->value() : "",
               c.provenance.snapshot ? "1" : "0"});
    if (c.provenance.snapshot)
        append(k, key(*c.provenance.snapshot));
    return k;
}
inline StableKey key(const InstallReason& r)
{
    StableKey k{std::to_string(static_cast<int>(r.kind)), r.explicit_target ? "1" : "0"};
    if (r.explicit_target)
        append(k, key(r.explicit_target->selector));
    k.push_back(r.package ? "1" : "0");
    if (r.package)
        append(k, key(*r.package));
    append(k, {r.profile, r.feature, r.policy ? "1" : "0", r.policy ? r.policy->value() : ""});
    return k;
}
inline StableKey key(const PackageSelection& s) { return key(s.identity()); }
inline StableKey key(const CapabilityBinding& b)
{
    auto k = key(b.selector);
    append(k, key(b.provider));
    return k;
}
inline StableKey key(const ConfiguredFeature& f)
{
    return {f.profile, f.feature, f.enabled ? "1" : "0"};
}
inline StableKey key(const OfferedFeature& f)
{
    StableKey k{f.feature};
    append(k, key(f.provider));
    return k;
}
inline StableKey key(const ActivatedFeature& f)
{
    StableKey k{f.profile, f.feature};
    append(k, key(f.owner));
    return k;
}
inline StableKey key(const DependencyEdge& e)
{
    auto k = key(e.source);
    append(k, key(e.target));
    k.push_back(e.provider ? "1" : "0");
    if (e.provider)
        append(k, key(*e.provider));
    append(k, {std::to_string(static_cast<int>(e.role)),
               std::to_string(static_cast<int>(e.context)), e.reason ? "1" : "0"});
    if (e.reason)
        append(k, key(*e.reason));
    return k;
}
inline StableKey key(const NegativeConstraint& n)
{
    auto k = key(n.declared_by);
    append(k, {n.expression.text, std::to_string(static_cast<int>(n.role)),
               std::to_string(static_cast<int>(n.context)),
               std::to_string(static_cast<int>(n.source.kind)), n.source.feature});
    return k;
}
inline StableKey key(const SystemPolicySelection& p)
{
    StableKey k{p.policy.value()};
    append(k, key(p.selector));
    append(k, key(p.candidate));
    return k;
}
template <class T> void canonicalize(std::vector<T>& values)
{
    std::sort(values.begin(), values.end(),
              [](const auto& a, const auto& b) { return key(a) < key(b); });
    values.erase(std::unique(values.begin(), values.end()), values.end());
}
inline void canonicalize(ResolutionResult& r)
{
    for (auto& s : r.selections)
        canonicalize(s.reasons);
    std::sort(r.selections.begin(), r.selections.end(),
              [](const auto& a, const auto& b) { return key(a) < key(b); });
    canonicalize(r.capability_bindings);
    canonicalize(r.configured_features);
    canonicalize(r.offered_features);
    canonicalize(r.activated_features);
    canonicalize(r.edges);
    canonicalize(r.negative_constraints);
    canonicalize(r.system_policy_selections);
}
} // namespace athena::core::detail
