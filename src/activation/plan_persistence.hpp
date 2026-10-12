// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "projection_fs.hpp"
#include <exception>
#include <optional>
#include <memory>
#include <map>

namespace athena::activation {
struct PlanIdentity { ActivationId activation_id; PlanDigest digest; };
struct InspectedActivationPlan { ActivationIntent intent; ActivationPlan plan; PlanDigest digest; };
enum class PlanPublication { Published, AlreadyPublished };
struct PlanPersistenceResult {
    InspectedActivationPlan document;
    PlanPublication publication;
    ProjectionOutcome outcome;
};
enum class PlanPersistenceCause {
    InvalidDocument, IdentityMismatch, AuthorityMismatch, UnsafeEntry,
    AdministrativeLimit, BootstrapConflict, NamespaceIncomplete,
    PreparationConflict, PublicationConflict, MissingPlan, Substituted,
    UnsupportedFilesystem, Io, Durability
};
struct PlanPersistenceError : std::runtime_error {
    PlanPersistenceCause cause;
    ProjectionOutcome outcome;
    int error;
    std::exception_ptr underlying;
    PlanPersistenceError(PlanPersistenceCause, ProjectionOutcome, int, std::string_view,
        std::exception_ptr = {});
};
// All operations require held exclusive State and equivalent-authority writer
// exclusion. Returned documents are observations, never execution capabilities.
ProjectionOutcome ensure_plan_namespace(ProjectionContext&, const ProjectionState&);
InspectedActivationPlan inspect_activation_plan(const ProjectionContext&, const ProjectionState&, const ActivationId&);
PlanPersistenceResult publish_activation_plan(ProjectionContext&, const ProjectionState&,
    const ActivationIntent&, const ActivationPlan&);
// Expected digest comes from the authorized request or a separately validated
// durable reference, never from the candidate being recovered. No initialization.
PlanPersistenceResult recover_activation_plan(ProjectionContext&, const ProjectionState&, const PlanIdentity&);

// Internal R1/R2-A evidence model. No filesystem paths/fds are added to the four APIs.
// Object indexes remain stable; foreign objects own value observations, not fds.
class PlanAccess;
namespace plan_detail {
struct OperationEvidence {
    using ObjectId = std::size_t;
    OperationEvidence() = default;
    OperationEvidence(const OperationEvidence&) = delete;
    OperationEvidence& operator=(const OperationEvidence&) = delete;
    OperationEvidence(OperationEvidence&&) noexcept = default;
    OperationEvidence& operator=(OperationEvidence&&) noexcept = default;
    struct Scope {};
    // Stable across moves; copied inventories keep their invocation provenance.
    std::shared_ptr<const Scope> scope; // allocated lazily on the first observation
    enum class Phase { Inspection, Bootstrap, Preparation, Publication, Recovery };
    enum class TransitionKind { BootstrapPublication, PlanPublication };
    // Four authority anchors + two bindings + plans/preparations + target and
    // its two files. Acquisition allows four scratch fds (outer enumeration,
    // foreign container, inner enumeration/document, constrained record read).
    // Excludes caller-owned context/State and independently bounded inherited
    // D2/D3 validation/traversal temporaries; this is the R1 acquirer budget.
    static constexpr std::size_t max_pinned_fds = 11;
    static constexpr std::size_t max_acquisition_fds = max_pinned_fds + 4;
    static constexpr std::size_t max_containers = 4096;
    struct Object {
        std::optional<ObjectId> parent;
        std::string name;
        struct statx metadata;
        std::optional<std::size_t> pin;
    };
    struct InventoryEntry { ObjectId object; Object observation; };
    struct Inventory {
        Phase phase;
        ObjectId directory;
        struct statx before, after;
        std::vector<InventoryEntry> entries;
        std::shared_ptr<const Scope> scope;
    };
    struct Binding { ObjectId object; std::string canonical_bytes; };
    struct Target {
        ObjectId container, intent, plan;
        std::string intent_bytes, plan_bytes;
        InspectedActivationPlan document;
        std::optional<PlanIdentity> independent_expected;
    };
    struct ForeignContainer {
        ObjectId container;
        bool published;
        std::optional<Inventory> interior; // original anchor, never replaced
        std::optional<Inventory> rechecked; // latest successful comparison
    };
    struct Transition {
        TransitionKind kind;
        Phase phase;
        ObjectId object, old_parent, new_parent;
        std::string old_name, new_name;
        struct statx instance;
    };
    struct Barrier { Phase phase; ObjectId object; struct statx instance; };
    std::vector<Object> objects;
    std::vector<core::detail::Fd> pins;
    std::vector<Inventory> inventories;
    std::vector<Binding> bindings;
    std::optional<Target> target;
    std::vector<ForeignContainer> foreign;
    std::vector<Transition> transitions;
    std::vector<Barrier> barriers;

    ObjectId add(std::optional<ObjectId>, std::string, const struct statx&,
                 core::detail::Fd pin = {});
    std::optional<ObjectId> find(std::optional<ObjectId>, std::string_view) const;
    static bool same_identity(const struct statx&, const struct statx&);
    static bool same_observation(const struct statx&, const struct statx&);
    bool matches(ObjectId, const struct statx&) const;
    bool same_inventory(const Inventory&, const Inventory&) const;
    void record_transition(TransitionKind, Phase, ObjectId, ObjectId,
                           std::string, const struct statx&);
private:
    // Bounded O(N) storage, O(log N) lookup without descriptor ownership.
    std::map<std::pair<std::optional<ObjectId>, std::string>, ObjectId> relationships;
    friend class ::athena::activation::PlanAccess;
    // Only the verified I/O boundary may record a completed barrier.
    void record_barrier(Phase, ObjectId, const struct statx&);
};
}
struct PlanEvidenceTestAccess;

// Internal friend boundary, like LegacyAccess. Does not expose context fds.
class PlanAccess {
public:
    static std::vector<std::string> administration_entries(const ProjectionContext&);
    static void namespace_shape(const ProjectionContext&);
    static ProjectionOutcome ensure(ProjectionContext&, const ProjectionState&);
    static InspectedActivationPlan inspect(const ProjectionContext&, const ProjectionState&, const ActivationId&);
    static PlanPersistenceResult publish(ProjectionContext&, const ProjectionState&, const ActivationIntent&, const ActivationPlan&);
    static PlanPersistenceResult recover(ProjectionContext&, const ProjectionState&, const PlanIdentity&);
private:
    friend struct PlanEvidenceTestAccess;
    // Shared acquisition/validation boundaries; four-API conclusion integration is R2-B.
    static plan_detail::OperationEvidence acquire_evidence(
        const ProjectionContext&, const ProjectionState&, const std::optional<ActivationId>&);
    static void compare_foreign(const ProjectionContext&, const ProjectionState&, plan_detail::OperationEvidence&);
    static void evidence_barrier(const ProjectionContext&, const ProjectionState&,
        plan_detail::OperationEvidence&, plan_detail::OperationEvidence::Phase, plan_detail::OperationEvidence::ObjectId);
    static void evidence_transition(const ProjectionContext&, const ProjectionState&,
        plan_detail::OperationEvidence&, plan_detail::OperationEvidence::TransitionKind,
        plan_detail::OperationEvidence::Phase, plan_detail::OperationEvidence::ObjectId,
        plan_detail::OperationEvidence::ObjectId, std::string);
    static PlanPersistenceResult recover_with_evidence(ProjectionContext&, const ProjectionState&,
        const PlanIdentity&, plan_detail::OperationEvidence*);
    static PlanPersistenceResult publish_with_evidence(ProjectionContext&, const ProjectionState&,
        const ActivationIntent&, const ActivationPlan&, plan_detail::OperationEvidence*);
    struct Operation;
};
}
