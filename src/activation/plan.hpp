// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "current_selection.hpp"
#include "../generation/document.hpp"
#include <optional>
#include <vector>

namespace athena::activation {
struct ActivationOwnership { ActivationId id; bool operator==(const ActivationOwnership&) const = default; };
struct AdoptionOwnership { AdoptionId id; bool operator==(const AdoptionOwnership&) const = default; };
struct EmptyOwnership { bool operator==(const EmptyOwnership&) const = default; };
using BaseOwnership = std::variant<ActivationOwnership, AdoptionOwnership, EmptyOwnership>;
struct SelectedBase {
    CurrentSelection current;
    generation::GenerationDocumentDigest generation_digest;
    BaseOwnership ownership;
    bool operator==(const SelectedBase&) const = default;
};
struct AbsentBase { bool operator==(const AbsentBase&) const = default; };
using ActivationBase = std::variant<AbsentBase, SelectedBase>;
struct ActivationIntent {
    ActivationId activation_id;
    std::string generation_authority, store_authority, projection_root; // Raw native absolute bytes.
    ProjectionId projection_id;
    std::uint64_t target_generation;
    generation::GenerationDocumentDigest target_generation_digest;
    ActivationBase base;
    void validate() const;
    bool operator==(const ActivationIntent&) const = default;
};

struct ArtifactSource { core::ArtifactId artifact_id; bool operator==(const ArtifactSource&) const = default; };
struct LegacyTreeSource {
    std::string locator;
    LegacySealDigest seal_digest;
    bool operator==(const LegacyTreeSource&) const = default;
};
struct LegacyAliasSource {
    std::string locator, alias_target; // Alias target is literal bytes, not a locator.
    core::ArtifactId artifact_id;
    LegacySealDigest seal_digest;
    bool operator==(const LegacyAliasSource&) const = default;
};
using ActivationSource = std::variant<ArtifactSource, LegacyTreeSource, LegacyAliasSource>;
// Canonical identity order: kind spelling, then unsigned raw identity bytes.
bool source_identity_less(const ActivationSource&, const ActivationSource&);

struct WitnessReference {
    std::variant<ActivationId, AdoptionId> owner;
    std::uint64_t entry_id;
    bool operator==(const WitnessReference&) const = default;
};
WitnessReference parse_witness_reference(std::string_view);
std::string serialize(const WitnessReference&);

struct CreateLink { std::uint64_t new_source; WitnessReference new_witness; bool operator==(const CreateLink&) const = default; };
struct ReplaceLink {
    std::uint64_t old_source; WitnessReference old_witness;
    std::uint64_t new_source; WitnessReference new_witness;
    bool operator==(const ReplaceLink&) const = default;
};
struct RemoveLink { std::uint64_t old_source; WitnessReference old_witness; bool operator==(const RemoveLink&) const = default; };
struct KeepLink {
    std::uint64_t old_source; WitnessReference old_witness;
    std::uint64_t new_source; WitnessReference new_witness;
    bool operator==(const KeepLink&) const = default;
};
struct EnsureDirectory { bool operator==(const EnsureDirectory&) const = default; };
using OperationState = std::variant<CreateLink, ReplaceLink, RemoveLink, KeepLink, EnsureDirectory>;
struct ActivationOperation {
    std::uint64_t id;
    std::string path; // Raw native relative bytes.
    OperationState state;
    bool operator==(const ActivationOperation&) const = default;
};
// REMOVE: depth descending, bytes ascending; directories: depth ascending,
// bytes ascending; remaining links: unsigned bytes ascending. No repair/sorting.
bool operation_less(const ActivationOperation&, const ActivationOperation&);
struct ActivationPlan {
    ActivationId activation_id;
    std::vector<ActivationSource> sources;
    std::vector<ActivationOperation> operations;
    void validate() const;
    bool operator==(const ActivationPlan&) const = default;
};
enum class TerminalOutcome { Complete, Aborted };
struct ActivationStatus {
    ActivationId activation_id;
    PlanDigest plan_digest;
    TerminalOutcome outcome;
    bool operator==(const ActivationStatus&) const = default;
};
std::string serialize(const ActivationIntent&);
std::string serialize(const ActivationPlan&);
std::string serialize(const ActivationStatus&);
ActivationIntent parse_activation_intent(std::string_view, const std::optional<ActivationId>& expected_id = {});
ActivationPlan parse_activation_plan(std::string_view, const std::optional<ActivationId>& expected_id = {});
ActivationStatus parse_activation_status(std::string_view, const std::optional<ActivationId>& expected_id = {});
PlanDigest derive_plan_digest(const ActivationIntent&, const ActivationPlan&);
// Strict canonical parsing precedes digesting supplied documents.
PlanDigest digest_plan_documents(std::string_view intent, std::string_view activation);
}
