#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace athena::install {

/*
 * Remove a set of installed packages transactionally.
 *
 * Removal is generation-based: the current generation is reconstructed
 * from its exact store paths, validated as a candidate environment,
 * materialized as a new generation, and activated through the existing
 * GenerationManager transaction.
 */
void remove_packages(
    const std::vector<std::string>& package_names,
    const std::filesystem::path& target_root
);

} // namespace athena::install
