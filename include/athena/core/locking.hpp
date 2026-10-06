// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "types.hpp"
#include "path_layout.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace athena::core {

enum class LockKind { State, StoreGc, BuildInput, RepositorySnapshot };
enum class LockMode { Shared, Exclusive };

const char* lock_kind_name(LockKind kind) noexcept;
int lock_order(LockKind kind) noexcept;

struct LockLayout {
    std::filesystem::path state;
    std::filesystem::path store_gc;
    std::filesystem::path repository_snapshot;
    std::filesystem::path build_inputs;

    static LockLayout from_paths(const PathLayout& paths);
    std::filesystem::path build_input(const BuildInputId& id) const;
};

class FileLock {
public:
    FileLock(const std::filesystem::path& lock_path, LockKind kind,
             LockMode mode = LockMode::Exclusive,
             std::string operation = "unspecified", bool wait = true);
    ~FileLock();
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    FileLock(FileLock&& other) noexcept;
    FileLock& operator=(FileLock&& other) noexcept;
    bool owns_lock() const noexcept;
    const std::filesystem::path& path() const noexcept { return path_; }
private:
    std::filesystem::path path_;
    int descriptor_ = -1;
};

class LockSet {
public:
    void acquire(const std::filesystem::path& path, LockKind kind,
                 LockMode mode, std::string operation, bool wait = true);
private:
    int last_order_ = -1;
    std::string last_path_;
    std::vector<FileLock> locks_;
};

} // namespace athena::core
