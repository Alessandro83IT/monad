// SPDX-License-Identifier: GPL-3.0-or-later
#include "operational_activation.hpp"
#include "../generation/document.hpp"

namespace athena::activation {
OperationalActivationError::OperationalActivationError(OperationalStage s, OperationalFailure f,
    std::optional<PlanIdentity> id, std::exception_ptr cause)
    : std::runtime_error("Operational activation failed; inspect underlying error"),
      stage(s), failure(f), identity(std::move(id)), underlying(std::move(cause)) {}
namespace {
void conflict(const char* message) {
    throw ExecutionError(ExecutionCause::SelectionConflict, ProjectionOutcome::NotMutated, 0, message);
}
void verify_generation(const ProjectionBinding& binding, const OperationalGeneration& requested) {
    auto observed = generation::read_generation_document(binding.generation_authority, requested.id);
    if (observed.digest != requested.digest || observed.bytes != requested.document)
        throw ExecutionError(ExecutionCause::GenerationMismatch, ProjectionOutcome::NotMutated, 0,
            "Requested generation differs from persisted document");
}
}
OperationalActivationResult activate_operational(ProjectionContext& context, const ProjectionState& state,
    const OperationalPlanInput& request, const std::optional<PlanDigest>& expected_digest) {
    OperationalStage stage = OperationalStage::Identity;
    // Until current is authoritatively observed, a previous attempt may have
    // selected the target. Inspection/authority failures cannot prove otherwise.
    OperationalFailure failure = OperationalFailure::SelectionUncertain;
    std::optional<PlanIdentity> identity;
    try {
        if (request.binding != context.binding() || state.generations() != request.binding.generation_authority)
            conflict("Operational activation authority mismatch");
        auto actual = inspect_selection(context, state);
        // An exact-identity retry can already have selected its target. Digest
        // equality is checked against canonical construction before publication.
        const auto* current = actual ? std::get_if<CurrentV1>(&*actual) : nullptr;
        const bool retry = current && current->activation_id == request.activation_id &&
            current->generation == request.target.id;
        failure = retry ? OperationalFailure::AfterSelection : OperationalFailure::BeforeSelection;
        if (!retry) {
            if (const auto* base = std::get_if<SelectedBase>(&request.base)) {
                if (!actual || *actual != base->current) conflict("Operational activation base conflict");
            } else if (actual) conflict("Operational activation requires absent base");
        }
        verify_generation(request.binding, request.target);
        if (request.base_generation) verify_generation(request.binding, *request.base_generation);
        stage = OperationalStage::Construction;
        auto canonical = build_operational_plan(request);
        identity = PlanIdentity{request.activation_id, canonical.digest};
        if (expected_digest && *expected_digest != canonical.digest)
            throw SchemaError(SchemaErrorCode::Reference, "Authorized plan digest mismatch");
        if (retry && current->plan_digest != canonical.digest) conflict("Operational activation retry digest conflict");
        stage = OperationalStage::Publication;
        auto published = publish_activation_plan(context, state, canonical.intent, canonical.plan);
        stage = OperationalStage::Selection;
        // Any exception inside D6 may follow a visible rename or an earlier
        // uncertain attempt. Never infer absence of selection from an error type.
        if (!retry) failure = OperationalFailure::SelectionUncertain;
        auto selected = commit_selection(context, state, *identity);
        if (selected.state != SelectionState::TargetSelected)
            conflict("D6 did not establish target selection");
        failure = OperationalFailure::AfterSelection;
        stage = OperationalStage::Reconciliation;
        auto reconciled = reconcile_selection(context, state, *identity);
        return {*identity, published.publication, reconciled};
    } catch (...) {
        throw OperationalActivationError(stage, failure, std::move(identity), std::current_exception());
    }
}
}
