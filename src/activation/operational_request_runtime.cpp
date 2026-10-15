// SPDX-License-Identifier: GPL-3.0-or-later
#include "operational_request_runtime.hpp"
#include "../generation/document.hpp"

namespace athena::activation {
namespace {
[[noreturn]] void conflict(const char* message) {
    throw ExecutionError(ExecutionCause::SelectionConflict,
        ProjectionOutcome::NotMutated, 0, message);
}
void verify_document(const ProjectionBinding& binding, const OperationalGeneration& requested) {
    const auto observed = generation::read_generation_document(binding.generation_authority, requested.id);
    if (observed.digest != requested.digest || observed.bytes != requested.document)
        throw ExecutionError(ExecutionCause::GenerationMismatch,
            ProjectionOutcome::NotMutated, 0, "Prepared generation differs from persisted document");
}
}

OperationalRequestState revalidate_operational_request(ProjectionContext& context,
    const ProjectionState& state, const PreparedOperationalRequest& prepared,
    const std::optional<PlanDigest>& expected) {
    const auto& input = prepared.input();
    if (expected && *expected != prepared.identity().digest)
        throw SchemaError(SchemaErrorCode::Reference, "Prepared request digest mismatch");
    if (input.binding != context.binding() || state.generations() != input.binding.generation_authority)
        conflict("Prepared request authority mismatch");
    // This is the existing constrained D6 observation: validates held State and
    // the already-bound context. A visible link is never interpreted as ownership.
    const auto actual = inspect_selection(context, state);
    const auto* current = actual ? std::get_if<CurrentV1>(&*actual) : nullptr;
    const bool retry = current && current->activation_id == prepared.identity().activation_id &&
        current->generation == input.target.id;
    if (retry) {
        if (current->plan_digest != prepared.identity().digest)
            conflict("Prepared request retry digest conflict");
    } else if (const auto* base = std::get_if<SelectedBase>(&input.base)) {
        if (!actual || *actual != base->current) conflict("Prepared request base conflict");
    } else if (actual) {
        conflict("Prepared request requires absent base");
    }
    verify_document(input.binding, input.target);
    if (input.base_generation) verify_document(input.binding, *input.base_generation);
    // Do not silently accept a changed current/context during document reads.
    // This does not replace the caller's exclusion or D6/D7's fresh checks.
    if (inspect_selection(context, state) != actual)
        conflict("Prepared request current changed during observation");
    return retry ? OperationalRequestState::TargetSelected : OperationalRequestState::BaseCurrent;
}

PreparedOperationalRequest prepare_operational_request(ProjectionContext& context,
    const ProjectionState& state, const OperationalPlanInput& input,
    const std::optional<PlanDigest>& expected) {
    auto prepared = PreparedOperationalRequest::create(input, expected);
    (void)revalidate_operational_request(context, state, prepared, expected);
    return prepared;
}

OperationalActivationResult activate_prepared_operational(ProjectionContext& context,
    const ProjectionState& state, const PreparedOperationalRequest& prepared) {
    return activate_operational(context, state, prepared.input(), prepared.identity().digest);
}
}
