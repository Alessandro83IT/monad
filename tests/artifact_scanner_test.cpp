#include "artifact/scanner.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <fcntl.h>
#include <dirent.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/xattr.h>
#include <unistd.h>

using namespace athena::artifact;
namespace fs = std::filesystem;

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
struct TempDirectory {
    fs::path path;
    TempDirectory()
    {
        std::string pattern = (fs::temp_directory_path() / "athena-artifact-XXXXXX").native();
        const char* made = ::mkdtemp(pattern.data());
        require(made != nullptr, "mkdtemp failed");
        path = made;
    }
    ~TempDirectory() { std::error_code error; fs::remove_all(path, error); }
};
void write(const fs::path& path, const std::string& bytes, mode_t mode = 0644)
{
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    require(!stream.fail(), "fixture write failed");
    require(::chmod(path.c_str(), mode) == 0, "fixture chmod failed");
}
template<class Function>
void rejects(Function function, ErrorCode expected, const std::string& path)
{
    try { function(); }
    catch (const Error& error) {
        require(error.code() == expected, error.what());
        require(error.path() == path, "wrong offending byte path");
        return;
    }
    throw std::runtime_error("invalid filesystem accepted");
}

bool hidden_metadata_failure(const Error& error)
{
    return error.code() == ErrorCode::InspectionFailure &&
        std::string(error.what()).find("hidden trusted.*") != std::string::npos;
}

// A real scan is always attempted. Privilege absence is an explicit production
// failure, never a test-only bypass of metadata inspection.
bool compare_scan(const fs::path& root, NormalizedArtifact expected, const char* name)
{
    normalize(expected);
    try { require(scan(root) == expected, name); return true; }
    catch (const Error& error) {
        if (!hidden_metadata_failure(error)) throw;
        std::cout << "SKIP successful scan " << name << ": " << error.what() << '\n';
        return false;
    }
}

struct Snapshot {
    std::string path;
    struct stat status;
};
std::vector<Snapshot> snapshot(const fs::path& root)
{
    std::vector<Snapshot> result;
    const auto add = [&](const fs::path& node, std::string path) {
        struct stat status{};
        require(::lstat(node.c_str(), &status) == 0, "snapshot lstat failed");
        result.push_back({std::move(path), status});
    };
    struct CloseDirectory {
        void operator()(DIR* directory) const { ::closedir(directory); }
    };
    std::function<void(const fs::path&, const std::string&)> visit;
    visit = [&](const fs::path& node, const std::string& relative) {
        add(node, relative);
        if (!S_ISDIR(result.back().status.st_mode)) return;
        const int fd = ::open(node.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NOATIME | O_CLOEXEC);
        require(fd >= 0, "snapshot directory open failed");
        DIR* raw = ::fdopendir(fd);
        if (!raw) { ::close(fd); throw std::runtime_error("snapshot fdopendir failed"); }
        std::unique_ptr<DIR, CloseDirectory> directory(raw);
        for (;;) {
            errno = 0;
            const auto* entry = ::readdir(directory.get());
            if (!entry) { require(errno == 0, "snapshot readdir failed"); break; }
            const std::string name(entry->d_name);
            if (name != "." && name != "..")
                visit(node / name, relative.empty() ? name : relative + '/' + name);
        }
    };
    visit(root, "");
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return path_bytes_less(a.path, b.path);
    });
    return result;
}
void require_unchanged(const std::vector<Snapshot>& a, const std::vector<Snapshot>& b)
{
    require(a.size() == b.size(), "scan changed tree membership");
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i].status;
        const auto& y = b[i].status;
        require(a[i].path == b[i].path && x.st_dev == y.st_dev && x.st_ino == y.st_ino &&
            x.st_mode == y.st_mode && x.st_nlink == y.st_nlink && x.st_uid == y.st_uid &&
            x.st_gid == y.st_gid && x.st_size == y.st_size &&
            x.st_mtim.tv_sec == y.st_mtim.tv_sec && x.st_mtim.tv_nsec == y.st_mtim.tv_nsec &&
            x.st_ctim.tv_sec == y.st_ctim.tv_sec && x.st_ctim.tv_nsec == y.st_ctim.tv_nsec,
            "scan mutated source filesystem");
        if (!S_ISLNK(x.st_mode))
            require(x.st_atim.tv_sec == y.st_atim.tv_sec && x.st_atim.tv_nsec == y.st_atim.tv_nsec,
                "scan changed regular file/directory access time");
    }
}
}

int main()
{
    TempDirectory empty;
    bool success_available = false;
    try { success_available = compare_scan(empty.path, {}, "empty root"); }
    catch (const Error& error) {
        if (error.code() != ErrorCode::UnsupportedMetadata || !error.path().empty()) throw;
        // A host-inherited security label/semantic flag is itself forbidden by
        // v1. Do not strip it or bypass inspection to make fixtures acceptable.
        std::cout << "SKIP scanner fixtures: temporary root inherits unsupported metadata: "
            << error.what() << '\n';
        return 77;
    }
    if (!success_available)
        rejects([&] { (void)scan(empty.path); }, ErrorCode::InspectionFailure, "");

    TempDirectory invalid;
    rejects([&] { (void)scan(invalid.path / "missing"); }, ErrorCode::InvalidRoot, "");
    write(invalid.path / "file", "x");
    rejects([&] { (void)scan(invalid.path / "file"); }, ErrorCode::InvalidRoot, "");
    fs::create_directory_symlink(empty.path, invalid.path / "root-link");
    rejects([&] { (void)scan(invalid.path / "root-link"); }, ErrorCode::InvalidRoot, "");
    rejects([&] { (void)scan(invalid.path / "root-link/."); }, ErrorCode::InvalidRoot, "");
    rejects([&] { (void)scan(fs::path(std::string("a\0b", 3))); }, ErrorCode::InvalidRoot, "");
    rejects([] { (void)scan(""); }, ErrorCode::InvalidRoot, "");

    TempDirectory tree;
    fs::create_directories(tree.path / "usr/bin");
    fs::create_directories(tree.path / "usr/share/empty");
    fs::create_directory(tree.path / ".athena");
    write(tree.path / "usr/bin/tool", std::string("a\0\xff\n", 4), 0500);
    write(tree.path / "data", std::string("\0\x80\xff", 3), 0600);
    write(tree.path / "zero", "");
    write(tree.path / "sparse", "");
    require(::truncate((tree.path / "sparse").c_str(), 8192) == 0, "sparse fixture failed");
    write(tree.path / std::string("raw-\xff", 5), "raw");
    NormalizedArtifact expected{1, {
        {".athena", Directory{}}, {"usr", Directory{}}, {"usr/bin", Directory{}},
        {"usr/share", Directory{}}, {"usr/share/empty", Directory{}},
        {"usr/bin/tool", RegularFile{true, std::string("a\0\xff\n", 4)}},
        {"data", RegularFile{false, std::string("\0\x80\xff", 3)}},
        {"zero", RegularFile{}}, {std::string("raw-\xff", 5), RegularFile{false, "raw"}}
    }};
    expected.entries.push_back({"sparse", RegularFile{false, std::string(8192, '\0')}});
    const std::vector<std::pair<std::string, std::string>> links = {
        {"relative", "usr/bin/tool"}, {"absolute", "/usr/bin/tool"},
        {"dangling", "missing"}, {"dotdot", "usr/../data"},
        {"escape", "../../outside"}, {"literal", "a//./b/"},
        {"raw-link", std::string("../\xff", 4)}, {"cycle", "cycle"}, {"dir-link", "usr"}
    };
    for (const auto& [name, target] : links) {
        require(::symlink(target.c_str(), (tree.path / name).c_str()) == 0, "fixture symlink failed");
        expected.entries.push_back({name, SymbolicLink{target}});
    }
    // Non-owner execute bits also count; ordinary directory modes normalize.
    write(tree.path / "group-exec", "g", 0610);
    write(tree.path / "other-exec", "o", 0601);
    expected.entries.push_back({"group-exec", RegularFile{true, "g"}});
    expected.entries.push_back({"other-exec", RegularFile{true, "o"}});
    require(::chmod((tree.path / "usr/share/empty").c_str(), 0700) == 0, "directory chmod failed");
    const auto before = snapshot(tree.path);
    compare_scan(tree.path, expected, "directories, modes, binary bytes and literal symlinks");
    require_unchanged(before, snapshot(tree.path));
    // Reopen contents and links only after the side-effect snapshot comparison.
    std::ifstream bytes(tree.path / "data", std::ios::binary);
    require(std::string(std::istreambuf_iterator<char>(bytes), {}) == std::string("\0\x80\xff", 3), "payload rewritten");
    for (const auto& [name, target] : links)
        require(fs::read_symlink(tree.path / name).native() == target, "symlink rewritten");

    TempDirectory order_a;
    TempDirectory order_b;
    std::vector<std::string> filenames = {"z", "a", std::string(1, '\xff'), std::string(1, '\x80')};
    NormalizedArtifact ordered;
    for (const auto& name : filenames) {
        write(order_a.path / name, name);
        ordered.entries.push_back({name, RegularFile{false, name}});
    }
    std::reverse(filenames.begin(), filenames.end());
    for (const auto& name : filenames) write(order_b.path / name, name, 0600);
    compare_scan(order_a.path, ordered, "creation order A");
    compare_scan(order_b.path, ordered, "creation order B and ordinary permission normalization");
    require(::chmod(order_b.path.c_str(), 0755) == 0, "root chmod failed");
    const timespec times[2]{{123, 0}, {456, 0}};
    require(::utimensat(AT_FDCWD, (order_b.path / "a").c_str(), times, 0) == 0, "timestamp fixture failed");
    compare_scan(order_b.path, ordered, "timestamps and root ordinary mode ignored");

    TempDirectory hard;
    write(hard.path / "a", "linked");
    fs::create_hard_link(hard.path / "a", hard.path / "b");
    rejects([&] { (void)scan(hard.path); }, ErrorCode::HardLinkTopology, "a");
    TempDirectory inside;
    TempDirectory outside;
    write(inside.path / "file", "linked");
    fs::create_hard_link(inside.path / "file", outside.path / "outside");
    rejects([&] { (void)scan(inside.path); }, ErrorCode::HardLinkTopology, "file");
    TempDirectory linked_symlink;
    fs::create_symlink("missing", linked_symlink.path / "a");
    require(::link((linked_symlink.path / "a").c_str(), (linked_symlink.path / "b").c_str()) == 0, "link symlink fixture failed");
    rejects([&] { (void)scan(linked_symlink.path); }, ErrorCode::HardLinkTopology, "a");

    TempDirectory fifo;
    require(::mkfifo((fifo.path / "fifo").c_str(), 0600) == 0, "mkfifo failed");
    rejects([&] { (void)scan(fifo.path); }, ErrorCode::UnsupportedFileType, "fifo");
    TempDirectory socket_root;
    const int socket_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    require(socket_fd >= 0, "socket fixture failed");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto socket_path = (socket_root.path / "socket").native();
    require(socket_path.size() < sizeof(address.sun_path), "socket fixture path too long");
    std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
    const int bound = ::bind(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    const int bind_error = errno;
    ::close(socket_fd);
    if (bound != 0) {
        require(bind_error == EPERM || bind_error == EACCES || bind_error == EOPNOTSUPP,
            "unexpected bind fixture failure");
        std::cout << "SKIP socket fixture: host prohibits Unix socket binding\n";
    } else rejects([&] { (void)scan(socket_root.path); }, ErrorCode::UnsupportedFileType, "socket");

    for (const mode_t special : {S_ISUID, S_ISGID, S_ISVTX}) {
        TempDirectory permissions;
        write(permissions.path / "file", "x", 0644 | special);
        rejects([&] { (void)scan(permissions.path); }, ErrorCode::UnsupportedPermissions, "file");
        fs::remove(permissions.path / "file");
        fs::create_directory(permissions.path / "dir");
        require(::chmod((permissions.path / "dir").c_str(), 0700 | special) == 0, "special directory chmod failed");
        rejects([&] { (void)scan(permissions.path); }, ErrorCode::UnsupportedPermissions, "dir");
        require(::chmod((permissions.path / "dir").c_str(), 0700) == 0, "restore directory mode failed");
        require(::chmod(permissions.path.c_str(), 0700 | special) == 0, "special root chmod failed");
        rejects([&] { (void)scan(permissions.path); }, ErrorCode::UnsupportedPermissions, "");
        require(::chmod(permissions.path.c_str(), 0700) == 0, "restore root mode failed");
    }

    for (const std::string name : {"", "file", "dir"}) {
        TempDirectory metadata;
        fs::path node = metadata.path;
        if (name == "file") { node /= name; write(node, "x"); }
        if (name == "dir") { node /= name; fs::create_directory(node); }
        if (::setxattr(node.c_str(), "user.athena-test", "", 0, 0) != 0) {
            require(errno == EOPNOTSUPP || errno == EPERM || errno == EACCES, "unexpected xattr fixture failure");
            std::cout << "SKIP xattr fixture: host facility unavailable\n";
        } else rejects([&] { (void)scan(metadata.path); }, ErrorCode::UnsupportedMetadata, name);
    }

    TempDirectory link_metadata;
    fs::create_symlink("missing", link_metadata.path / "link");
    if (::lsetxattr((link_metadata.path / "link").c_str(), "user.athena-test", "", 0, 0) == 0)
        rejects([&] { (void)scan(link_metadata.path); }, ErrorCode::UnsupportedMetadata, "link");
    else {
        require(errno == EOPNOTSUPP || errno == EPERM || errno == EACCES, "unexpected symlink xattr fixture failure");
        std::cout << "SKIP symlink xattr fixture: host facility unavailable\n";
    }

    TempDirectory trusted;
    if (::setxattr(trusted.path.c_str(), "trusted.athena-test", "", 0, 0) == 0)
        rejects([&] { (void)scan(trusted.path); }, ErrorCode::UnsupportedMetadata, "");
    else {
        require(errno == EOPNOTSUPP || errno == EPERM || errno == EACCES, "unexpected trusted xattr fixture failure");
        std::cout << "SKIP trusted xattr fixture: host facility/privilege unavailable\n";
    }

    TempDirectory capabilities;
    write(capabilities.path / "file", "x");
    const unsigned char capability[] = {
        1,0,0,2, 0,4,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
    }; // Linux VFS_CAP_REVISION_2, effective CAP_NET_BIND_SERVICE.
    if (::setxattr((capabilities.path / "file").c_str(), "security.capability", capability, sizeof(capability), 0) == 0)
        rejects([&] { (void)scan(capabilities.path); }, ErrorCode::UnsupportedMetadata, "file");
    else {
        require(errno == EOPNOTSUPP || errno == EPERM || errno == EACCES, "unexpected capability fixture failure");
        std::cout << "SKIP capability fixture: host facility/privilege unavailable\n";
    }

    // NODUMP affects metadata behavior and is not an allocation-only flag.
    // Restore the fixture outside the scanner before temporary-root cleanup.
    TempDirectory inode_flags;
    write(inode_flags.path / "file", "x");
    const int flag_fd = ::open((inode_flags.path / "file").c_str(), O_RDONLY | O_CLOEXEC);
    require(flag_fd >= 0, "flags fixture open failed");
    int original_flags = 0;
    if (::ioctl(flag_fd, FS_IOC_GETFLAGS, &original_flags) == 0) {
        int unsupported_flags = original_flags | FS_NODUMP_FL;
        if (::ioctl(flag_fd, FS_IOC_SETFLAGS, &unsupported_flags) == 0) {
            rejects([&] { (void)scan(inode_flags.path); }, ErrorCode::UnsupportedMetadata, "file");
            require(::ioctl(flag_fd, FS_IOC_SETFLAGS, &original_flags) == 0, "flags fixture restore failed");
        } else {
            require(errno == EOPNOTSUPP || errno == ENOTTY || errno == EPERM || errno == EACCES,
                "unexpected flags fixture set failure");
            std::cout << "SKIP inode flag fixture: host facility/privilege unavailable\n";
        }
    } else {
        require(errno == EOPNOTSUPP || errno == ENOTTY || errno == EPERM || errno == EACCES,
            "unexpected flags fixture inspection failure");
        std::cout << "SKIP inode flag fixture: host facility unavailable\n";
    }
    ::close(flag_fd);

    // Linux ACL xattr fixture: a named user makes this a nontrivial access ACL.
    // Version 2 and entries are little endian per the Linux POSIX ACL interface.
    unsigned char acl[] = {
        2,0,0,0, 1,0,7,0,255,255,255,255, 2,0,4,0,254,255,0,0,
        4,0,5,0,255,255,255,255, 16,0,5,0,255,255,255,255, 32,0,5,0,255,255,255,255
    };
    const auto acl_user = static_cast<std::uint32_t>(::geteuid());
    for (unsigned int i = 0; i < 4; ++i) acl[16 + i] = static_cast<unsigned char>(acl_user >> (i * 8));
    for (const char* attribute : {"system.posix_acl_access", "system.posix_acl_default"}) {
        TempDirectory acl_root;
        if (::setxattr(acl_root.path.c_str(), attribute, acl, sizeof(acl), 0) == 0)
            rejects([&] { (void)scan(acl_root.path); }, ErrorCode::UnsupportedMetadata, "");
        else {
            if (errno != EOPNOTSUPP && errno != EPERM && errno != EACCES)
                throw std::runtime_error(std::string("unexpected ACL fixture failure: ") + std::strerror(errno));
            std::cout << "SKIP ACL fixture: host facility unavailable\n";
        }
    }

    TempDirectory unreadable;
    write(unreadable.path / "file", "secret", 0000);
    if (::geteuid() != 0)
        rejects([&] { (void)scan(unreadable.path); }, ErrorCode::ReadFailure, "file");
    else std::cout << "SKIP unreadable-file fixture: root can read mode 0000\n";
    require(::chmod((unreadable.path / "file").c_str(), 0600) == 0, "restore file permissions failed");
    std::cout << "Artifact scanner rejection/read-only tests passed; successful scans "
        << (success_available ? "available" : "require unavailable metadata inspection privilege") << ".\n";
}
