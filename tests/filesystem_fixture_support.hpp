// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <stdexcept>

namespace fixture_fs {
// Only for positive setup below a test-owned temporary root. Never normalize
// existing entries: negative fixtures and production output retain their modes.
inline void create_directories(const std::filesystem::path& path) {
    if (path.empty()) throw std::invalid_argument("empty fixture directory");
    if (std::filesystem::exists(path)) return;
    fixture_fs::create_directories(path.parent_path());
    if (std::filesystem::create_directory(path))
        std::filesystem::permissions(path, std::filesystem::perms::owner_all);
}
}
