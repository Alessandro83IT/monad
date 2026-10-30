// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdlib>
#include <filesystem>
#include <cerrno>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace athena::test {
inline std::filesystem::path temporary_root() {
    if (const char *root = std::getenv("ATHENA_TEST_TMPDIR"); root && *root)
        return root;
    return std::filesystem::temp_directory_path();
}
inline std::filesystem::path make_temporary_directory(std::string_view prefix) {
    const auto root = temporary_root();
    if (!std::filesystem::is_directory(root))
        throw std::runtime_error("test temporary root is not a directory: " + root.string());
    std::string pattern = (root / (std::string(prefix) + "-XXXXXX")).string();
    if (!::mkdtemp(pattern.data()))
        throw std::system_error(errno, std::generic_category(), "create private test fixture");
    return pattern;
}
}
