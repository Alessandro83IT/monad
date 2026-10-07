// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "transaction.hpp"
#include "../installed/environment.hpp"

namespace athena::install {
void install_plan(const core::InstallPlan& plan,
                  generation::GenerationManager& generations,
                  const store::ObjectStore& objects,
                  const realization::ExecutionOptions& execution,
                  const std::filesystem::path& target_root) {
    plan.validate();
    if (plan.selections().empty()) return;
    // The initialized manager is a caller precondition. Preflight reads state
    // but performs no acquisition, publication, generation writes or activation.
    auto previous = generations.current();
    auto current = installed::prepare_install(previous, plan);
    // Preserve semantic preflight before registration. The handle is deliberately
    // not retired by unwinding; stale pins belong to future recovery policy.
    store::TransactionPins transaction({objects.root(), generations.authority()});
    {
        core::FileLock state(core::state_lock_path(generations.authority()), core::LockKind::State, core::LockMode::Shared);
        store::StoreGcLock gc(objects.root());
        previous = generations.current();
        current = installed::prepare_install(previous, plan);
        for (const auto& record : current.realizations) transaction.add_locked(record.artifact_id, gc);
    }
    auto protected_execution = execution; protected_execution.transaction = &transaction;
    auto records = realization::realize(plan, objects, protected_execution);
    current.realizations.insert(current.realizations.end(), records.begin(), records.end());
    installed::inherit_profile_features(current, &previous);
    // create() owns validation and canonical ordering. No paths or identities
    // are duplicated in the realization membership.
    const auto target = generations.create_pinned(current.entries, current.realizations, current.policies, transaction);
    try {
        generations.switch_to(target.id, target_root, objects);
    } catch (...) {
        // C.3 owns filesystem rollback; discard only our inactive metadata.
        // Preserve the activation exception if metadata cleanup also fails.
        try { generations.discard(target.id); } catch (...) {}
        throw;
    }
    transaction.handoff(target.id);
}
}
