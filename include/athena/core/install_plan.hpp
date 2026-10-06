// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "dependency.hpp"
#include "selection.hpp"

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace athena::core {

struct InstallTarget {
    DependencySelector selector;
    bool operator==(const InstallTarget&) const = default;
};

struct ResolutionEnvironments {
    TargetEnvironment host;
    TargetEnvironment build;
    bool operator==(const ResolutionEnvironments&) const = default;
};

struct SystemPolicyRequest {
    SystemPolicyId policy;
    std::vector<InstallTarget> required;
    bool operator==(const SystemPolicyRequest&) const = default;
};

// Fully qualified identity of one immutable repository record. Artifact and
// build-input identities live on RepositoryCandidate's optional payloads and
// are never synthesized by planning.
struct CandidateIdentity {
    PackageId package;
    PackageVersion version;
    RepositoryId repository;
    RepositorySnapshotRef snapshot;
    RepositoryProvenance provenance;
    bool operator==(const CandidateIdentity&) const = default;
};

struct InstallRequest {
    std::vector<InstallTarget> targets;
    TargetEnvironment target;
    ResolutionEnvironments environments;
    std::vector<RepositorySnapshot> snapshots;
    std::map<RepositoryId, RepositorySelectionRule> repositories;
    // Empty means the unnamed configuration; resolution preserves it exactly.
    std::string profile;
    std::map<std::string, bool> feature_states;
    std::vector<SystemPolicyRequest> system_policies;
    std::vector<DependencyRole> active_roles;
    bool optional_enabled = false;
    bool propagated_enabled = true;
};

enum class InstallReasonKind { ExplicitRequest, DependencyOf, EnabledByFeature, RequiredBySystemPolicy };
struct InstallReason {
    InstallReasonKind kind = InstallReasonKind::ExplicitRequest;
    std::optional<InstallTarget> explicit_target;
    // Selected declaring owner for DependencyOf and EnabledByFeature.
    std::optional<CandidateIdentity> package;
    std::string profile;
    std::string feature;
    std::optional<SystemPolicyId> policy;
    bool operator==(const InstallReason&) const = default;
};

struct PackageSelection {
    RepositoryCandidate candidate;
    RepositorySnapshotRef snapshot;
    std::vector<InstallReason> reasons;
    CandidateIdentity identity() const {
        return {candidate.package.package_id, candidate.package.version,
                candidate.provenance.repository_id, snapshot, candidate.provenance};
    }
    bool operator==(const PackageSelection&) const = default;
};

struct CapabilityBinding {
    DependencySelector selector;
    CandidateIdentity provider;
    bool operator==(const CapabilityBinding&) const = default;
};

struct ConfiguredFeature {
    std::string feature;
    std::string profile;
    bool enabled = false;
    bool operator==(const ConfiguredFeature&) const = default;
};
struct OfferedFeature {
    std::string feature;
    CandidateIdentity provider;
    bool operator==(const OfferedFeature&) const = default;
};
struct ActivatedFeature {
    std::string feature;
    std::string profile;
    CandidateIdentity owner;
    bool operator==(const ActivatedFeature&) const = default;
};

struct DependencyEdge {
    CandidateIdentity source;
    DependencySelector target;
    std::optional<CandidateIdentity> provider;
    DependencyRole role = DependencyRole::Runtime;
    DependencyTargetContext context = DependencyTargetContext::Target;
    std::optional<InstallReason> reason;
    bool operator==(const DependencyEdge&) const = default;
};

// `expression` is the canonical public dependency-language expression whose
// negation is required. It can preserve atomic and composed NOT/AND/OR,
// conditional, scoped, selector, and version-constraint semantics.
enum class NegativeConstraintSourceKind { ConflictDeclaration, PackageDependency, FeatureDependency };
struct NegativeConstraintSource {
    NegativeConstraintSourceKind kind = NegativeConstraintSourceKind::ConflictDeclaration;
    std::string feature;
    bool operator==(const NegativeConstraintSource&) const = default;
};

struct NegativeConstraint {
    DependencyExpression expression;
    CandidateIdentity declared_by;
    DependencyRole role = DependencyRole::Runtime;
    DependencyTargetContext context = DependencyTargetContext::Target;
    NegativeConstraintSource source;
    bool operator==(const NegativeConstraint&) const = default;
};

struct SystemPolicySelection {
    SystemPolicyId policy;
    DependencySelector selector;
    CandidateIdentity candidate;
    bool operator==(const SystemPolicySelection&) const = default;
};

struct ResolutionResult {
    std::vector<PackageSelection> selections;
    std::vector<CapabilityBinding> capability_bindings;
    std::vector<ConfiguredFeature> configured_features;
    std::vector<OfferedFeature> offered_features;
    std::vector<ActivatedFeature> activated_features;
    std::vector<DependencyEdge> edges;
    std::vector<NegativeConstraint> negative_constraints;
    TargetEnvironment target;
    ResolutionEnvironments environments;
    std::string profile;
    std::vector<SystemPolicySelection> system_policy_selections;
    // Effective resolution policy needed by offline installed validation.
    std::vector<DependencyRole> active_roles{DependencyRole::Runtime};
    bool optional_enabled = false;
    bool propagated_enabled = true;
    std::map<RepositoryId, VersionScheme> version_schemes;
    bool operator==(const ResolutionResult&) const = default;
};

enum class InstallPlanErrorCode { DuplicatePackage, InvalidBinding, InvalidEdge, InvalidFeature, InvalidReason, InvalidExpression, TargetMismatch, RepositoryMismatch, Conflict };
class InstallPlanError : public std::invalid_argument {
public:
    InstallPlanError(InstallPlanErrorCode code, std::string message) : std::invalid_argument(std::move(message)), code_(code) {}
    InstallPlanErrorCode code() const noexcept { return code_; }
private:
    InstallPlanErrorCode code_;
};

class InstallPlan {
public:
    const ResolutionResult& resolution() const noexcept { return result_; }
    const ResolutionResult& result() const noexcept { return result_; }
    const std::vector<PackageSelection>& selections() const noexcept { return result_.selections; }
    const std::vector<CapabilityBinding>& capability_bindings() const noexcept { return result_.capability_bindings; }
    const std::vector<ConfiguredFeature>& configured_features() const noexcept { return result_.configured_features; }
    const std::vector<OfferedFeature>& offered_features() const noexcept { return result_.offered_features; }
    const std::vector<ActivatedFeature>& activated_features() const noexcept { return result_.activated_features; }
    const std::vector<DependencyEdge>& edges() const noexcept { return result_.edges; }
    const std::vector<NegativeConstraint>& negative_constraints() const noexcept { return result_.negative_constraints; }
    const std::vector<SystemPolicySelection>& system_policy_selections() const noexcept { return result_.system_policy_selections; }
    const TargetEnvironment& target() const noexcept { return result_.target; }
    void validate() const;
private:
    explicit InstallPlan(ResolutionResult result) : result_(std::move(result)) {}
    ResolutionResult result_;
    friend class InstallPlanBuilder;
};

class InstallPlanBuilder {
public:
    explicit InstallPlanBuilder(ResolutionResult result) : result_(std::move(result)) {}
    InstallPlan build() const &;
    InstallPlan build() &&;
private:
    ResolutionResult result_;
};

// Pure public planning: resolve once, then validate and finalize the accepted value.
// ResolutionError and InstallPlanError retain their distinct structured codes.
InstallPlan plan_install(const InstallRequest& request);

} // namespace athena::core
