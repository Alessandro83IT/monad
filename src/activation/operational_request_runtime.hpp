// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "operational_request.hpp"
#include "operational_activation.hpp"

namespace athena::activation {
// A transient identity observation under caller-held State, NOT a capability,
// a durability certificate, or evidence that payloads/witnesses are authorized.
enum class OperationalRequestState { BaseCurrent, TargetSelected };

// B1.2: detach and structurally validate caller-verified inventories/snapshots,
// then compare the exact persisted documents and current record under State.
// The caller supplies the ID, binding, base, documents and complete inventories;
// no selection discovery, inventory scanning, ownership acquisition or ID
// generation occurs. Historical bytes are retained without reserialization.
PreparedOperationalRequest prepare_operational_request(ProjectionContext&,
    const ProjectionState&, const OperationalPlanInput&,
    const std::optional<PlanDigest>& expected_digest = {});

// B1.3: read-only reobservation of the SAME request, including decoded claims.
// Binding/State, exact base (or exact-ID/digest target retry), and both documents
// must still match. No rebasing, publication, barriers, recovery or mutations.
// Throws existing SchemaError, ExecutionError or generation read errors.
OperationalRequestState revalidate_operational_request(ProjectionContext&,
    const ProjectionState&, const PreparedOperationalRequest&,
    const std::optional<PlanDigest>& expected_digest = {});

// The sole execution route remains D8.2 -> D4 -> fresh D6 -> fresh D7. This
// forwards the frozen request and its pinned digest, NEVER a cached observation.
// Errors/results retain D8.2's stage, conservative outcome and retry semantics.
// Caller must independently authorize the operation. Decoding a request or
// obtaining BaseCurrent/TargetSelected does not provide that authorization.
OperationalActivationResult activate_prepared_operational(ProjectionContext&,
    const ProjectionState&, const PreparedOperationalRequest&);

// For all APIs: caller holds exclusive State (after TransactionLease, before
// StoreGc), protects generations until publication, excludes equivalent-authority
// writers/mount changes, and supplies previously verified complete inventories.
// Do not call managers/TransactionPins or reacquire State inside this scope.
// A returned observation can become stale after releasing State. Execution
// always rechecks in D8.2/D6/D7, even when preparation/revalidation succeeded.
// No live payload/witness ownership or completed recovery is certified here.
}
