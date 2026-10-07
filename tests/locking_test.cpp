// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <athena/core/locking.hpp>
#include "core/detail/filesystem.hpp"
#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <memory>
#include <thread>
#include <fstream>
#include <chrono>
#include <sstream>
#include <charconv>
#include <sys/sysmacros.h>
#include <sys/file.h>
#include <system_error>
#include <atomic>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
using namespace athena::core;
namespace {
std::string executable;
void inherit(int fd) { assert(::fcntl(fd, F_SETFD, 0) == 0); }
void send(int fd) { char c = 'x'; assert(::write(fd, &c, 1) == 1); }
void receive(int fd) { char c; assert(::read(fd, &c, 1) == 1); }
int wait_child(pid_t pid) { int status; assert(::waitpid(pid, &status, 0) == pid); assert(WIFEXITED(status)); return WEXITSTATUS(status); }
bool number(std::string_view text, std::uint64_t& value, int base = 10) {
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
bool blocked_flock(std::string_view line, pid_t pid, const struct stat& expected, pid_t process = 0) {
    std::istringstream record{std::string(line)};
    std::string id, waiting, type, policy, mode, owner, identity, start, end, extra;
    if (!(record >> id >> waiting >> type >> policy >> mode >> owner >> identity >> start >> end) ||
        (record >> extra)) return false;
    if (id.empty() || id.back() != ':' || waiting != "->" || type != "FLOCK" ||
        policy != "ADVISORY" || mode != "WRITE" || start != "0" || end != "EOF") return false;
    std::uint64_t index, owner_pid, device_major, device_minor, inode;
    if (!number(std::string_view(id).substr(0, id.size() - 1), index) || !number(owner, owner_pid) ||
        (owner_pid != static_cast<std::uint64_t>(pid) &&
         (!process || owner_pid != static_cast<std::uint64_t>(process)))) return false;
    const auto first = identity.find(':'), second = identity.find(':', first == identity.npos ? 0 : first + 1);
    if (first == identity.npos || second == identity.npos || identity.find(':', second + 1) != identity.npos) return false;
    const std::string_view device(identity);
    // /proc/locks uses hexadecimal major/minor, but a decimal inode.
    return number(device.substr(0, first), device_major, 16) &&
        number(device.substr(first + 1, second - first - 1), device_minor, 16) &&
        number(device.substr(second + 1), inode) &&
        device_major == ::major(expected.st_dev) && device_minor == ::minor(expected.st_dev) &&
        inode == expected.st_ino;
}
void parser_regressions() {
    struct stat expected{}; expected.st_dev = ::makedev(0xab, 0x123); expected.st_ino = 9523;
    assert(blocked_flock("24: -> FLOCK ADVISORY WRITE 900000001 ab:123:9523 0 EOF", 900000001, expected));
    assert(!blocked_flock("24: -> FLOCK ADVISORY WRITE 900000001 ab:123:9524 0 EOF", 900000001, expected));
    assert(!blocked_flock("24: -> FLOCK ADVISORY WRITE 3 ab:123:9523 0 EOF", 900000001, expected));
    assert(!blocked_flock("24: -> POSIX ADVISORY WRITE 900000001 ab:123:9523 0 EOF", 900000001, expected));
    assert(!blocked_flock("24: FLOCK ADVISORY WRITE 900000001 ab:123:9523 0 EOF", 900000001, expected));
    assert(!blocked_flock("24: -> FLOCK ADVISORY WRITE 900000001 ac:123:9523 0 EOF", 900000001, expected));
    assert(!blocked_flock("24: -> FLOCK ADVISORY WRITE 900000001 ab:124:9523 0 EOF", 900000001, expected));
    assert(!blocked_flock("24: -> FLOCK ADVISORY WRITE 900000001 ab:123:9523junk 0 EOF", 900000001, expected));
    assert(!blocked_flock("24: -> FLOCK ADVISORY WRITE 900000001 ab:123:9523 0 EOF extra", 900000001, expected));
    expected.st_dev = ::makedev(0, 1);
    assert(!blocked_flock("24: -> POSIX ADVISORY WRITE 3 00:01:9523 900000001 900000001", 900000001, expected));
    assert(blocked_flock("24: -> FLOCK ADVISORY WRITE 3 00:01:9523 0 EOF", 900000001, expected, 3));
}
void wait_for_kernel_waiter(pid_t pid, const std::filesystem::path& path, pid_t process = 0) {
    // Retain a descriptor to the exact fixture inode while observing its wait queue.
    auto parent = athena::core::detail::open_directory(path.parent_path());
    auto lock = athena::core::detail::open_at(parent.get(), path.filename().string(), O_RDONLY);
    const auto expected = athena::core::detail::stat_fd(lock.get());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        std::ifstream locks("/proc/locks"); assert(locks.is_open());
        std::string line;
        while (std::getline(locks, line)) {
            if (blocked_flock(line, pid, expected, process)) return;
        }
        assert(!locks.bad());
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}
int contender(const std::filesystem::path& path, LockMode mode) {
    auto pid = ::fork(); assert(pid >= 0);
    if (!pid) { ::execl(executable.c_str(), executable.c_str(), "contend", path.c_str(), mode == LockMode::Shared ? "shared" : "exclusive", nullptr); ::_exit(99); }
    return wait_child(pid);
}
struct Worker {
    pid_t pid; int ready; int finish;
    Worker(const std::filesystem::path& path, LockMode mode) {
        int a[2], b[2]; assert(::pipe2(a, O_CLOEXEC) == 0 && ::pipe2(b, O_CLOEXEC) == 0);
        pid = ::fork(); assert(pid >= 0);
        if (!pid) {
            ::close(a[0]); ::close(b[1]);
            inherit(a[1]); inherit(b[0]);
            auto output = std::to_string(a[1]), input = std::to_string(b[0]);
            ::execl(executable.c_str(), executable.c_str(), "worker", path.c_str(), mode == LockMode::Shared ? "shared" : "exclusive", output.c_str(), input.c_str(), nullptr); ::_exit(99);
        }
        ::close(a[1]); ::close(b[0]); ready = a[0]; finish = b[1]; receive(ready);
    }
    void stop(bool killed) {
        if (killed) assert(::kill(pid, SIGKILL) == 0); else send(finish);
        int status; assert(::waitpid(pid, &status, 0) == pid);
        assert(killed ? WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL : WIFEXITED(status) && WEXITSTATUS(status) == 0);
        ::close(ready); ::close(finish);
    }
};
template<class F> void rejects_order(F f) { bool rejected = false; try { f(); } catch (const std::logic_error&) { rejected = true; } assert(rejected); }
template<class F> void rejects(F f) { bool rejected = false; try { f(); } catch (const std::exception&) { rejected = true; } assert(rejected); }
}
int main(int argc, char** argv) {
    executable = std::filesystem::canonical("/proc/self/exe").string();
    if (argc > 1) {
        const std::string action = argv[1];
        const auto mode = std::string(argv[3]) == "shared" ? LockMode::Shared : LockMode::Exclusive;
        if (action == "contend") {
            try { FileLock lock(argv[2], LockKind::State, mode, "probe", false); return 0; }
            catch (const std::system_error& e) { return e.code().value() == EWOULDBLOCK || e.code().value() == EAGAIN ? 1 : 98; }
        }
        if (action == "exec") {
            FileLock lock(argv[2], LockKind::State);
            ::execl(executable.c_str(), executable.c_str(), "contend", argv[2], "exclusive", nullptr); return 99;
        }
        if (action == "waiter") {
            // Prove actual contention before signalling the holder's controller.
            try { FileLock probe(argv[2], LockKind::State, mode, "probe", false); return 97; }
            catch (const std::system_error& e) { assert(e.code().value() == EWOULDBLOCK || e.code().value() == EAGAIN); }
            send(std::stoi(argv[4]));
        }
        FileLock lock(argv[2], LockKind::State, mode);
        send(std::stoi(argv[4])); receive(std::stoi(argv[5])); return 0;
    }
    parser_regressions();
    char pattern[] = "/tmp/athena-locks-XXXXXX";
    auto raw = ::mkdtemp(pattern); assert(raw);
    const std::filesystem::path root(raw), path = root / "lock";
    for (bool killed : {false, true}) {
        Worker worker(path, LockMode::Exclusive);
        assert(contender(path, LockMode::Exclusive) == 1);
        assert(contender(path, LockMode::Shared) == 1);
        worker.stop(killed); assert(contender(path, LockMode::Exclusive) == 0);
    }
    { Worker worker(path, LockMode::Shared);
      assert(contender(path, LockMode::Shared) == 0);
      assert(contender(path, LockMode::Exclusive) == 1); worker.stop(false); }
    // Blocking waiter reports only after the holder exits. Pipe handshake replaces sleeps.
    { Worker worker(path, LockMode::Exclusive);
      int a[2], b[2]; assert(::pipe2(a, O_CLOEXEC) == 0 && ::pipe2(b, O_CLOEXEC) == 0);
      auto pid = ::fork(); assert(pid >= 0);
      if (!pid) { ::close(a[0]); ::close(b[1]); inherit(a[1]); inherit(b[0]); auto x = std::to_string(a[1]), y = std::to_string(b[0]);
        ::execl(executable.c_str(), executable.c_str(), "waiter", path.c_str(), "exclusive", x.c_str(), y.c_str(), nullptr); ::_exit(99); }
      ::close(a[1]); ::close(b[0]); receive(a[0]);
      wait_for_kernel_waiter(pid, path);
      worker.stop(false); receive(a[0]); send(b[1]); assert(wait_child(pid) == 0); ::close(a[0]); ::close(b[1]); }
    // A fork-inherited descriptor keeps the lock after the parent's RAII owner closes.
    {
        auto lock = std::make_unique<FileLock>(path, LockKind::State);
        int ready[2], finish[2]; assert(::pipe2(ready, O_CLOEXEC) == 0 && ::pipe2(finish, O_CLOEXEC) == 0);
        auto pid = ::fork(); assert(pid >= 0);
        if (!pid) { ::close(ready[0]); ::close(finish[1]); send(ready[1]); receive(finish[0]); lock.reset(); ::_exit(0); }
        ::close(ready[1]); ::close(finish[0]); receive(ready[0]); lock.reset();
        assert(contender(path, LockMode::Exclusive) == 1);
        send(finish[1]); assert(wait_child(pid) == 0);
        assert(contender(path, LockMode::Exclusive) == 0); ::close(ready[0]); ::close(finish[1]);
    }
    auto pid = ::fork(); assert(pid >= 0);
    if (!pid) { ::execl(executable.c_str(), executable.c_str(), "exec", path.c_str(), "exclusive", nullptr); ::_exit(99); }
    assert(wait_child(pid) == 0); // CLOEXEC drops the last reference.
    {
        FileLock first(path, LockKind::State);
        rejects_order([&] { FileLock duplicate(path, LockKind::State); });
        rejects_order([&] { FileLock duplicate_shared(path, LockKind::State, LockMode::Shared); });
        rejects_order([&] { FileLock same_rank(root / "different", LockKind::State); });
        rejects_order([&] { FileLock alias(std::filesystem::path(root.string() + "//lock"), LockKind::StoreGc); });
        FileLock moved(std::move(first)); assert(!first.owns_lock() && moved.owns_lock());
        FileLock gc(root / "gc", LockKind::StoreGc);
        rejects_order([&] { FileLock reverse(root / "state", LockKind::State); });
        rejects_order([&] { LockSet separate; separate.acquire(root / "other", LockKind::State, LockMode::Exclusive, "order"); });
        std::thread thread([&] {
            rejects_order([&] { FileLock duplicate(path, LockKind::State); });
            rejects_order([&] { FileLock reverse(root / "thread-state", LockKind::State); });
        });
        thread.join();
        moved = std::move(gc); assert(!gc.owns_lock());
        assert(contender(path, LockMode::Exclusive) == 0);
    }
    { FileLock only_gc(root / "gc", LockKind::StoreGc);
      rejects_order([&] { FileLock reverse(root / "reverse-only", LockKind::State); });
      rejects_order([&] { LockSet separate; separate.acquire(root / "reverse-helper", LockKind::State, LockMode::Exclusive, "reverse"); });
    }
    { FileLock independent(root / "different", LockKind::State); }
    { FileLock independent(path, LockKind::State); }
    // Failed nonblocking acquisition removes its reservation before retrying.
    { Worker worker(path, LockMode::Exclusive);
      try { FileLock failed(path, LockKind::State, LockMode::Exclusive, "rollback", false); assert(false); }
      catch (const std::system_error& e) { assert(e.code().value() == EWOULDBLOCK || e.code().value() == EAGAIN); }
      { FileLock unrelated(root / "rollback-other", LockKind::State); }
      worker.stop(false);
      FileLock retried(path, LockKind::State);
    }
    // Pending acquisitions reserve rank and inode before waiting in the kernel.
    { Worker worker(path, LockMode::Exclusive);
      std::atomic<pid_t> tid{0};
      std::thread pending([&] { tid = ::syscall(SYS_gettid); FileLock lock(path, LockKind::State); });
      while (!tid.load()) std::this_thread::yield();
      wait_for_kernel_waiter(tid.load(), path, ::getpid());
      rejects_order([&] { FileLock same_rank(root / "pending-other", LockKind::State); });
      rejects_order([&] { FileLock duplicate(path, LockKind::State); });
      worker.stop(false); pending.join();
    }
    { FileLock after_pending(path, LockKind::State); }
    assert(::symlink("lock", (root / "symlink").c_str()) == 0);
    rejects([&] { FileLock unsafe(root / "symlink", LockKind::State); });
    std::filesystem::create_directory(root / "directory");
    rejects([&] { FileLock unsafe(root / "directory", LockKind::State); });
    assert(::mkfifo((root / "fifo").c_str(), 0600) == 0);
    rejects([&] { FileLock unsafe(root / "fifo", LockKind::State); });
    assert(::link(path.c_str(), (root / "hardlink").c_str()) == 0);
    rejects([&] { FileLock unsafe(path, LockKind::State); });
    ::unlink((root / "hardlink").c_str());
    assert(::symlink("directory", (root / "parent-link").c_str()) == 0);
    rejects([&] { FileLock unsafe(root / "parent-link" / "lock", LockKind::State); });
    assert(::chmod(path.c_str(), 0666) == 0);
    rejects([&] { FileLock unsafe(path, LockKind::State); });
    assert(::chmod(path.c_str(), 0600) == 0);
    std::filesystem::create_directory(root / "writable");
    assert(::chmod((root / "writable").c_str(), 0777) == 0);
    rejects([&] { FileLock unsafe(root / "writable" / "lock", LockKind::State); });
    // A secure leaf cannot compensate for an ancestor that permits entry replacement.
    std::filesystem::create_directory(root / "writable" / "private");
    assert(::chmod((root / "writable" / "private").c_str(), 0700) == 0);
    rejects([&] { FileLock unsafe(root / "writable" / "private" / "lock", LockKind::State); });
    assert(!std::filesystem::exists(root / "writable" / "private" / "lock"));
    assert(::chmod((root / "writable").c_str(), 01777) == 0);
    { FileLock sticky_safe(root / "writable" / "private" / "lock", LockKind::State); }
    const auto authority = root / "generations";
    assert(state_lock_path(authority) == authority / ".athena-locks/state.lock");
    assert(store_gc_lock_path(root / "store") == root / "store/.athena-locks/gc.lock");
    { FileLock state(state_lock_path(authority), LockKind::State); }
    std::filesystem::remove_all(root);
}
