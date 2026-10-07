// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../include/athena/core/locking.hpp"
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <sstream>
#include <iomanip>
#include "detail/filesystem.hpp"
#include <pthread.h>
#include <algorithm>

namespace athena::core {

const char* lock_kind_name(LockKind kind) noexcept
{
    switch (kind) {
    case LockKind::State: return "state";
    case LockKind::StoreGc: return "store-gc";
    case LockKind::BuildInput: return "build-input";
    case LockKind::RepositorySnapshot: return "repository-snapshot";
    case LockKind::TransactionLease: return "transaction-lease";
    }
    return "unknown";
}

int lock_order(LockKind kind) noexcept
{
    switch (kind) {
    case LockKind::State: return 10;
    case LockKind::StoreGc: return 20;
    case LockKind::BuildInput: return 1;
    case LockKind::RepositorySnapshot: return 2;
    case LockKind::TransactionLease: return 3;
    }
    return 100;
}

LockLayout LockLayout::from_paths(const PathLayout& paths)
{
    paths.validate();
    return {
        paths.state / "locks" / "state.lock",
        paths.store / ".athena-locks" / "gc.lock",
        paths.repository_state / "locks" / "snapshot.lock",
        paths.build / "locks"
    };
}

std::filesystem::path LockLayout::build_input(const BuildInputId& id) const
{
    const auto& value = id.value();
    std::ostringstream encoded;
    for (const unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') encoded << static_cast<char>(c);
        else encoded << '%' << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c) << std::dec;
    }
    if (encoded.str().empty() || encoded.str() == "." || encoded.str() == "..")
        throw std::invalid_argument("invalid BuildInputId lock key");
    return build_inputs / (encoded.str() + ".lock");
}

namespace {
struct Held { std::uint64_t token; dev_t device; ino_t inode; int order; };
struct Registry { pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; std::vector<Held> held; std::uint64_t next = 1; };
// Process-lifetime registry avoids static destruction ordering with long-lived locks.
Registry& registry() { static auto* value = new Registry; return *value; }
void before_fork() { ::pthread_mutex_lock(&registry().mutex); }
void after_fork() { ::pthread_mutex_unlock(&registry().mutex); }
struct Guard {
    Guard() { ::pthread_mutex_lock(&registry().mutex); }
    ~Guard() { ::pthread_mutex_unlock(&registry().mutex); }
};
void initialize_registry() {
    static const bool initialized = [] {
        (void)registry();
        const int error = ::pthread_atfork(before_fork, after_fork, after_fork);
        if (error) throw std::runtime_error("cannot register lock fork handlers: " + std::string(std::strerror(error)));
        return true;
    }();
    (void)initialized;
}
void remove_registration(std::uint64_t token) noexcept {
    auto& entries = registry().held;
    entries.erase(std::remove_if(entries.begin(), entries.end(), [token](const Held& h) { return h.token == token; }), entries.end());
}
}

std::filesystem::path state_lock_path(const std::filesystem::path& authority) {
    if (!authority.is_absolute()) throw std::invalid_argument("generation authority must be absolute");
    return authority / ".athena-locks" / "state.lock";
}
std::filesystem::path store_gc_lock_path(const std::filesystem::path& authority) {
    if (!authority.is_absolute()) throw std::invalid_argument("store authority must be absolute");
    return authority / ".athena-locks" / "gc.lock";
}

FileLock::FileLock(const std::filesystem::path& path, LockKind kind,
                   LockMode mode, std::string operation, bool wait, bool existing_only) : path_(path)
{
    (void)operation; // Diagnostic metadata must never mutate the synchronization inode.
    if (!path.is_absolute()) throw std::invalid_argument("lock path must be absolute: " + path.string());
    initialize_registry();
    auto parent = detail::open_directory(path.parent_path(), !existing_only, true);
    const auto parent_stat = detail::stat_fd(parent.get());
    if (parent_stat.st_uid != ::geteuid() || (parent_stat.st_mode & 0022))
        throw std::runtime_error("unsafe lock directory ownership or permissions: " + path.parent_path().string());
    auto fd = detail::open_at(parent.get(), path.filename().string(), (existing_only ? 0 : O_CREAT) | O_RDWR, 0600);
    const auto stat = detail::stat_fd(fd.get());
    if (!S_ISREG(stat.st_mode) || stat.st_uid != ::geteuid() || stat.st_nlink != 1 || (stat.st_mode & 0022))
        throw std::runtime_error("unsafe lock object type, ownership, links or permissions: " + path.string());
    {
        Guard guard;
        for (const auto& held : registry().held) {
            if (held.device == stat.st_dev && held.inode == stat.st_ino)
                throw std::logic_error("duplicate lock acquisition: " + path.string());
            if (lock_order(kind) <= held.order)
                throw std::logic_error("lock acquisition violates process-wide lock order");
        }
        registration_ = registry().next++;
        registry().held.push_back({registration_, stat.st_dev, stat.st_ino, lock_order(kind)});
    }
    try {
        int flags = mode == LockMode::Shared ? LOCK_SH : LOCK_EX;
        if (!wait) flags |= LOCK_NB;
        detail::acquire_flock(fd.get(), flags); // EINTR always retries, including nonblocking calls.
        struct stat current{};
        const auto locked = detail::stat_fd(fd.get());
        if (::fstatat(parent.get(), path.filename().c_str(), &current, AT_SYMLINK_NOFOLLOW) < 0 ||
            locked.st_nlink != 1 || current.st_dev != locked.st_dev || current.st_ino != locked.st_ino)
            throw std::runtime_error("lock leaf changed during acquisition: " + path.string());
    } catch (...) {
        Guard guard; remove_registration(registration_); registration_ = 0; throw;
    }
    descriptor_ = fd.release();
}
void FileLock::release() noexcept {
    if (descriptor_ >= 0) {
        Guard guard;
        ::close(descriptor_); descriptor_ = -1;
        remove_registration(registration_); registration_ = 0;
    }
}
FileLock::~FileLock() { release(); }
FileLock::FileLock(FileLock&& other) noexcept
    : path_(std::move(other.path_)), descriptor_(std::exchange(other.descriptor_, -1)),
      registration_(std::exchange(other.registration_, 0)) {}
FileLock& FileLock::operator=(FileLock&& other) noexcept {
    if (this != &other) { release(); path_ = std::move(other.path_);
        descriptor_ = std::exchange(other.descriptor_, -1); registration_ = std::exchange(other.registration_, 0); }
    return *this;
}
bool FileLock::owns_lock() const noexcept { return descriptor_ >= 0; }
void LockSet::acquire(const std::filesystem::path& path, LockKind kind,
                      LockMode mode, std::string operation, bool wait) {
    locks_.emplace_back(path, kind, mode, std::move(operation), wait);
}
} // namespace athena::core
