// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "plan_persistence.hpp"
#include <map>
#include <set>

namespace athena::activation {
enum class SelectionState { BaseSelected, TargetSelected };
struct SelectionResult {
    SelectionState state;
    ProjectionOutcome outcome;
};
enum class ExecutionCause { SelectionConflict, GenerationMismatch, MembershipMismatch,
    IncompleteBase, UnsafeMetadata, PreparationConflict, Io, Durability };
struct ExecutionError : std::runtime_error {
    ExecutionCause cause;
    ProjectionOutcome outcome;
    int error;
    ExecutionError(ExecutionCause, ProjectionOutcome, int, std::string_view);
};
// Read-only exact record observation, even when source verification cannot run.
// Visibility alone proves neither the earlier barrier nor reconciliation completion.
std::optional<CurrentSelection> inspect_selection(ProjectionContext&, const ProjectionState&);
// Independent expected identity, held exclusive State. Never publishes a plan.
SelectionResult commit_selection(ProjectionContext&, const ProjectionState&, const PlanIdentity&);
// Only repairs barriers on an already selected target; never promotes a preparation.
SelectionResult recover_selection(ProjectionContext&, const ProjectionState&, const PlanIdentity&);

// Internal common authority boundary; maps are observations scoped to held State.
struct ReconciliationResult;
class ExecutionAccess {
    friend std::optional<CurrentSelection> inspect_selection(ProjectionContext&, const ProjectionState&);
    friend SelectionResult commit_selection(ProjectionContext&, const ProjectionState&, const PlanIdentity&);
    friend SelectionResult recover_selection(ProjectionContext&, const ProjectionState&, const PlanIdentity&);
    friend ReconciliationResult reconcile_selection(ProjectionContext&, const ProjectionState&, const PlanIdentity&);
    friend ReconciliationResult inspect_reconciliation(ProjectionContext&, const ProjectionState&, const PlanIdentity&);
    struct Link { std::string target; WitnessReference witness; bool operator==(const Link&) const = default; };
    struct Authorized {
        InspectedActivationPlan document;
        std::map<std::string, Link> old_links, desired_links;
        std::set<std::string> old_directories, desired_directories;
    };
    static Authorized authorize(ProjectionContext&, const ProjectionState&, const PlanIdentity&);
    static SelectionResult commit(ProjectionContext&, const ProjectionState&, const PlanIdentity&, bool recovery);
    static std::optional<CurrentSelection> current(const ProjectionContext&, bool sync = false);
    static std::optional<CurrentSelection> observe(ProjectionContext&, const ProjectionState&);
    static void require_selected(const ProjectionContext&, const Authorized&);
    static void verify_base(ProjectionContext&, const ProjectionState&, const Authorized&);
    static void directory(const ProjectionContext&, std::string_view, bool sync);
    static void absent(const ProjectionContext&, std::string_view, bool sync);
    static void witness(const ProjectionContext&, std::string_view, const Link&, bool sync);
    static ProjectionOutcome apply(ProjectionContext&, const ProjectionState&, const Authorized&);
    static void verify_complete(const ProjectionContext&, const Authorized&, bool sync);
private:
    static std::string read(const ProjectionContext&, int, std::string_view, std::size_t, bool sync = false);
    static generation::ValidatedGenerationDocument generation(const ProjectionContext&, std::uint64_t);
};
}
