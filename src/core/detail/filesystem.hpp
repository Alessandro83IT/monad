// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>

namespace athena::core::detail {
// Per-operation injection only. No global hooks. Callbacks use normal errno
// semantics, never throw, and preserve the supported filesystem contract.
struct Syscalls {
    ssize_t (*write)(int, const void*, size_t);
    int (*fsync)(int);
    int (*rename)(int, const char*, int, const char*, unsigned);
    int (*flock)(int, int);
    int (*fstat)(int, struct stat*) = ::fstat;
    ssize_t (*random)(void*, size_t, unsigned) = nullptr;
    int (*cloexec)(int) = nullptr;
    int (*fstatfs)(int, struct statfs*) = ::fstatfs;
};
const Syscalls& linux_syscalls();
class Fd {
public:
    Fd() noexcept = default;
    static Fd adopt(int fd, const Syscalls& = linux_syscalls()); // Explicit ownership transfer; enforces CLOEXEC.
    ~Fd();
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&&) noexcept;
    Fd& operator=(Fd&&) noexcept;
    int get() const noexcept { return fd_; }
    int release() noexcept; // Explicit transfer back to a raw owner.
private:
    explicit Fd(int fd) noexcept : fd_(fd) {}
    int fd_ = -1;
};
struct PersistenceError : std::runtime_error {
    enum class Stage { Write, FileBarrier, Publication, DirectoryBarrier };
    Stage stage;
    // Namespace publication uncertainty; false does not roll back arbitrary direct writes.
    bool outcome_uncertain;
    int error;
    PersistenceError(Stage, bool, int, std::string_view);
};
// Single leaf names only; no slash, dot, or dot-dot. Directory traversal never follows symlinks.
void leaf(std::string_view);
struct stat stat_fd(int);
void regular(int);
void directory(int);
Fd open_directory(const std::filesystem::path&, bool create = false, bool trusted_ancestry = false);
Fd open_at(int parent, std::string_view name, int flags, mode_t mode = 0600);
void mkdir_at(int parent, std::string_view name, mode_t mode = 0700);
void unlink_at(int parent, std::string_view name, bool is_directory = false);
void complete_write(int, std::string_view, const Syscalls& = linux_syscalls());
void file_barrier(int, const Syscalls& = linux_syscalls());
void directory_barrier(int, bool visible = false, const Syscalls& = linux_syscalls());
void acquire_flock(int, int, const Syscalls& = linux_syscalls());
// Durable mutation requires a supported local Linux filesystem (see README).
// Only under that contract does a failed rename prove no publication.
// Overlayfs and unknown filesystem types are rejected before mutation.
// Honest storage barriers remain a deployment prerequisite.
// Parent must be a private/trusted directory: concurrent untrusted entry mutation is unsupported.
void atomic_replace(int parent, std::string_view destination, std::string_view data,
                    const Syscalls& = linux_syscalls());
enum class Publication { Published, AlreadyExists };
// The same supported local filesystem contract applies here.
// Caller has already synchronized prepared file/tree contents. This only publishes the name.
// Same parent enforces same-filesystem publication and one parent durability barrier.
Publication publish_noreplace(int parent, std::string_view prepared, std::string_view destination,
                             const Syscalls& = linux_syscalls());
}
