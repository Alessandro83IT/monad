// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "operational_request_archive.hpp"

namespace athena::activation {
enum class GenerationOperationKind { Switch, Rollback };
// Kind is independently retained caller intent, not a new serialized authority.
// Both kinds execute the identical D8.2 path. Rollback additionally requires a
// target ID below the ORIGINAL base ID, including selected-target retries.
struct GenerationOperationReference {
    GenerationOperationKind kind;
    RequestArchiveReference request;
};
struct GenerationOperationInput {
    ActivationId activation_id;
    ProjectionBinding binding;
    SelectedBase base; // Explicit already-operational CurrentV1; no discovery.
    std::uint64_t target_generation;
    // Complete previously verified snapshots, including zero-link members.
    // These are claims; only fresh D6/D7 can authorize execution.
    std::vector<OperationalSourceProjection> base_sources, target_sources;
    std::vector<OperationalWitness> old_witnesses;
};
class PreparedGenerationOperation {
public:
    GenerationOperationKind kind() const noexcept { return reference_.kind; }
    const PreparedOperationalRequest& request() const noexcept { return request_; }
    const GenerationOperationReference& reference() const noexcept { return reference_; }
private:
    PreparedGenerationOperation(GenerationOperationKind, PreparedOperationalRequest);
    PreparedOperationalRequest request_;
    GenerationOperationReference reference_;
    friend PreparedGenerationOperation prepare_generation_operation(ProjectionContext&,
        const ProjectionState&, GenerationOperationKind, const GenerationOperationInput&,
        const std::optional<PlanDigest>&);
    friend PreparedGenerationOperation recover_generation_operation(ProjectionContext&,
        const ProjectionState&, OperationalRequestArchive&, const GenerationOperationReference&);
};

// Reads exact existing base/target documents under State, builds/detaches B1,
// checks explicit intent and B1.3. No generation enumeration, mkdir, migration,
// ownership inference, implicit base selection or GenerationManager call.
PreparedGenerationOperation prepare_generation_operation(ProjectionContext&,
    const ProjectionState&, GenerationOperationKind, const GenerationOperationInput&,
    const std::optional<PlanDigest>& expected_digest = {});
// Returns B2 publication disposition. Retain operation.reference() independently
// BEFORE attempting persistence; no returned disposition confers execution authority.
RequestPublication persist_generation_operation(ProjectionContext&, const ProjectionState&,
    OperationalRequestArchive&, const PreparedGenerationOperation&);
// May repair/promote the independently pinned B2 record before rejecting its
// B3-specific shape. It NEVER selects or executes. No fallback/rebase occurs.
PreparedGenerationOperation recover_generation_operation(ProjectionContext&, const ProjectionState&,
    OperationalRequestArchive&, const GenerationOperationReference& independently_expected);
// Explicit caller-authorized execution: ALWAYS recover/revalidate via B2/B1.3,
// then delegate solely to B1 -> D8.2 -> D4 -> fresh D6/D7, even after prior recovery.
// B2/shape failures do not classify earlier activation history. D8 errors retain
// their exact existing stages, identities and conservative retry outcomes.
OperationalActivationResult activate_generation_operation(ProjectionContext&, const ProjectionState&,
    OperationalRequestArchive&, const GenerationOperationReference& independently_expected);

// Every API requires caller-held exclusive State, after TransactionLease and
// before StoreGc, and exclusion of equivalent-authority writers/mount changes.
// Existing immutable generations and verified inventories/sources must stay live
// until D4 takes its references. Across State intervals/restarts the caller must
// arrange that lifetime using EXISTING retention/pins; B2/B3 add no GC roots.
// A missing/stale generation fails closed, never reconstructed or implicitly pinned.
// Root provisioning, durability, dedicated binding and independent reference are
// the unchanged B2 caller contract. No manager or TransactionPins is called here.
}
