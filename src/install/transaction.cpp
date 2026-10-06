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
    const auto previous = generations.current();
    auto current = installed::prepare_install(previous, plan);
    auto records = realization::realize(plan, objects, execution);
    current.realizations.insert(current.realizations.end(), records.begin(), records.end());
    installed::inherit_profile_features(current, &previous);
    // create() owns validation and canonical ordering. No paths or identities
    // are duplicated in the realization membership.
    const auto target = generations.create(current.entries, current.realizations, current.policies);
    try {
        generations.switch_to(target.id, target_root, objects);
    } catch (...) {
        // C.3 owns filesystem rollback; discard only our inactive metadata.
        // Preserve the activation exception if metadata cleanup also fails.
        try { generations.discard(target.id); } catch (...) {}
        throw;
    }
}
}
