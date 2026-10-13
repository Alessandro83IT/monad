// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "operational_plan.hpp"
#include "reconciliation.hpp"

namespace athena::activation {
enum class OperationalStage { Identity, Construction, Publication, Selection, Reconciliation };
enum class OperationalFailure { BeforeSelection, SelectionUncertain, AfterSelection };
struct OperationalActivationError : std::runtime_error {
    OperationalStage stage;
    OperationalFailure failure;
    // Present once canonical construction has completed; retain this identity
    // for diagnosis/retry, including uncertain publication and selection.
    std::optional<PlanIdentity> identity;
    std::exception_ptr underlying;
    OperationalActivationError(OperationalStage, OperationalFailure,
        std::optional<PlanIdentity>, std::exception_ptr);
};
struct OperationalActivationResult {
    PlanIdentity identity;
    PlanPublication publication;
    ReconciliationResult reconciliation;
};
// Internal authorized-request boundary. Caller holds exclusive State (after any
// TransactionLease, before StoreGc), protects persisted generations, and excludes
// equivalent-authority writers/mount changes. Source inventories are preverified
// caller observations as in D8.1; D6/D7 independently authorize them afresh.
// Context must already be bound. No initialization, source sealing, cleanup,
// pin retirement, manager calls or additional State acquisition occurs here.
// Success: exact requested CurrentV1 durably selected and D7 physically complete.
// Failure: all plans, preparations, witnesses and generation roots are preserved.
// Retry requires the same complete request and optional independently expected
// digest; a different live base is rejected, never implicitly rebased.
OperationalActivationResult activate_operational(ProjectionContext&, const ProjectionState&,
    const OperationalPlanInput&, const std::optional<PlanDigest>& expected_digest = {});
}
