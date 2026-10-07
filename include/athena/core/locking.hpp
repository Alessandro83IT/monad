// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "types.hpp"
#include "path_layout.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>

namespace athena::core {

enum class LockKind { State, StoreGc, BuildInput, RepositorySnapshot, TransactionLease };
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

// Lease locks precede authority locks; nested equal-rank acquisitions are rejected.
// Classification is a caller contract; namespace-derived paths are opt-in in M2.5-A.
// The hierarchy is deliberately conservative across threads (not thread-local).
// After a multithreaded fork, the child must exec/_exit before using C++ APIs.
// Process-wide hierarchy, including pending acquisitions, enforced by inode identity.
// Fork inherits the open file description and registry; close releases only the last
// reference. No explicit LOCK_UN is issued. Exec closes descriptors via CLOEXEC.
// Never unlink or replace a lock leaf while any participant may use it.
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
    std::uint64_t registration_ = 0;
    void release() noexcept;
};

// Namespace-derived authorities; opening FileLock safely creates missing directories.
std::filesystem::path state_lock_path(const std::filesystem::path& generation_state);
std::filesystem::path store_gc_lock_path(const std::filesystem::path& store);

class LockSet {
public:
    void acquire(const std::filesystem::path& path, LockKind kind,
                 LockMode mode, std::string operation, bool wait = true);
private:
    std::vector<FileLock> locks_;
};

} // namespace athena::core
