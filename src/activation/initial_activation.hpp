// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "legacy.hpp"
#include "operational_activation.hpp"
namespace athena::activation {
enum class InitialScenario {
    Fresh,
    VerifiedPreparation,
    RecoveryRequired,
    LegacyEmpty,
    LegacyNonempty,
    Selected
};
enum class InitialStage {
    Classification,
    GenerationZero,
    GenerationRead,
    CurrentObservation,
    StoreBinding,
    Binding,
    Migration,
    Recovery
};
enum class InitialCause {
    Conflict,
    BindingRecoveryRequired,
    ExplicitMigrationRequired,
    StoreBindingConflict,
    LegacyMetadataUnsupported,
    LegacyLocatorUnsupported,
    PrimitiveFailure
};
struct InitialActivationError : std::runtime_error {
    InitialStage stage;
    InitialCause cause;
    ProjectionOutcome outcome;
    std::exception_ptr underlying;
    InitialActivationError(InitialStage, InitialCause, ProjectionOutcome, std::exception_ptr = {});
};
struct InitialObservation {
    InitialScenario scenario;
    bool bound;
    std::optional<CurrentSelection> current;
};
// Internal APIs. Caller holds State continuously and excludes external writers.
// Authority directories already exist. No manager/pins lock acquisition, cleanup,
// retirement, or implicit nonempty migration. Observations grant no ownership.
InitialObservation inspect_initial(const ProjectionBinding &, const store::ObjectStore &,
                                   const ProjectionState &, ProjectionIo = {});
// Publishes/resynchronizes only canonical empty modern generation 0, never current.
// Complete private preparation is reusable; incomplete/foreign residue is retained.
ProjectionOutcome
ensure_initial_generation(const ProjectionState &,
                          const core::detail::Syscalls & = core::detail::linux_syscalls());
OperationalActivationResult bootstrap_initial(const ProjectionBinding &, const store::ObjectStore &,
                                              const ProjectionState &, const ActivationId &,
                                              ProjectionIo = {});
// Explicit same-generation conversion: seals and adopts legacy membership inside
// this exclusion interval, then constructs D8.1 and executes D8.2.
OperationalActivationResult migrate_initial(const ProjectionBinding &, const store::ObjectStore &,
                                            const ProjectionState &, const ActivationId &,
                                            const AdoptionId &, ProjectionIo = {});
// Uses ONLY exact CurrentV1 as independently authorized identity; no plan scan
// chooses a candidate and no absent/historical current is promoted.
ReconciliationResult recover_initial(const ProjectionBinding &, const store::ObjectStore &,
                                     const ProjectionState &, ProjectionIo = {});
} // namespace athena::activation
