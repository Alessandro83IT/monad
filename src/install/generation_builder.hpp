#pragma once

#include "../generation/generation.hpp"
#include "../package/package.hpp"
#include "remove_plan.hpp"

#include <filesystem>
#include <utility>
#include <vector>

namespace athena::install {

/*
 * Apply the packages produced by an InstallPlan to the current
 * generation.
 *
 * Packages with the same logical name replace the existing entry.
 * Packages not mentioned by the plan remain active. New package
 * names are appended to the generation.
 *
 * This function only constructs generation metadata. It does not
 * activate files and does not modify persistent generation state.
 */
std::vector<athena::generation::GenerationEntry> build_target_entries(
    const athena::generation::Generation& current,
    const std::vector<
        std::pair<
            athena::package::Package,
            std::filesystem::path
        >
    >& installed
);

/*
 * Apply a RemovePlan to the complete current generation.
 *
 * The operation is deliberately pure: it only constructs the entry list
 * for the candidate generation. Store entries are never deleted here;
 * unreferenced immutable store objects remain available for rollback
 * until a later garbage-collection pass removes them safely.
 */
std::vector<athena::generation::GenerationEntry> remove_target_entries(
    const athena::generation::Generation& current,
    const athena::install::RemovePlan& removal
);

}
