// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include "../../include/athena/core/path_layout.hpp"

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
void remove_packages(
    const std::vector<std::string>& package_names,
    const std::filesystem::path& target_root,
    const athena::core::PathLayout& paths
);

} // namespace athena::install
