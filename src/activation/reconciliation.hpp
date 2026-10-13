// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "selection.hpp"
namespace athena::activation {
enum class ReconciliationState { Complete, Incomplete };
struct ReconciliationResult { ReconciliationState state; ProjectionOutcome outcome; };
// Read-only physical completion check after full authority validation. Ambiguity throws.
ReconciliationResult inspect_reconciliation(ProjectionContext&, const ProjectionState&, const PlanIdentity&);
// Repeats authorized operations and repairs barriers; never changes current.
ReconciliationResult reconcile_selection(ProjectionContext&, const ProjectionState&, const PlanIdentity&);
}
