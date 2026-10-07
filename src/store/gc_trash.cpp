// SPDX-License-Identifier: GPL-3.0-or-later
#include "gc_trash.hpp"
#include "object_store.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <system_error>

namespace athena::store {
namespace d = core::detail;
namespace a = administrative;
namespace fs = std::filesystem;
GcTrash::GcTrash(const fs::path& source, bool modern, const d::Syscalls& ops)
    : source_(a::locator(source)), modern_(modern) {
    source_fd_ = d::open_directory(source_, false, true);
    root_ = a::ensure_directory(source_fd_.get(), ".athena-gc-trash", ops);
    std::string preparation;
    for (;;) {
        id_ = a::random_id(ops); preparation = ".prepare-" + id_;
        struct stat status{};
        if (::fstatat(root_.get(), id_.c_str(), &status, AT_SYMLINK_NOFOLLOW) == 0) continue;
        if (errno != ENOENT) throw std::runtime_error("trash collision inspection");
        if ((ops.mkdir ? ops.mkdir : ::mkdirat)(root_.get(), preparation.c_str(), 0700) == 0) break;
        if (errno != EEXIST) throw std::system_error(errno, std::generic_category(), "trash mkdir");
    }
    container_ = d::open_at(root_.get(), preparation, O_RDONLY | O_DIRECTORY); a::private_directory(container_.get());
    lease_identity_ = d::open_at(container_.get(), "lease", O_CREAT | O_EXCL | O_RDWR, 0600);
    d::complete_write(lease_identity_.get(), "athena-gc-trash-v1\n" + id_ + "\n", ops); d::file_barrier(lease_identity_.get(), ops);
    lease_ = std::make_unique<core::FileLock>(source_ / ".athena-gc-trash" / preparation / "lease", core::LockKind::TransactionLease,
                                           core::LockMode::Exclusive, "trash preparation", true, true);
    a::unchanged(container_.get(), "lease", lease_->descriptor());
    d::directory_barrier(container_.get(), false, ops);
    a::rename_detach(root_.get(), preparation, root_.get(), id_, ops);
}
GcTrash::GcTrash(const fs::path& source, bool modern, std::string id)
    : source_(a::locator(source)), modern_(modern), id_(std::move(id)) {
    if (!a::hex_name(id_, 32)) throw std::runtime_error("invalid trash ID");
    source_fd_ = d::open_directory(source_, false, true);
    root_ = d::open_at(source_fd_.get(), ".athena-gc-trash", O_RDONLY | O_DIRECTORY); a::private_directory(root_.get());
    container_ = d::open_at(root_.get(), id_, O_RDONLY | O_DIRECTORY); a::private_directory(container_.get());
    lease_identity_ = d::open_at(container_.get(), "lease", O_RDONLY); a::private_file(lease_identity_.get());
    lease_ = std::make_unique<core::FileLock>(source_ / ".athena-gc-trash" / id_ / "lease", core::LockKind::TransactionLease,
                                           core::LockMode::Exclusive, "trash reclaim", false, true);
    a::unchanged(container_.get(), "lease", lease_->descriptor()); validate();
}
void GcTrash::validate() const {
    auto source_now = d::open_directory(source_, false, true);
    const auto original = d::stat_fd(source_fd_.get()), current = d::stat_fd(source_now.get());
    if (original.st_dev != current.st_dev || original.st_ino != current.st_ino)
        throw std::runtime_error("trash source namespace substituted");
    a::same_mount(source_fd_.get(), source_now.get());
    a::private_directory(root_.get()); a::private_directory(container_.get());
    a::unchanged(source_fd_.get(), ".athena-gc-trash", root_.get()); a::unchanged(root_.get(), id_, container_.get());
    a::unchanged(container_.get(), "lease", lease_identity_.get()); a::private_file(lease_identity_.get());
    a::same_mount(source_fd_.get(), root_.get()); a::same_mount(root_.get(), container_.get());
    if (a::read_file(container_.get(), "lease") != "athena-gc-trash-v1\n" + id_ + "\n") throw std::runtime_error("invalid/unknown trash lease format");
    for (const auto& name : a::entries(container_.get())) {
        if (name == "lease") continue;
        if ((modern_ && !a::hex_name(name, 64)) || (!modern_ && name != "payload")) throw std::runtime_error("invalid trash contents");
        auto payload = d::open_at(container_.get(), name, O_RDONLY | O_DIRECTORY); a::same_mount(container_.get(), payload.get());
    }
    if (a::entries(container_.get()).size() > 2) throw std::runtime_error("multiple trash payloads");
}
void GcTrash::detach_locked(std::string_view name, const d::Syscalls& ops) {
    validate(); d::leaf(name);
    if (modern_ && !a::hex_name(name, 64)) throw std::runtime_error("invalid trash ArtifactId");
    // Validate source is a real directory on this mount before any authoritative mutation.
    auto object = d::open_at(source_fd_.get(), name, O_RDONLY | O_DIRECTORY); a::same_mount(source_fd_.get(), object.get());
    a::unchanged(source_fd_.get(), name, object.get());
    a::rename_detach(source_fd_.get(), name, container_.get(), modern_ ? name : "payload", ops);
    durable_ = true;
}
void GcTrash::confirm_locked(const d::Syscalls& ops) {
    validate();
    if (modern_) for (const auto& name : a::entries(container_.get())) {
        if (name == "lease") continue;
        struct stat authoritative{}, detached{};
        if (::fstatat(source_fd_.get(), name.c_str(), &authoritative, AT_SYMLINK_NOFOLLOW) == 0) {
            if (::fstatat(container_.get(), name.c_str(), &detached, AT_SYMLINK_NOFOLLOW) < 0)
                throw std::runtime_error("trash payload disappeared during confirmation");
            if (authoritative.st_dev == detached.st_dev && authoritative.st_ino == detached.st_ino)
                throw std::runtime_error("trash aliases authoritative object; recovery required");
        } else if (errno != ENOENT) throw std::runtime_error("authoritative detach confirmation failed");
    }
    d::require_durable_directory(source_fd_.get(), ops); d::require_durable_directory(container_.get(), ops);
    d::directory_barrier(source_fd_.get(), true, ops); d::directory_barrier(container_.get(), true, ops);
    durable_ = true;
}
void GcTrash::reclaim(const AuthorityContext& authority, const d::Syscalls& ops) {
    if (!durable_) throw std::runtime_error("uncertain trash detach cannot be reclaimed");
    validate();
    for (const auto& name : a::entries(container_.get())) if (name != "lease")
        ObjectStore::reclaim_private_trash_payload(container_.get(), name, ops);
    d::directory_barrier(container_.get(), true, ops);
    // Claimants must verify under StoreGc after acquiring a lease. Hold the
    // synchronization inode until namespace removal completes, never replace it.
    core::FileLock store(core::store_gc_lock_path(authority.store()), core::LockKind::StoreGc);
    validate();
    if ((ops.unlink ? ops.unlink : ::unlinkat)(container_.get(), "lease", 0) < 0)
        throw std::system_error(errno, std::generic_category(), "trash lease cleanup");
    if ((ops.unlink ? ops.unlink : ::unlinkat)(root_.get(), id_.c_str(), AT_REMOVEDIR) < 0)
        throw std::system_error(errno, std::generic_category(), "trash container cleanup");
    d::directory_barrier(root_.get(), true, ops);
    lease_.reset(); lease_identity_ = {}; container_ = {};
}
std::vector<std::string> GcTrash::containers(const fs::path& source) {
    auto directory = d::open_directory(source, false, true); struct stat status{};
    if (::fstatat(directory.get(), ".athena-gc-trash", &status, AT_SYMLINK_NOFOLLOW) < 0) {
        if (errno == ENOENT) return {}; throw std::runtime_error("trash root inspection");
    }
    auto root = d::open_at(directory.get(), ".athena-gc-trash", O_RDONLY | O_DIRECTORY); a::private_directory(root.get());
    a::same_mount(directory.get(), root.get()); std::vector<std::string> result;
    for (const auto& name : a::entries(root.get())) {
        if (name.starts_with(".prepare-") && a::hex_name(std::string_view(name).substr(9), 32)) continue;
        if (!a::hex_name(name, 32)) throw std::runtime_error("unexpected trash entry");
        result.push_back(name);
    }
    return result;
}
}
