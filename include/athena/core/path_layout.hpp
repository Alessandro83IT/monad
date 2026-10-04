#pragma once

#include <filesystem>
#include <stdexcept>
#include <utility>

namespace athena::core {

struct PathLayout {
    std::filesystem::path root;
    std::filesystem::path store;
    std::filesystem::path state;
    std::filesystem::path cache;
    std::filesystem::path build;
    std::filesystem::path repository_snapshots;
    std::filesystem::path repository_state;
    std::filesystem::path generation_state;
    std::filesystem::path target_root;

    static PathLayout from_root(std::filesystem::path base)
    {
        if (base.empty()) throw std::invalid_argument("Athena root cannot be empty");
        base = base.lexically_normal();
        PathLayout result{
            base, base / "store", base / "state", base / "cache",
            base / "build", base / "repositories" / "snapshots",
            base / "repositories" / "state", base / "generations",
            base / "target"
        };
        result.validate();
        return result;
    }
    void validate() const
    {
        if (root.empty() || store.empty() || state.empty() || cache.empty() ||
            build.empty() || repository_snapshots.empty() || repository_state.empty() ||
            generation_state.empty() || target_root.empty())
            throw std::invalid_argument("PathLayout contains an empty path");
        if (!root.is_absolute()) throw std::invalid_argument("PathLayout root must be absolute");
    }
};

} // namespace athena::core
