// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
namespace athena::test {
// mkdtemp atomically reserves a private directory. Never removes a name owned by
// another invocation, even when Debug/Release CTest processes run concurrently.
class TemporaryDirectory {
  public:
    explicit TemporaryDirectory(const std::string &prefix) {
        auto pattern = (std::filesystem::temp_directory_path() / (prefix + "-XXXXXX")).string();
        auto name = ::mkdtemp(pattern.data());
        if (!name)
            throw std::runtime_error("cannot reserve test directory");
        path_ = name;
    }
    ~TemporaryDirectory() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;
    const std::filesystem::path &path() const { return path_; }

  private:
    std::filesystem::path path_;
};
} // namespace athena::test
