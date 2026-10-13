// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "plan.hpp"
#include "projection_binding.hpp"

namespace athena::activation {
// Caller-observed complete projection of one generation member. Paths are raw
// relative bytes. Source identity/seal, payload contents and instance durability
// must already have been verified by the caller; these values are not evidence.
struct OperationalSourceProjection {
    ActivationSource source;
    std::vector<std::string> links;
    // Artifact directories may include empty directories. Legacy declarations
    // may only repeat link parents: D3/D6 project legacy manifest links, not the
    // source tree's explicit empty directories. All parents are derived.
    std::vector<std::string> directories;
};
struct OperationalGeneration {
    std::uint64_t id;
    generation::GenerationDocumentDigest digest;
    std::string document; // Exact validated bytes, including historical provenance.
    std::vector<OperationalSourceProjection> sources; // Complete membership, even zero-link sources.
};
struct OperationalWitness {
    std::string path;
    WitnessReference witness;
};
struct OperationalPlanInput {
    ActivationId activation_id; // Supplied request identity; never randomly generated.
    ProjectionBinding binding;
    ActivationBase base;
    std::optional<OperationalGeneration> base_generation; // Required iff SelectedBase.
    OperationalGeneration target;
    std::vector<OperationalWitness> old_witnesses; // Exactly one per base link.
};
struct OperationalPlanResult {
    ActivationIntent intent;
    ActivationPlan plan;
    PlanDigest digest;
};
// Pure construction and structural validation. Plan/inventory errors use
// SchemaError; generation codec errors retain their existing types. No filesystem
// access, locks, bootstrap, sealing, selection or execution.
// Generation bytes/digests and supplied membership are cross-checked, but source
// inventories and witnesses remain claims. D4 persistence and fresh D6/D7
// authorization are mandatory before any execution; this is not a capability.
OperationalPlanResult build_operational_plan(const OperationalPlanInput&);
}
