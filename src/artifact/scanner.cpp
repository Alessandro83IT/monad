// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scanner.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <dirent.h>
#include <fcntl.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/magic.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace athena::artifact {
namespace {
class Descriptor {
public:
    explicit Descriptor(int fd) : fd_(fd) {}
    ~Descriptor() { if (fd_ >= 0) ::close(fd_); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    Descriptor(Descriptor&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    Descriptor& operator=(Descriptor&& other) noexcept
    {
        if (this != &other) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }
    int get() const { return fd_; }
private:
    int fd_;
};

struct DirectoryCloser {
    void operator()(DIR* directory) const noexcept { ::closedir(directory); }
};

[[noreturn]] void system_error(ErrorCode code, const std::string& path, const char* operation)
{
    const int number = errno;
    throw Error(code, path, std::string(operation) + ": " + std::strerror(number));
}

struct stat inspect(int fd, const std::string& path)
{
    struct stat value{};
    if (::fstat(fd, &value) != 0) system_error(ErrorCode::InspectionFailure, path, "fstat");
    return value;
}

bool same_observation(const struct stat& a, const struct stat& b)
{
    // Transient observation evidence only. Access times are intentionally not
    // compared: the kernel may account for readlink access, even on O_PATH fds.
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_mode == b.st_mode &&
        a.st_nlink == b.st_nlink && a.st_uid == b.st_uid && a.st_gid == b.st_gid &&
        a.st_size == b.st_size && a.st_mtim.tv_sec == b.st_mtim.tv_sec &&
        a.st_mtim.tv_nsec == b.st_mtim.tv_nsec && a.st_ctim.tv_sec == b.st_ctim.tv_sec &&
        a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}

void unchanged(const struct stat& before, const struct stat& after, const std::string& path)
{
    if (!same_observation(before, after))
        throw Error(ErrorCode::ChangedTree, path, "filesystem node changed during observation");
}

void raw_semantics(const struct stat& value, const std::string& path)
{
    if (!S_ISDIR(value.st_mode) && !S_ISREG(value.st_mode) && !S_ISLNK(value.st_mode))
        throw Error(ErrorCode::UnsupportedFileType, path, "only directory, regular file and symlink are supported");
    if (value.st_mode & (S_ISUID | S_ISGID | S_ISVTX))
        throw Error(ErrorCode::UnsupportedPermissions, path, "setuid, setgid and sticky bits are unsupported");
    if ((S_ISREG(value.st_mode) || S_ISLNK(value.st_mode)) && value.st_nlink != 1)
        throw Error(ErrorCode::HardLinkTopology, path, "regular files and symlinks must have link count one");
}

bool can_list_trusted_attributes()
{
    // Linux trusted.* listing uses capable(CAP_SYS_ADMIN), in the initial user
    // namespace. A capability in a nested user namespace is not sufficient.
    __user_cap_header_struct header{_LINUX_CAPABILITY_VERSION_3, 0};
    __user_cap_data_struct data[2]{};
    if (::syscall(SYS_capget, &header, data) != 0) return false;
    if (!(data[CAP_SYS_ADMIN / 32].effective & (1U << (CAP_SYS_ADMIN % 32)))) return false;
    Descriptor user_namespace(::open("/proc/self/ns/user", O_RDONLY | O_CLOEXEC));
    if (user_namespace.get() < 0) return false;
    struct stat status{};
    struct statfs filesystem{};
    // Linux reserves this namespace inode (USER_NS_INIT_INO in newer UAPI
    // headers). A full identity uid_map alone can also occur in nested user
    // namespaces, so it is not sufficient proof of initial-namespace authority.
    constexpr ino_t initial_user_namespace_inode = 0xEFFFFFFDU;
    return ::fstat(user_namespace.get(), &status) == 0 &&
        ::fstatfs(user_namespace.get(), &filesystem) == 0 &&
        filesystem.f_type == NSFS_MAGIC && status.st_ino == initial_user_namespace_inode;
}

bool attributes(int fd, const std::string& path, const std::string* symlink_locator)
{
    const auto count = symlink_locator
        ? ::llistxattr(symlink_locator->c_str(), nullptr, 0)
        : ::flistxattr(fd, nullptr, 0);
    if (count < 0) {
        // An explicit filesystem declaration of unsupported xattrs establishes
        // absence. EPERM/EACCES/other failures do not.
        if (errno == EOPNOTSUPP) return true;
        system_error(ErrorCode::InspectionFailure, path, "list extended attributes");
    }
    if (count != 0)
        throw Error(ErrorCode::UnsupportedMetadata, path, "extended attributes/ACLs/security labels are unsupported");
    return can_list_trusted_attributes();
}

void flags(int fd, bool symlink, const std::string& path)
{
    struct statx value{};
    if (::statx(fd, "", AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS, &value) != 0)
        system_error(ErrorCode::InspectionFailure, path, "statx filesystem attributes");
    // Compression is explicitly nonsemantic in v1; mount-root is placement.
    // Automount, encryption, verity, DAX and unknown attributes fail closed.
    constexpr std::uint64_t nonsemantic_attributes = STATX_ATTR_MOUNT_ROOT | STATX_ATTR_COMPRESSED;
    if (value.stx_attributes & ~nonsemantic_attributes)
        throw Error(ErrorCode::UnsupportedMetadata, path, "semantic filesystem attributes are unsupported");
    if (symlink) {
        // Linux does not provide GETFLAGS on O_PATH symlink descriptors. Require
        // statx to explicitly cover the inode mutation flags instead.
        constexpr auto required = STATX_ATTR_IMMUTABLE | STATX_ATTR_APPEND;
        if ((value.stx_attributes_mask & required) != required)
            throw Error(ErrorCode::InspectionFailure, path, "symlink filesystem flag absence cannot be established");
        return;
    }
    int raw = 0;
    if (::ioctl(fd, FS_IOC_GETFLAGS, &raw) != 0) {
        // tmpfs has no persistent inode flags beyond those reported by statx.
        // Do not treat an arbitrary ENOTTY as proof on an unknown filesystem.
        const int error = errno;
        struct statfs filesystem{};
        if ((error == ENOTTY || error == EOPNOTSUPP) &&
            ::fstatfs(fd, &filesystem) == 0 && filesystem.f_type == TMPFS_MAGIC &&
            (value.stx_attributes_mask & (STATX_ATTR_IMMUTABLE | STATX_ATTR_APPEND)) ==
                (STATX_ATTR_IMMUTABLE | STATX_ATTR_APPEND)) return;
        errno = error;
        system_error(ErrorCode::InspectionFailure, path, "inspect inode flags");
    }
    // Allocation/accounting and internal indexing flags do not change v1
    // semantics. Unknown bits fail closed, including inheritance and nodump.
    constexpr unsigned int nonsemantic = FS_EXTENT_FL | FS_HUGE_FILE_FL | FS_INDEX_FL |
        FS_INLINE_DATA_FL | FS_EOFBLOCKS_FL | FS_COMPR_FL | FS_COMPRBLK_FL | FS_NOCOMP_FL;
    if (static_cast<unsigned int>(raw) & ~nonsemantic)
        throw Error(ErrorCode::UnsupportedMetadata, path, "semantic or unknown inode flags are unsupported");
}

Descriptor open_root(const std::filesystem::path& root)
{
    const auto& bytes = root.native();
    if (bytes.empty() || bytes.find('\0') != bytes.npos)
        throw Error(ErrorCode::InvalidRoot, {}, "root locator is empty or contains NUL");
    Descriptor current(::open(root.is_absolute() ? "/" : ".", O_PATH | O_DIRECTORY | O_CLOEXEC));
    if (current.get() < 0) system_error(ErrorCode::InvalidRoot, {}, "open root base");
    // Walk the locator itself with no-follow semantics, including its parents.
    for (const auto& component : root) {
        const auto name = component.native();
        if (name.empty() || name == "/" || name == ".") continue;
        Descriptor next(::openat(current.get(), name.c_str(), O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (next.get() < 0) system_error(ErrorCode::InvalidRoot, {}, "open real root directory component");
        current = std::move(next);
    }
    const auto before = inspect(current.get(), {});
    Descriptor readable(::openat(current.get(), ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_NOATIME));
    if (readable.get() < 0) system_error(ErrorCode::InspectionFailure, {}, "open root for read-only enumeration");
    unchanged(before, inspect(readable.get(), {}), {});
    return readable;
}

std::vector<std::string> names(int fd, const std::string& path)
{
    Descriptor duplicate(::openat(fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW | O_NOATIME));
    if (duplicate.get() < 0) system_error(ErrorCode::InspectionFailure, path, "open enumeration descriptor");
    // fdopendir owns its own fd; retain the RAII descriptor separately.
    const int copy = ::dup(duplicate.get());
    if (copy < 0) system_error(ErrorCode::InspectionFailure, path, "duplicate enumeration descriptor");
    DIR* raw = ::fdopendir(copy);
    if (!raw) {
        const int error = errno;
        ::close(copy);
        errno = error;
        system_error(ErrorCode::InspectionFailure, path, "fdopendir");
    }
    std::unique_ptr<DIR, DirectoryCloser> directory(raw);
    std::vector<std::string> result;
    for (;;) {
        errno = 0;
        const auto* entry = ::readdir(directory.get());
        if (!entry) {
            if (errno) system_error(ErrorCode::InspectionFailure, path, "readdir");
            break;
        }
        std::string name(entry->d_name);
        if (name != "." && name != "..") result.push_back(std::move(name));
    }
    std::sort(result.begin(), result.end(), path_bytes_less);
    if (std::adjacent_find(result.begin(), result.end()) != result.end())
        throw Error(ErrorCode::ChangedTree, path, "duplicate directory observation");
    return result;
}

struct Observation {
    std::string path;
    struct stat status;
};
struct Scan {
    NormalizedArtifact model;
    std::vector<Observation> observations;
    std::optional<std::string> unverified_attributes;

    void metadata(int fd, const std::string& path, const std::string* locator)
    {
        if (!attributes(fd, path, locator) && !unverified_attributes)
            unverified_attributes = path;
    }

    void require_complete_metadata() const
    {
        if (unverified_attributes)
            throw Error(ErrorCode::InspectionFailure, *unverified_attributes,
                "cannot establish absence of hidden trusted.* xattrs; initial-namespace CAP_SYS_ADMIN required");
    }

    void directory(int fd, const std::string& path)
    {
        const auto before = inspect(fd, path);
        raw_semantics(before, path);
        metadata(fd, path, nullptr);
        flags(fd, false, path);
        observations.push_back({path, before});
        const auto children = names(fd, path);
        for (const auto& name : children) {
            const std::string relative = path.empty() ? name : path + '/' + name;
            validate_path(relative);
            Descriptor node(::openat(fd, name.c_str(), O_PATH | O_NOFOLLOW | O_CLOEXEC));
            if (node.get() < 0) system_error(ErrorCode::InspectionFailure, relative, "open no-follow node");
            const auto status = inspect(node.get(), relative);
            raw_semantics(status, relative);
            if (S_ISLNK(status.st_mode)) {
                // llistxattr must operate on the actual link, not the proc fd
                // link. The parent descriptor pins every preceding component.
                const std::string locator = "/proc/self/fd/" + std::to_string(fd) + '/' + name;
                metadata(node.get(), relative, &locator);
                flags(node.get(), true, relative);
                std::string target(256, '\0');
                for (;;) {
                    const auto length = ::readlinkat(node.get(), "", target.data(), target.size());
                    if (length < 0) system_error(ErrorCode::ReadFailure, relative, "readlinkat");
                    if (static_cast<std::size_t>(length) < target.size()) {
                        target.resize(static_cast<std::size_t>(length));
                        break;
                    }
                    if (target.size() > target.max_size() / 2)
                        throw Error(ErrorCode::ReadFailure, relative, "link target exceeds local resource limit");
                    target.resize(target.size() * 2);
                }
                model.entries.push_back({relative, SymbolicLink{std::move(target)}});
                observations.push_back({relative, status});
            } else {
                const int options = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NOATIME | O_NONBLOCK |
                    (S_ISDIR(status.st_mode) ? O_DIRECTORY : 0);
                Descriptor readable(::openat(fd, name.c_str(), options));
                if (readable.get() < 0) system_error(ErrorCode::ReadFailure, relative, "open payload for reading");
                unchanged(status, inspect(readable.get(), relative), relative);
                if (S_ISDIR(status.st_mode)) {
                    model.entries.push_back({relative, Directory{}});
                    directory(readable.get(), relative);
                } else {
                    metadata(readable.get(), relative, nullptr);
                    flags(readable.get(), false, relative);
                    std::string content;
                    char buffer[65536];
                    for (;;) {
                        const auto length = ::read(readable.get(), buffer, sizeof(buffer));
                        if (length < 0) {
                            if (errno == EINTR) continue;
                            system_error(ErrorCode::ReadFailure, relative, "read payload");
                        }
                        if (!length) break;
                        content.append(buffer, static_cast<std::size_t>(length));
                        if (status.st_size < 0 || content.size() > static_cast<std::uintmax_t>(status.st_size))
                            throw Error(ErrorCode::ChangedTree, relative, "file grew while reading");
                    }
                    if (status.st_size < 0 || content.size() != static_cast<std::uintmax_t>(status.st_size))
                        throw Error(ErrorCode::ReadFailure, relative, "incomplete file content");
                    unchanged(status, inspect(readable.get(), relative), relative);
                    model.entries.push_back({relative, RegularFile{(status.st_mode & 0111) != 0, std::move(content)}});
                    observations.push_back({relative, status});
                }
            }
            unchanged(status, inspect(node.get(), relative), relative);
            struct stat named{};
            if (::fstatat(fd, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0)
                system_error(ErrorCode::InspectionFailure, relative, "recheck named node");
            unchanged(status, named, relative);
        }
        if (children != names(fd, path))
            throw Error(ErrorCode::ChangedTree, path, "directory membership changed");
        unchanged(before, inspect(fd, path), path);
    }
};
}

NormalizedArtifact scan(const std::filesystem::path& root)
{
    auto descriptor = open_root(root);
    Scan first;
    first.directory(descriptor.get(), {});
    first.require_complete_metadata();
    normalize(first.model);
    // Re-observe all nodes, metadata and bytes so changes to early entries
    // while later entries were read do not silently produce a mixed model.
    Scan second;
    second.directory(descriptor.get(), {});
    second.require_complete_metadata();
    normalize(second.model);
    if (first.observations.size() != second.observations.size())
        throw Error(ErrorCode::ChangedTree, {}, "tree membership changed between observations");
    for (std::size_t i = 0; i < first.observations.size(); ++i) {
        const auto& a = first.observations[i];
        const auto& b = second.observations[i];
        if (a.path != b.path) throw Error(ErrorCode::ChangedTree, a.path, "tree membership changed");
        unchanged(a.status, b.status, a.path);
    }
    if (first.model != second.model)
        throw Error(ErrorCode::ChangedTree, {}, "payload bytes changed between observations");
    auto named_root = open_root(root);
    unchanged(inspect(descriptor.get(), {}), inspect(named_root.get(), {}), {});
    return std::move(first.model);
}
} // namespace athena::artifact
