// SPDX-License-Identifier: GPL-3.0-or-later
#include "filesystem.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/fs.h>
#include <linux/magic.h>
#include <sys/vfs.h>
#include <sys/file.h>
#include <sys/syscall.h>
#include <sys/random.h>
#include <unistd.h>
#include <utility>
#include <array>
#include <system_error>

namespace athena::core::detail {
namespace {
[[noreturn]] void fail(const std::string& context) {
    throw std::runtime_error(context + ": " + std::strerror(errno));
}
int rename_linux(int a, const char* x, int b, const char* y, unsigned flags) {
    return flags ? static_cast<int>(::syscall(SYS_renameat2, a, x, b, y, flags)) : ::renameat(a, x, b, y);
}
int cloexec_linux(int fd) { return ::fcntl(fd, F_SETFD, FD_CLOEXEC); }
const Syscalls direct{::write, ::fsync, rename_linux, ::flock, ::fstat, ::getrandom, cloexec_linux, ::fstatfs};
void durable_parent(int fd, const Syscalls& ops) {
    directory(fd);
    struct statfs fs{};
    if (ops.fstatfs(fd, &fs) < 0) fail("durability filesystem validation");
    switch (fs.f_type) {
    case EXT4_SUPER_MAGIC: case XFS_SUPER_MAGIC: case BTRFS_SUPER_MAGIC:
    case TMPFS_MAGIC: return;
    default: throw std::runtime_error("unsupported/unknown durability filesystem");
    }
}
void barrier(int fd, PersistenceError::Stage stage, bool visible, const Syscalls& ops) {
    int result;
    do { result = ops.fsync(fd); } while (result < 0 && errno == EINTR);
    if (result < 0) throw PersistenceError(stage, visible, errno,
        stage == PersistenceError::Stage::FileBarrier ? "file barrier failure" : "directory barrier failure");
}
}
const Syscalls& linux_syscalls() { return direct; }
Fd Fd::adopt(int fd, const Syscalls& ops) {
    if (fd < 0) throw std::invalid_argument("cannot adopt invalid descriptor");
    if ((ops.cloexec ? ops.cloexec(fd) : cloexec_linux(fd)) < 0) {
        const int error = errno; ::close(fd); errno = error; fail("descriptor CLOEXEC");
    }
    return Fd(fd);
}
int Fd::release() noexcept { return std::exchange(fd_, -1); }
Fd::~Fd() { if (fd_ >= 0) ::close(fd_); } // Never retry close on Linux.
Fd::Fd(Fd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
Fd& Fd::operator=(Fd&& other) noexcept {
    if (this != &other) { if (fd_ >= 0) ::close(fd_); fd_ = std::exchange(other.fd_, -1); }
    return *this;
}
PersistenceError::PersistenceError(Stage s, bool uncertain, int e, std::string_view context)
    : std::runtime_error(std::string(context) + (uncertain ? " (OUTCOME UNCERTAIN): " : ": ") + std::strerror(e)),
      stage(s), outcome_uncertain(uncertain), error(e) {}
void leaf(std::string_view name) {
    if (name.empty() || name == "." || name == ".." || name.find('/') != name.npos || name.find('\0') != name.npos)
        throw std::invalid_argument("unsafe directory-relative leaf");
}
struct stat stat_fd(int fd) { struct stat s{}; if (::fstat(fd, &s) < 0) fail("fstat descriptor"); return s; }
void regular(int fd) { if (!S_ISREG(stat_fd(fd).st_mode)) throw std::runtime_error("descriptor is not a regular file"); }
void directory(int fd) { if (!S_ISDIR(stat_fd(fd).st_mode)) throw std::runtime_error("descriptor is not a directory"); }
Fd open_at(int parent, std::string_view name, int flags, mode_t mode) {
    directory(parent); leaf(name);
    if (flags & O_PATH) throw std::invalid_argument("openat O_PATH is unsupported by owned file/directory primitives");
    const std::string n(name);
    int fd = ::openat(parent, n.c_str(), flags | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, mode);
    if (fd < 0) fail("openat " + n);
    return Fd::adopt(fd);
}
void mkdir_at(int parent, std::string_view name, mode_t mode) {
    directory(parent); leaf(name); const std::string n(name);
    if (::mkdirat(parent, n.c_str(), mode) < 0 && errno != EEXIST) fail("mkdirat " + n);
    auto fd = open_at(parent, name, O_RDONLY | O_DIRECTORY); directory(fd.get());
}
Fd open_directory(const std::filesystem::path& path, bool create, bool trusted_ancestry) {
    if (!path.is_absolute()) throw std::invalid_argument("directory path must be absolute");
    int root_fd = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (root_fd < 0) fail("open root directory");
    auto fd = Fd::adopt(root_fd);
    // Namespace-visible root ownership may be mapped (including an overflow uid).
    const uid_t root_owner = stat_fd(fd.get()).st_uid;
    auto validate_ancestor = [trusted_ancestry, root_owner](int descriptor) {
        if (!trusted_ancestry) return;
        const auto s = stat_fd(descriptor);
        if ((s.st_uid != root_owner && s.st_uid != ::geteuid()) ||
            ((s.st_mode & 0022) && !(s.st_mode & S_ISVTX)))
            throw std::runtime_error("unsafe lock directory ancestor ownership or permissions");
    };
    validate_ancestor(fd.get());
    for (const auto& part : path.relative_path()) {
        const auto name = part.string(); leaf(name);
        if (create) mkdir_at(fd.get(), name);
        fd = open_at(fd.get(), name, O_RDONLY | O_DIRECTORY);
        validate_ancestor(fd.get());
    }
    directory(fd.get()); return fd;
}
void unlink_at(int parent, std::string_view name, bool is_directory) {
    directory(parent); leaf(name); const std::string n(name);
    if (::unlinkat(parent, n.c_str(), is_directory ? AT_REMOVEDIR : 0) < 0) fail("unlinkat " + n);
}
void complete_write(int fd, std::string_view data, const Syscalls& ops) {
    while (!data.empty()) {
        const auto n = ops.write(fd, data.data(), data.size());
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw PersistenceError(PersistenceError::Stage::Write, false, n == 0 ? EIO : errno, "write failure");
        if (static_cast<size_t>(n) > data.size()) throw std::logic_error("write returned invalid count");
        data.remove_prefix(static_cast<size_t>(n));
    }
}
void file_barrier(int fd, const Syscalls& ops) { regular(fd); barrier(fd, PersistenceError::Stage::FileBarrier, false, ops); }
void directory_barrier(int fd, bool visible, const Syscalls& ops) {
    struct stat s{};
    if (ops.fstat(fd, &s) < 0)
        throw PersistenceError(PersistenceError::Stage::DirectoryBarrier, visible, errno, "directory barrier validation failure");
    if (!S_ISDIR(s.st_mode))
        throw PersistenceError(PersistenceError::Stage::DirectoryBarrier, visible, ENOTDIR, "directory barrier validation failure");
    barrier(fd, PersistenceError::Stage::DirectoryBarrier, visible, ops);
}
void acquire_flock(int fd, int flags, const Syscalls& ops) {
    int result; do { result = ops.flock(fd, flags); } while (result < 0 && errno == EINTR);
    if (result < 0) throw std::system_error(errno, std::generic_category(), "flock acquisition");
}
void atomic_replace(int parent, std::string_view destination, std::string_view data, const Syscalls& ops) {
    durable_parent(parent, ops); leaf(destination); const std::string dest(destination);
    std::string temp;
    Fd fd;
    // Armed immediately at exclusive creation, before descriptor adoption can throw.
    struct Cleanup { int parent; const std::string& name; bool active = false;
        ~Cleanup() { if (active) ::unlinkat(parent, name.c_str(), 0); }
    } cleanup{parent, temp};
    for (;;) {
        std::array<unsigned char, 16> random{};
        size_t offset = 0;
        while (offset < random.size()) {
            const auto count = (ops.random ? ops.random : ::getrandom)(random.data() + offset, random.size() - offset, 0);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { if (count == 0) errno = EIO; fail("temporary name randomness"); }
            if (static_cast<size_t>(count) > random.size() - offset) throw std::logic_error("random returned invalid count");
            offset += static_cast<size_t>(count);
        }
        temp = ".athena-tmp-";
        constexpr char hex[] = "0123456789abcdef";
        for (auto c : random) { temp += hex[c >> 4]; temp += hex[c & 15]; }
        if (temp == dest) continue;
        int raw = ::openat(parent, temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (raw >= 0) { cleanup.active = true; fd = Fd::adopt(raw, ops); break; }
        if (errno != EEXIST) fail("create temporary");
    }
    complete_write(fd.get(), data, ops); file_barrier(fd.get(), ops);
    if (ops.rename(parent, temp.c_str(), parent, dest.c_str(), 0) < 0)
        throw PersistenceError(PersistenceError::Stage::Publication, false, errno, "rename/replacement failure");
    cleanup.active = false;
    directory_barrier(parent, true, ops);
}
Publication publish_noreplace(int parent, std::string_view prepared, std::string_view destination, const Syscalls& ops) {
    durable_parent(parent, ops); leaf(prepared); leaf(destination);
    const std::string source(prepared), dest(destination);
    if (source == dest) throw std::invalid_argument("publication source equals destination");
    struct stat s{};
    if (::fstatat(parent, source.c_str(), &s, AT_SYMLINK_NOFOLLOW) < 0) fail("prepared entry validation");
    if (!S_ISREG(s.st_mode) && !S_ISDIR(s.st_mode)) throw std::runtime_error("unsafe prepared entry type");
    if (ops.rename(parent, source.c_str(), parent, dest.c_str(), RENAME_NOREPLACE) < 0) {
        const int e = errno;
        if (e == EEXIST) return Publication::AlreadyExists;
        throw PersistenceError(PersistenceError::Stage::Publication, false, e, "rename/publication failure");
    }
    directory_barrier(parent, true, ops); return Publication::Published;
}
}
