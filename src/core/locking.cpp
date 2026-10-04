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

namespace athena::core {

const char* lock_kind_name(LockKind kind) noexcept
{
    switch (kind) {
    case LockKind::State: return "state";
    case LockKind::StoreGc: return "store-gc";
    case LockKind::BuildInput: return "build-input";
    case LockKind::RepositorySnapshot: return "repository-snapshot";
    }
    return "unknown";
}

int lock_order(LockKind kind) noexcept
{
    switch (kind) {
    case LockKind::State: return 10;
    case LockKind::StoreGc: return 20;
    case LockKind::BuildInput: return 30;
    case LockKind::RepositorySnapshot: return 40;
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

FileLock::FileLock(const std::filesystem::path& path, LockKind kind,
                   LockMode mode, std::string operation, bool wait)
    : path_(path)
{
    if (!path.is_absolute())
        throw std::invalid_argument("lock path must be absolute: " + path.string());
    std::filesystem::create_directories(path.parent_path());
    descriptor_ = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor_ < 0)
        throw std::runtime_error("Cannot open lock " + path.string() + ": " + std::strerror(errno));
    int flags = mode == LockMode::Shared ? LOCK_SH : LOCK_EX;
    if (!wait) flags |= LOCK_NB;
    if (::flock(descriptor_, flags) != 0) {
        const int error = errno;
        ::close(descriptor_);
        descriptor_ = -1;
        throw std::runtime_error("Cannot acquire " + std::string(lock_kind_name(kind)) +
            " lock " + path.string() + ": " + std::strerror(error));
    }
    if (mode == LockMode::Exclusive) {
        const std::string metadata = "pid=" + std::to_string(::getpid()) +
            "\nkind=" + lock_kind_name(kind) + "\noperation=" + operation + "\n";
        (void)::ftruncate(descriptor_, 0);
        (void)::lseek(descriptor_, 0, SEEK_SET);
        (void)::write(descriptor_, metadata.data(), metadata.size());
    }
}

FileLock::~FileLock() { if (descriptor_ >= 0) { ::flock(descriptor_, LOCK_UN); ::close(descriptor_); } }
FileLock::FileLock(FileLock&& other) noexcept : path_(std::move(other.path_)), descriptor_(std::exchange(other.descriptor_, -1)) {}
FileLock& FileLock::operator=(FileLock&& other) noexcept
{
    if (this != &other) {
        if (descriptor_ >= 0) { ::flock(descriptor_, LOCK_UN); ::close(descriptor_); }
        path_ = std::move(other.path_);
        descriptor_ = std::exchange(other.descriptor_, -1);
    }
    return *this;
}
bool FileLock::owns_lock() const noexcept { return descriptor_ >= 0; }

void LockSet::acquire(const std::filesystem::path& path, LockKind kind,
                      LockMode mode, std::string operation, bool wait)
{
    const int order = lock_order(kind);
    const auto normalized = path.lexically_normal().string();
    if (order < last_order_ || (order == last_order_ && !last_path_.empty() && normalized < last_path_))
        throw std::logic_error("Lock acquisition violates global lock order");
    locks_.emplace_back(path, kind, mode, std::move(operation), wait);
    last_order_ = order;
    last_path_ = normalized;
}

} // namespace athena::core
