#include "generation_builder.hpp"

#include <algorithm>

namespace athena::install {

/*
 * Merge newly installed packages into the complete active
 * environment represented by the current generation.
 *
 * The operation is deliberately pure with respect to Athena's
 * persistent state: it only produces the entry list that a later
 * GenerationManager::create() call will persist.
 */
std::vector<athena::generation::GenerationEntry> build_target_entries(
    const athena::generation::Generation& current,
    const std::vector<
        std::pair<
            athena::package::Package,
            std::filesystem::path
        >
    >& installed
)
{
    std::vector<athena::generation::GenerationEntry> entries =
        current.entries;

    for (const auto& [package, store_path] : installed) {
        const auto it =
            std::find_if(
                entries.begin(),
                entries.end(),
                [&package](const auto& entry) {
                    return entry.package == package.name;
                }
            );

        const athena::generation::GenerationEntry replacement{
            package.name,
            store_path.string()
        };

        if (it != entries.end()) {
            *it = replacement;
        }
        else {
            entries.push_back(replacement);
        }
    }

    return entries;
}


/*
 * Construct the candidate generation after a complete removal plan.
 *
 * This mirrors build_target_entries() but in the opposite direction:
 * only entries explicitly named by the RemovePlan are removed. The
 * immutable Store is intentionally untouched because historical
 * generations may still reference the removed store objects.
 */
std::vector<athena::generation::GenerationEntry> remove_target_entries(
    const athena::generation::Generation& current,
    const athena::install::RemovePlan& removal
)
{
    std::vector<athena::generation::GenerationEntry> entries;

    entries.reserve(current.entries.size());

    for (const auto& entry : current.entries) {

        if (!removal.contains(entry.package)) {
            entries.push_back(entry);
        }
    }

    return entries;
}


}
