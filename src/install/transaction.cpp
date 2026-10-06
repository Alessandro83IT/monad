#include "transaction.hpp"
#include <algorithm>
#include <set>

namespace athena::install {
void install_plan(const core::InstallPlan& plan,
                  generation::GenerationManager& generations,
                  const store::ObjectStore& objects,
                  const realization::ExecutionOptions& execution,
                  const std::filesystem::path& target_root) {
    // Publication is the entire first phase: even generation initialization
    // belongs to the caller, never to a partially realized transaction.
    auto records = realization::realize(plan, objects, execution);
    if (records.empty()) return;

    auto current = generations.current();
    std::set<std::string> replaced;
    for (const auto& record : records)
        replaced.insert(record.candidate.package.value());
    std::erase_if(current.entries, [&](const auto& entry) {
        return replaced.contains(entry.package);
    });
    std::erase_if(current.realizations, [&](const auto& record) {
        return replaced.contains(record.candidate.package.value());
    });
    current.realizations.insert(current.realizations.end(), records.begin(), records.end());
    // create() owns validation and canonical ordering. No paths or identities
    // are duplicated in the realization membership.
    const auto target = generations.create(current.entries, current.realizations);
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
