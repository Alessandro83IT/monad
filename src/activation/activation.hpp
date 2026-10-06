// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace athena::activation {

void activate(
    const std::filesystem::path& store_directory,
    const std::filesystem::path& target_root
);

void deactivate(
    const std::filesystem::path& store_directory,
    const std::filesystem::path& target_root
);

// Runtime projection supplied by the caller after source verification.
// Directory paths merge with real target directories; files and literal source
// symlinks are projected as links to the source entry, never dereferenced.
struct Projection {
    std::vector<std::string> links;
    std::vector<std::string> directories;
};
void activate(const std::filesystem::path& source_directory,
              const std::filesystem::path& target_root, const Projection& projection);
void deactivate(const std::filesystem::path& source_directory,
                const std::filesystem::path& target_root, const Projection& projection);

bool target_is_free(
    const std::filesystem::path& target
);

}
