// SPDX-License-Identifier: GPL-3.0-or-later
#include "package_operation.hpp"
#include "legacy.hpp"
#include "../artifact/identity.hpp"
#include "../install/generation_builder.hpp"
#include "../store/transaction_pins.hpp"
#include "../store/object_store_detail.hpp"
#include <fcntl.h>
#include <algorithm>
#include <set>
#include <type_traits>

namespace athena::activation {
namespace {
[[noreturn]] void bad(const char* message) { throw SchemaError(SchemaErrorCode::Reference, message); }
void kind(PackageOperationKind value) {
    if (value != PackageOperationKind::Install && value != PackageOperationKind::Remove)
        bad("Unknown package operation kind");
}
void target_id(const generation::Generation& base, std::uint64_t target) {
    if (target == 0 || target <= base.id) bad("Package operation requires a new generation above the original base");
}
bool same_members(const generation::Generation& a, const generation::Generation& b) {
    return a.id == b.id && a.entries == b.entries && a.realizations == b.realizations && a.policies == b.policies &&
        b.compatibility == generation::GenerationCompatibility::Modern;
}
void environment(const PreparedOperationalRequest& request) {
    const auto& in = request.input();
    if (!in.base_generation) bad("Package operation requires complete original base");
    const auto base = generation::validate_generation_document(in.base_generation->document, in.base_generation->id);
    const auto target = generation::validate_generation_document(in.target.document, in.target.id);
    target_id(base.generation, target.generation.id);
    if (target.generation.compatibility != generation::GenerationCompatibility::Modern)
        bad("Package operation target requires modern semantic membership");
    installed::validate_environment(target.generation);
}
void retention(ProjectionContext& context, const ProjectionState& state, const PreparedGenerationOperation& op) {
    const auto retained = recover_activation_plan(context, state, op.request().identity());
    const auto canonical = build_operational_plan(op.request().input());
    if (retained.document.intent != canonical.intent || retained.document.plan != canonical.plan)
        bad("Package operation retention differs from frozen request");
}
// Verify the actual inventory rather than treating B1's structurally complete
// source list as proof. D6/D7 repeat their stronger ownership/durability checks.
void sources(ProjectionContext& context, const ProjectionState& state, const OperationalGeneration& input,
    const core::detail::Syscalls& io) {
    store::ObjectStore objects(context.binding().store_authority);
    for (const auto& claimed : input.sources) {
        std::set<std::string> links, directories;
        if (const auto* object = std::get_if<ArtifactSource>(&claimed.source)) {
            store::StoreGcLock gc(objects.root());
            store::AuthorityContext(objects.root(), state.generations()).validate_binding(false);
            objects.verify(object->artifact_id);
            auto pinned = core::detail::open_directory(objects.object_path(object->artifact_id));
            const auto before = core::detail::stat_fd(pinned.get());
            const auto tree = artifact::scan(objects.object_path(object->artifact_id));
            if (artifact::derive_artifact_id(tree) != object->artifact_id) bad("Package operation source changed");
            for (const auto& entry : tree.entries) {
                if (std::holds_alternative<artifact::Directory>(entry.value)) directories.insert(entry.path);
                else links.insert(entry.path);
            }
            // Re-establish current-instance durability before taking a durable
            // retention reference; upstream publication history is not inferred.
            store::object_detail::synchronize(objects.object_path(object->artifact_id), tree, io);
            for (auto parent = objects.object_path(object->artifact_id).parent_path();;) {
                auto directory = core::detail::open_directory(parent);
                core::detail::directory_barrier(directory.get(), true, io);
                if (parent == objects.root()) break;
                if (parent == parent.root_path()) bad("Artifact escaped bound store during synchronization");
                parent = parent.parent_path();
            }
            objects.verify(object->artifact_id);
            auto named = core::detail::open_directory(objects.object_path(object->artifact_id));
            const auto after = core::detail::stat_fd(named.get());
            if (before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
                before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
                before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
                bad("Artifact root instance changed across B4 synchronization");
        } else {
            const auto legacy = std::visit([](const auto& value) -> LegacySource {
                if constexpr (std::is_same_v<std::decay_t<decltype(value)>, ArtifactSource>)
                    throw SchemaError(SchemaErrorCode::Reference, "Expected legacy source");
                else return value;
            }, claimed.source);
            const auto observed = verify_legacy_seal(context, state, input.id, legacy);
            links.insert(observed.projection.begin(), observed.projection.end());
        }
        // B1 derives parents. Compare the complete effective inventory, including
        // explicit empty artifact directories and every zero-link member.
        auto parents = [](std::set<std::string>& into, const std::string& name) {
            auto path = name;
            for (auto slash = path.rfind('/'); slash != path.npos; slash = path.rfind('/')) {
                path.resize(slash); into.insert(path);
            }
        };
        for (const auto& path : links) parents(directories, path);
        std::set<std::string> expected_links(claimed.links.begin(), claimed.links.end());
        std::set<std::string> expected_directories(claimed.directories.begin(), claimed.directories.end());
        for (const auto& path : expected_links) parents(expected_directories, path);
        for (const auto& path : claimed.directories) parents(expected_directories, path);
        if (links != expected_links || directories != expected_directories)
            bad("Package operation source inventory is not complete and exact");
    }
}
void generation_barriers(const ProjectionBinding& binding, const OperationalGeneration& requested,
    const core::detail::Syscalls& io) {
    namespace d = core::detail;
    auto root = d::open_directory(binding.generation_authority);
    auto directory = d::open_at(root.get(), std::to_string(requested.id), O_RDONLY | O_DIRECTORY);
    auto file = d::open_at(directory.get(), "generation.toml", O_RDONLY | O_NONBLOCK);
    d::regular(file.get());
    const auto original = d::stat_fd(file.get());
    const auto original_directory = d::stat_fd(directory.get());
    auto check = [&] {
        struct stat named {};
        if (::fstatat(directory.get(), "generation.toml", &named, AT_SYMLINK_NOFOLLOW) < 0)
            throw std::system_error(errno, std::generic_category(), "B4 generation observation");
        auto named_directory = d::open_at(root.get(), std::to_string(requested.id), O_RDONLY | O_DIRECTORY);
        const auto current_directory = d::stat_fd(named_directory.get());
        if (original_directory.st_dev != current_directory.st_dev || original_directory.st_ino != current_directory.st_ino)
            bad("Generation directory instance changed across B4 barriers");
        const auto current = d::stat_fd(file.get());
        auto same = [&](const struct stat& value) {
            return value.st_dev == original.st_dev && value.st_ino == original.st_ino &&
                value.st_mode == original.st_mode && value.st_uid == original.st_uid && value.st_gid == original.st_gid &&
                value.st_nlink == original.st_nlink && value.st_size == original.st_size &&
                value.st_mtim.tv_sec == original.st_mtim.tv_sec && value.st_mtim.tv_nsec == original.st_mtim.tv_nsec &&
                value.st_ctim.tv_sec == original.st_ctim.tv_sec && value.st_ctim.tv_nsec == original.st_ctim.tv_nsec;
        };
        const auto observed = generation::read_generation_document(binding.generation_authority, requested.id);
        if (!same(current) || !same(named) || observed.digest != requested.digest || observed.bytes != requested.document)
            bad("Generation changed across B4 durability barriers");
    };
    check();
    d::file_barrier(file.get(), io);
    d::directory_barrier(directory.get(), true, io);
    d::directory_barrier(root.get(), true, io);
    check();
}
PreparedGenerationOperation prepare(ProjectionContext& context, const ProjectionState& state,
    const PackageOperationInput& in, const std::optional<PlanDigest>& expected) {
    return prepare_generation_operation(context, state, GenerationOperationKind::Switch, in.generation, expected);
}
void retain(ProjectionContext& context, const ProjectionState& state, const PreparedGenerationOperation& op,
    const core::detail::Syscalls& io) {
    const auto& request = op.request();
    environment(request);
    generation_barriers(context.binding(), *request.input().base_generation, io);
    generation_barriers(context.binding(), request.input().target, io);
    (void)revalidate_operational_request(context, state, request, request.identity().digest);
    const auto plan = build_operational_plan(request.input());
    // Existing D4 publication is a retention authority, never execution authority.
    // This closes the B2-only lifetime gap without a new root or manager change.
    (void)publish_activation_plan(context, state, plan.intent, plan.plan);
}
}
namespace {
generation::Generation install_target(const generation::Generation& base, std::uint64_t target,
    const core::InstallPlan& plan, const std::vector<realization::RealizationRecord>& records) {
    target_id(base, target);
    if (plan.selections().empty()) bad("Package install requires nonempty resolved selections");
    if (records.size() != plan.selections().size()) bad("Incomplete realization closure");
    auto semantic_base = base;
    installed::capture_legacy_semantics(semantic_base, max_document_size);
    auto candidate = installed::prepare_install(semantic_base, plan);
    std::set<core::PackageId> seen;
    std::vector<installed::SemanticState> original_semantics;
    const auto preserved = candidate.realizations.size();
    for (const auto& record : records) {
        record.validate();
        auto selection = std::find_if(plan.selections().begin(), plan.selections().end(),
            [&](const auto& selected) { return selected.identity() == record.candidate; });
        if (selection == plan.selections().end() || !seen.insert(record.candidate.package).second)
            bad("Realization does not match resolved candidate identity");
        if (!record.semantics) bad("Realization lacks complete semantic state");
        if (const auto& artifact = selection->candidate.artifact; artifact &&
            (artifact->artifact_id != record.artifact_id || artifact->build_input_id != record.build_input_id ||
             artifact->provenance.origin != record.origin))
            bad("Realization differs from repository artifact identity/provenance");
        auto canonical = record;
        canonical.semantics = installed::from_selection(plan, *selection);
        original_semantics.push_back(*canonical.semantics);
        candidate.realizations.push_back(std::move(canonical));
    }
    // Reuse the historical profile normalization, accepting completed executor
    // records or the exact normalized records in an already-published target.
    installed::inherit_profile_features(candidate, &semantic_base);
    for (std::size_t index = 0; index < records.size(); ++index)
        if (*records[index].semantics != original_semantics[index] &&
            records[index].semantics != candidate.realizations[preserved + index].semantics)
            bad("Realization semantics differ from frozen resolution/profile");
    installed::validate_environment(candidate);
    return generation::make_generation(target, candidate.entries, candidate.realizations, candidate.policies);
}
}
generation::Generation build_install_operation_target(const generation::Generation& base,
    std::uint64_t target, const core::InstallRequest& request,
    const std::vector<realization::RealizationRecord>& records) {
    return install_target(base, target, core::plan_install(request), records);
}
generation::Generation build_remove_operation_target(const generation::Generation& base,
    std::uint64_t target, const std::vector<std::string>& names) {
    target_id(base, target);
    if (names.empty()) bad("Package remove requires explicit nonempty package identities");
    install::RemovePlan removal;
    for (const auto& name : names) {
        if (name.empty() || removal.contains(name)) bad("Empty or duplicate removal identity");
        const bool present = std::any_of(base.entries.begin(), base.entries.end(), [&](const auto& e) { return e.package == name; }) ||
            std::any_of(base.realizations.begin(), base.realizations.end(), [&](const auto& r) { return r.candidate.package.value() == name; });
        if (!present) bad("Removal identity is not installed");
        removal.add(name);
    }
    auto candidate = install::build_removal_generation(base, removal);
    installed::capture_legacy_semantics(candidate, max_document_size);
    installed::validate_environment(candidate);
    return generation::make_generation(target, candidate.entries, candidate.realizations, candidate.policies);
}
PreparedPackageOperation::PreparedPackageOperation(PackageOperationKind value, PreparedGenerationOperation op)
    : generation_(std::move(op)), reference_{value, generation_.reference()} {}
PreparedPackageOperation prepare_install_operation(ProjectionContext& context, const ProjectionState& state,
    const PackageOperationInput& input, const core::InstallRequest& request, const std::optional<PlanDigest>& expected) {
    auto op = prepare(context, state, input, expected);
    // Source/seal verification precedes legacy semantic metadata interpretation.
    sources(context, state, *op.request().input().base_generation, input.durability);
    sources(context, state, op.request().input().target, input.durability);
    const auto base = generation::validate_generation_document(op.request().input().base_generation->document,
        op.request().input().base_generation->id).generation;
    const auto target = generation::validate_generation_document(op.request().input().target.document,
        op.request().input().target.id).generation;
    const auto plan = core::plan_install(request);
    std::vector<realization::RealizationRecord> records;
    for (const auto& selection : plan.selections()) {
        const auto found = std::find_if(target.realizations.begin(), target.realizations.end(),
            [&](const auto& r) { return r.candidate == selection.identity(); });
        if (found == target.realizations.end()) bad("Target lacks a resolved realization");
        records.push_back(*found);
    }
    const auto built = install_target(base, target.id, plan, records);
    if (!same_members(built, target)) bad("Persisted install target differs from complete dependency plan");
    retain(context, state, op, input.durability);
    return {PackageOperationKind::Install, std::move(op)};
}
PreparedPackageOperation prepare_remove_operation(ProjectionContext& context, const ProjectionState& state,
    const PackageOperationInput& input, const std::vector<std::string>& names, const std::optional<PlanDigest>& expected) {
    auto op = prepare(context, state, input, expected);
    // Source/seal verification precedes legacy semantic metadata interpretation.
    sources(context, state, *op.request().input().base_generation, input.durability);
    sources(context, state, op.request().input().target, input.durability);
    const auto base = generation::validate_generation_document(op.request().input().base_generation->document,
        op.request().input().base_generation->id).generation;
    const auto target = generation::validate_generation_document(op.request().input().target.document,
        op.request().input().target.id).generation;
    if (!same_members(build_remove_operation_target(base, target.id, names), target))
        bad("Persisted remove target differs from exact requested removal");
    retain(context, state, op, input.durability);
    return {PackageOperationKind::Remove, std::move(op)};
}
RequestPublication persist_package_operation(ProjectionContext& context, const ProjectionState& state,
    OperationalRequestArchive& archive, const PreparedPackageOperation& op) {
    retention(context, state, op.generation());
    return persist_generation_operation(context, state, archive, op.generation());
}
PreparedPackageOperation recover_package_operation(ProjectionContext& context, const ProjectionState& state,
    OperationalRequestArchive& archive, const PackageOperationReference& expected) {
    kind(expected.kind);
    if (expected.generation.kind != GenerationOperationKind::Switch) bad("Package operations require B3 switch shape");
    auto op = recover_generation_operation(context, state, archive, expected.generation);
    environment(op.request());
    retention(context, state, op);
    return {expected.kind, std::move(op)};
}
OperationalActivationResult activate_package_operation(ProjectionContext& context, const ProjectionState& state,
    OperationalRequestArchive& archive, const PackageOperationReference& expected) {
    (void)recover_package_operation(context, state, archive, expected);
    return activate_generation_operation(context, state, archive, expected.generation);
}
}
