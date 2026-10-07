// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core/detail/filesystem.hpp"
#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <unistd.h>
#include <utility>
#include <sys/file.h>
#include <cstring>
#include <linux/magic.h>
using namespace athena::core::detail;
namespace {
int calls = 0, fault = 0, random_calls = 0;
bool renamed = false;
int adoption_fd = -1, collision_stage = 0;
std::filesystem::path collision_destination;
ssize_t write_test(int fd, const void* data, size_t size) {
    ++calls;
    if (!collision_destination.empty()) assert(!std::filesystem::exists(collision_destination));
    if (fault == 1 && calls == 1) { errno = EINTR; return -1; }
    if (fault == 2) return 0;
    if (fault == 3 || (fault == 7 && calls > 1)) { errno = ENOSPC; return -1; }
    return ::write(fd, data, size > 2 ? 2 : size);
}
int fsync_test(int fd) {
    auto s = stat_fd(fd);
    if (!collision_destination.empty()) {
        if (S_ISREG(s.st_mode)) { assert(collision_stage == 0); collision_stage = 1; }
        else { assert(collision_stage == 2); collision_stage = 3; }
    }
    if ((fault == 4 && S_ISREG(s.st_mode)) || (fault == 5 && S_ISDIR(s.st_mode))) { errno = EIO; return -1; }
    return ::fsync(fd);
}
int rename_test(int a, const char* x, int b, const char* y, unsigned flags) {
    if (fault == 6) { errno = EIO; return -1; }
    const int result = linux_syscalls().rename(a, x, b, y, flags);
    renamed = result == 0;
    if (!collision_destination.empty()) { assert(std::string(x) != std::string(y) && collision_stage == 1); collision_stage = 2; }
    return result;
}
int flock_test(int fd, int flags) {
    if (++calls == 1) { errno = EINTR; return -1; }
    return ::flock(fd, flags);
}
int stat_test(int fd, struct stat* s) {
    if (fault == 8 && renamed) { errno = EIO; return -1; }
    return ::fstat(fd, s);
}
ssize_t random_test(void* data, size_t size, unsigned) {
    std::memset(data, random_calls++ == 0 ? 0 : 1, size); return size;
}
int cloexec_test(int fd) {
    if (fault == 9) { adoption_fd = fd; errno = EIO; return -1; }
    return ::fcntl(fd, F_SETFD, FD_CLOEXEC);
}
const Syscalls injected{write_test, fsync_test, rename_test, flock_test, stat_test, nullptr, cloexec_test};
long rejected_type = 0;
int unsupported_fs(int, struct statfs* fs) { *fs = {}; fs->f_type = rejected_type; return 0; }
std::string read_file(const std::filesystem::path& path) { std::ifstream f(path); return {std::istreambuf_iterator<char>(f), {}}; }
template<class F> void rejects(F f) { bool caught = false; try { f(); } catch (const std::exception&) { caught = true; } assert(caught); }
}
int main() {
    char pattern[] = "/tmp/athena-durable-XXXXXX";
    const auto raw_root = ::mkdtemp(pattern); assert(raw_root);
    const auto root = std::filesystem::path(raw_root);
    auto parent = open_directory(root);
    {
        auto fd = open_at(parent.get(), "move", O_CREAT | O_RDWR | O_EXCL);
        int raw = fd.get(); assert(::fcntl(raw, F_GETFD) & FD_CLOEXEC);
        Fd moved(std::move(fd)); assert(fd.get() == -1 && moved.get() == raw);
        auto assigned = open_at(parent.get(), "discarded", O_CREAT | O_RDWR | O_EXCL);
        const int discarded = assigned.get();
        assigned = std::move(moved); assert(moved.get() == -1);
        assert(::fcntl(discarded, F_GETFD) == -1 && errno == EBADF);
        complete_write(assigned.get(), "complete");
    }
    assert(read_file(root / "move") == "complete");
    int closed;
    { auto fd = open_at(parent.get(), "move", O_RDONLY); closed = fd.get(); }
    assert(::fcntl(closed, F_GETFD) == -1 && errno == EBADF);
    for (int mode : {0, 1, 2, 3, 7}) {
        auto fd = open_at(parent.get(), "write", O_CREAT | O_WRONLY | O_TRUNC);
        fault = mode; calls = 0;
        if (mode <= 1) { complete_write(fd.get(), "abcdef", injected); assert(calls >= 3); assert(read_file(root / "write") == "abcdef"); }
        else {
            try { complete_write(fd.get(), "abcdef", injected); assert(false); }
            catch (const PersistenceError& e) { assert(e.stage == PersistenceError::Stage::Write && !e.outcome_uncertain); }
        }
    }
    calls = 0; complete_write(parent.get(), "", injected); assert(calls == 0);
    fault = 0; atomic_replace(parent.get(), "record", "old");
    for (int mode : {3, 7, 4, 6, 5, 8}) {
        fault = 0; atomic_replace(parent.get(), "record", "old"); renamed = false;
        fault = mode; calls = 0;
        try { atomic_replace(parent.get(), "record", "new", injected); assert(false); }
        catch (const PersistenceError& e) {
            assert(e.outcome_uncertain == (mode == 5 || mode == 8));
            if (e.outcome_uncertain) assert(std::string(e.what()).find("OUTCOME UNCERTAIN") != std::string::npos);
            assert(e.stage == ((mode == 3 || mode == 7) ? PersistenceError::Stage::Write : mode == 4 ? PersistenceError::Stage::FileBarrier : mode == 6 ? PersistenceError::Stage::Publication : PersistenceError::Stage::DirectoryBarrier));
        }
        assert(read_file(root / "record") == ((mode == 5 || mode == 8) ? "new" : "old"));
        for (const auto& entry : std::filesystem::directory_iterator(root)) assert(!entry.path().filename().string().starts_with(".athena-tmp-"));
    }
    fault = 9;
    try { atomic_replace(parent.get(), "record", "bad", injected); assert(false); }
    catch (const std::runtime_error& e) { assert(std::string(e.what()).find("descriptor CLOEXEC") != std::string::npos); }
    assert(adoption_fd >= 0);
    assert(::fcntl(adoption_fd, F_GETFD) == -1 && errno == EBADF);
    assert(read_file(root / "record") == "new");
    for (const auto& entry : std::filesystem::directory_iterator(root)) assert(!entry.path().filename().string().starts_with(".athena-tmp-"));
    fault = 0; random_calls = 0;
    collision_destination = root / ".athena-tmp-00000000000000000000000000000000";
    auto collision_ops = injected; collision_ops.random = random_test;
    atomic_replace(parent.get(), collision_destination.filename().string(), "complete collision record", collision_ops);
    assert(random_calls == 2 && collision_stage == 3 && read_file(collision_destination) == "complete collision record");
    unlink_at(parent.get(), collision_destination.filename().string()); collision_destination.clear();
    fault = 0; atomic_replace(parent.get(), "record", "success", injected); assert(read_file(root / "record") == "success");
    auto prepare = [&](const char* name) { auto fd = open_at(parent.get(), name, O_CREAT | O_EXCL | O_WRONLY); complete_write(fd.get(), "immutable"); file_barrier(fd.get()); };
    prepare("unsupported-prepared");
    auto unsupported = injected; unsupported.fstatfs = unsupported_fs;
    for (auto type : {0L, long(OVERLAYFS_SUPER_MAGIC), long(NFS_SUPER_MAGIC), long(FUSE_SUPER_MAGIC)}) {
        rejected_type = type; calls = 0; random_calls = 0; renamed = false;
        unsupported.random = random_test;
        rejects([&] { atomic_replace(parent.get(), "record", "unsupported", unsupported); });
        rejects([&] { publish_noreplace(parent.get(), "unsupported-prepared", "unsupported-destination", unsupported); });
        assert(calls == 0 && random_calls == 0 && !renamed);
        assert(read_file(root / "record") == "success");
        assert(std::filesystem::exists(root / "unsupported-prepared") && !std::filesystem::exists(root / "unsupported-destination"));
        for (const auto& entry : std::filesystem::directory_iterator(root)) assert(!entry.path().filename().string().starts_with(".athena-tmp-"));
    }
    prepare("prepared");
    assert(publish_noreplace(parent.get(), "prepared", "published") == Publication::Published);
    prepare("collision");
    assert(publish_noreplace(parent.get(), "collision", "published") == Publication::AlreadyExists);
    assert(std::filesystem::exists(root / "collision"));
    fault = 6;
    try { publish_noreplace(parent.get(), "collision", "not-visible", injected); assert(false); }
    catch (const PersistenceError& e) { assert(!e.outcome_uncertain && e.stage == PersistenceError::Stage::Publication); }
    assert(!std::filesystem::exists(root / "not-visible"));
    fault = 5;
    try { publish_noreplace(parent.get(), "collision", "visible", injected); assert(false); }
    catch (const PersistenceError& e) { assert(e.outcome_uncertain && e.stage == PersistenceError::Stage::DirectoryBarrier); }
    assert(read_file(root / "visible") == "immutable");
    prepare("metadata-prepared"); fault = 8; renamed = false;
    try { publish_noreplace(parent.get(), "metadata-prepared", "metadata-visible", injected); assert(false); }
    catch (const PersistenceError& e) { assert(e.outcome_uncertain && e.error == EIO && e.stage == PersistenceError::Stage::DirectoryBarrier); }
    assert(read_file(root / "metadata-visible") == "immutable");
    fault = 5;
    try { directory_barrier(parent.get(), false, injected); assert(false); }
    catch (const PersistenceError& e) { assert(!e.outcome_uncertain); }
    atomic_replace(parent.get(), "direct-operations", "no global fault hook");
    fault = 0;
    assert(::symlink("move", (root / "link").c_str()) == 0);
    rejects([&] { open_at(parent.get(), "link", O_RDONLY); });
    rejects([&] { open_at(parent.get(), "link", O_PATH); });
    rejects([&] { publish_noreplace(parent.get(), "link", "bad"); });
    rejects([&] { open_directory(root / "link"); });
    for (auto name : {"../escape", "/tmp/escape", ".", ".."}) {
        rejects([&] { atomic_replace(parent.get(), name, "bad"); });
        rejects([&] { unlink_at(parent.get(), name); });
        rejects([&] { mkdir_at(parent.get(), name); });
    }
    mkdir_at(parent.get(), "dir"); auto dir = open_at(parent.get(), "dir", O_RDONLY | O_DIRECTORY); directory(dir.get());
    rejects([&] { regular(dir.get()); });
    unlink_at(parent.get(), "dir", true);
    atomic_replace(parent.get(), "link", "entry replacement");
    assert(!std::filesystem::is_symlink(root / "link"));
    assert(read_file(root / "move") == "complete");
    mkdir_at(parent.get(), "anchored");
    auto anchored = open_at(parent.get(), "anchored", O_RDONLY | O_DIRECTORY);
    std::filesystem::rename(root / "anchored", root / "shifted");
    mkdir_at(parent.get(), "anchored");
    atomic_replace(anchored.get(), "record", "anchored");
    assert(read_file(root / "shifted/record") == "anchored");
    assert(!std::filesystem::exists(root / "anchored/record"));
    mkdir_at(parent.get(), "prepared-directory");
    auto prepared_dir = open_at(parent.get(), "prepared-directory", O_RDONLY | O_DIRECTORY);
    directory_barrier(prepared_dir.get());
    assert(publish_noreplace(parent.get(), "prepared-directory", "published-directory") == Publication::Published);
    calls = 0; auto fd = open_at(parent.get(), "flock", O_CREAT | O_RDWR);
    acquire_flock(fd.get(), LOCK_EX | LOCK_NB, injected); assert(calls == 2);
    std::filesystem::remove_all(root);
}
