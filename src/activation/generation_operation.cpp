// SPDX-License-Identifier: GPL-3.0-or-later
#include "generation_operation.hpp"
#include "../generation/publication.hpp"

namespace athena::activation {
namespace {
void kind_valid(GenerationOperationKind kind) {
    if (kind != GenerationOperationKind::Switch && kind != GenerationOperationKind::Rollback)
        throw SchemaError(SchemaErrorCode::Schema, "Unknown generation operation kind");
}
void shape(GenerationOperationKind kind, const SelectedBase& base, std::uint64_t target) {
    kind_valid(kind);
    if (!std::holds_alternative<CurrentV1>(base.current))
        throw SchemaError(SchemaErrorCode::Reference, "Generation operation requires explicit operational base");
    (void)serialize(base.current);
    if (target > generation::publication::max_id)
        throw SchemaError(SchemaErrorCode::Bounds, "Generation target exceeds supported ID bound");
    if (kind == GenerationOperationKind::Rollback && target >= selected_generation(base.current))
        throw SchemaError(SchemaErrorCode::Reference, "Rollback target must precede original base");
}
void shape(GenerationOperationKind kind, const PreparedOperationalRequest& request) {
    const auto* base = std::get_if<SelectedBase>(&request.input().base);
    if (!base) throw SchemaError(SchemaErrorCode::Reference, "Generation operation requires selected base");
    shape(kind, *base, request.input().target.id);
}
}

PreparedGenerationOperation::PreparedGenerationOperation(GenerationOperationKind kind,
    PreparedOperationalRequest request)
    : request_(std::move(request)), reference_{kind, identify_operational_request(request_)} {}

PreparedGenerationOperation prepare_generation_operation(ProjectionContext& context,
    const ProjectionState& state, GenerationOperationKind kind, const GenerationOperationInput& input,
    const std::optional<PlanDigest>& expected) {
    // Check authority BEFORE reading caller-selected paths or generation IDs.
    if (input.binding != context.binding() || state.generations() != input.binding.generation_authority)
        throw ExecutionError(ExecutionCause::SelectionConflict, ProjectionOutcome::NotMutated, 0,
            "Generation operation authority mismatch");
    state.revalidate(); context.revalidate();
    shape(kind, input.base, input.target_generation);
    const auto base_id = selected_generation(input.base.current);
    const auto base = generation::read_generation_document(input.binding.generation_authority, base_id);
    const auto target = generation::read_generation_document(input.binding.generation_authority, input.target_generation);
    OperationalPlanInput raw{input.activation_id, input.binding, input.base,
        OperationalGeneration{base_id, base.digest, base.bytes, input.base_sources},
        {input.target_generation, target.digest, target.bytes, input.target_sources}, input.old_witnesses};
    auto request = prepare_operational_request(context, state, raw, expected);
    return {kind, std::move(request)};
}

RequestPublication persist_generation_operation(ProjectionContext& context, const ProjectionState& state,
    OperationalRequestArchive& archive, const PreparedGenerationOperation& operation) {
    return archive.persist(context, state, operation.request());
}

PreparedGenerationOperation recover_generation_operation(ProjectionContext& context, const ProjectionState& state,
    OperationalRequestArchive& archive, const GenerationOperationReference& expected) {
    kind_valid(expected.kind); // Invalid intent must not cause B2 repair/mutation.
    auto recovered = archive.recover(context, state, expected.request);
    shape(expected.kind, recovered.request);
    return {expected.kind, std::move(recovered.request)};
}

OperationalActivationResult activate_generation_operation(ProjectionContext& context, const ProjectionState& state,
    OperationalRequestArchive& archive, const GenerationOperationReference& expected) {
    // Never accepts a cached B1 observation or treats B2 publication as authority.
    auto operation = recover_generation_operation(context, state, archive, expected);
    return activate_prepared_operational(context, state, operation.request());
}
}
