// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "generation_operation.hpp"
#include "../installed/environment.hpp"
#include <athena/core/install_plan.hpp>

namespace athena::activation {
enum class PackageOperationKind { Install, Remove };
struct PackageOperationReference {
    PackageOperationKind kind;
    GenerationOperationReference generation;
};
// Explicit existing complete target, published by the existing pinned generation
// producer. This adapter never runs an acquisition/build or the legacy installer.
// Sources include zero-link members; witnesses refer to the ORIGINAL base.
struct PackageOperationInput {
    GenerationOperationInput generation;
    // Operation-local durability fault seam; unchanged supported-filesystem and
    // honest-syscall contract. Never serialized, retained or execution authority.
    core::detail::Syscalls durability = core::detail::linux_syscalls();
};
class PreparedPackageOperation {
public:
    const PreparedGenerationOperation& generation() const noexcept { return generation_; }
    const PackageOperationReference& reference() const noexcept { return reference_; }
private:
    PreparedPackageOperation(PackageOperationKind, PreparedGenerationOperation);
    PreparedGenerationOperation generation_;
    PackageOperationReference reference_;
    friend PreparedPackageOperation prepare_install_operation(ProjectionContext&, const ProjectionState&,
        const PackageOperationInput&, const core::InstallRequest&, const std::optional<PlanDigest>&);
    friend PreparedPackageOperation prepare_remove_operation(ProjectionContext&, const ProjectionState&,
        const PackageOperationInput&, const std::vector<std::string>&, const std::optional<PlanDigest>&);
    friend PreparedPackageOperation recover_package_operation(ProjectionContext&, const ProjectionState&,
        OperationalRequestArchive&, const PackageOperationReference&);
};
// Generation builders do not mutate generation/store state. Historical legacy
// semantic capture may read metadata. Install uses the existing snapshot resolver and
// installed semantics, with exactly one complete record per selected identity.
// These functions certify semantics only, never payload existence/publication.
generation::Generation build_install_operation_target(const generation::Generation&,
    std::uint64_t, const core::InstallRequest&, const std::vector<realization::RealizationRecord>&);
generation::Generation build_remove_operation_target(const generation::Generation&,
    std::uint64_t, const std::vector<std::string>&);
// Requires existing explicit CurrentV1 base and existing target. Validates exact
// delta, full environment, real source inventories and payload identity. Publishes
// a D4 retention plan BEFORE returning, under the SAME exclusive State. Does not
// select or reconcile. Preparation is therefore not globally read-only; failed
// D4 publication can leave protected residue with its unchanged D4 outcome.
PreparedPackageOperation prepare_install_operation(ProjectionContext&, const ProjectionState&,
    const PackageOperationInput&, const core::InstallRequest&, const std::optional<PlanDigest>& = {});
PreparedPackageOperation prepare_remove_operation(ProjectionContext&, const ProjectionState&,
    const PackageOperationInput&, const std::vector<std::string>&, const std::optional<PlanDigest>& = {});
RequestPublication persist_package_operation(ProjectionContext&, const ProjectionState&,
    OperationalRequestArchive&, const PreparedPackageOperation&);
PreparedPackageOperation recover_package_operation(ProjectionContext&, const ProjectionState&,
    OperationalRequestArchive&, const PackageOperationReference& independently_expected);
OperationalActivationResult activate_package_operation(ProjectionContext&, const ProjectionState&,
    OperationalRequestArchive&, const PackageOperationReference& independently_expected);
// Every call: caller-held exclusive State, after any TransactionLease, before
// StoreGc; equivalent-authority writers/mount changes excluded. Generation
// publication and completed realization are upstream existing producers. No
// manager/lease/pin method is called under State. B2 is NEVER a GC root. D4/D5
// protect both generation metadata and sources across State release/restarts.
// A prepared/reference/archive observation is NOT execution authorization. The
// caller explicitly authorizes activate, which always uses B3 -> B1 -> D8.2 ->
// idempotent D4 -> fresh D6 -> fresh D7. Retain reference independently before B2.
}
