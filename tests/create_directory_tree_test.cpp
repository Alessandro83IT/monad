// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core/detail/filesystem.hpp"
#include "temporary_directory.hpp"
#include <cassert>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace athena::core::detail;
namespace fs = std::filesystem;

namespace {
int fail_chmod(int, mode_t) { errno = EIO; return -1; }
bool chmod_called = false;
int record_chmod(int fd, mode_t mode) { chmod_called = true; return ::fchmod(fd, mode); }
int replace_created_name(int parent, const char* name, mode_t mode) {
    if (::mkdirat(parent, name, mode) < 0) return -1;
    if (::renameat(parent, name, parent, ".layout-race-displaced") < 0) return -1;
    return ::symlinkat("/nonexistent-athena-layout-target", parent, name);
}
int replace_created_with_directory(int parent, const char* name, mode_t mode) {
    if (std::string_view(name) == "entry") return ::mkdirat(parent, name, mode);
    if (::mkdirat(parent, name, mode) < 0) return -1;
    if (::renameat(parent, name, parent, ".layout-race-displaced-dir") < 0) return -1;
    if (::mkdirat(parent, name, 0700) < 0) return -1;
    return ::fchmodat(parent, name, 0755, 0);
}
}

int main() {
    athena::test::TemporaryDirectory fixture("athena-layout-test");
    const auto root = fixture.path();

    // Symlink components are rejected without following or changing the target.
    const auto outside = root / "outside";
    fs::create_directory(outside);
    const auto link = root / "link";
    fs::create_directory_symlink(outside, link);
    bool rejected = false;
    try { create_directory_tree(link / "child", 0755); }
    catch (const std::exception&) { rejected = true; }
    assert(rejected && !fs::exists(outside / "child"));

    // Deterministically replace a just-created name before openat. The
    // descriptor-relative no-follow open rejects it before fchmod can run.
    const auto raced = root / "raced";
    auto race_ops = linux_syscalls();
    race_ops.mkdir = replace_created_name;
    race_ops.chmod = record_chmod;
    rejected = false;
    try { create_directory_tree(raced, 0755, false, race_ops); }
    catch (const std::exception&) { rejected = true; }
    bool injected_symlink = false;
    for (const auto& entry : fs::directory_iterator(root))
        injected_symlink = injected_symlink || entry.is_symlink();
    assert(rejected && !fs::exists(raced) && injected_symlink && !chmod_called);

    // Replacing the staging name with a real but publicly accessible directory
    // is rejected by descriptor ownership/mode validation before fchmod.
    const auto directory_race_root = root / "directory-race";
    fs::create_directory(directory_race_root);
    fs::permissions(directory_race_root, fs::perms::owner_all);
    auto directory_race_ops = linux_syscalls();
    directory_race_ops.mkdir = replace_created_with_directory;
    directory_race_ops.chmod = record_chmod;
    chmod_called = false;
    rejected = false;
    try { create_directory_tree(directory_race_root / "target", 0755, false, directory_race_ops); }
    catch (const std::exception&) { rejected = true; }
    bool public_replacement_found = false;
    for (const auto& entry : fs::directory_iterator(directory_race_root)) {
        if (entry.is_directory() && (entry.status().permissions() & fs::perms::others_read) != fs::perms::none)
            public_replacement_found = true;
    }
    assert(rejected && !fs::exists(directory_race_root / "target") && public_replacement_found && !chmod_called);

    // Existing unsafe shared and private leaves are rejected, never chmodded.
    const auto shared = root / "shared";
    fs::create_directory(shared);
    fs::permissions(shared, fs::perms::owner_all | fs::perms::group_write);
    rejected = false;
    std::string shared_error;
    try { create_directory_tree(shared, 0755); }
    catch (const std::exception& error) { rejected = true; shared_error = error.what(); }
    assert(rejected && (fs::status(shared).permissions() & fs::perms::group_write) != fs::perms::none);
    assert(shared_error.find(shared.string()) != std::string::npos && shared_error.find("writable") != std::string::npos);

    const auto private_dir = root / "private";
    fs::create_directory(private_dir);
    fs::permissions(private_dir, fs::perms::owner_all | fs::perms::others_read);
    rejected = false;
    std::string private_error;
    try { create_directory_tree(private_dir, 0700, true); }
    catch (const std::exception& error) { rejected = true; private_error = error.what(); }
    assert(rejected && (fs::status(private_dir).permissions() & fs::perms::others_read) != fs::perms::none);
    assert(private_error.find(private_dir.string()) != std::string::npos && private_error.find("mode 0700") != std::string::npos);

    const auto inaccessible_private = root / "inaccessible-private";
    fs::create_directory(inaccessible_private);
    fs::permissions(inaccessible_private, fs::perms::owner_read | fs::perms::owner_write);
    rejected = false;
    try { create_directory_tree(inaccessible_private, 0700, true); }
    catch (const std::exception&) { rejected = true; }
    assert(rejected && (fs::status(inaccessible_private).permissions() & fs::perms::all) ==
           (fs::perms::owner_read | fs::perms::owner_write));

    // An injected chmod failure leaves only a private staging directory; retry
    // does not accept it as the requested public path and can complete safely.
    const auto partial = root / "partial";
    auto ops = linux_syscalls();
    ops.chmod = fail_chmod;
    rejected = false;
    try { create_directory_tree(partial, 0755, false, ops); }
    catch (const std::system_error&) { rejected = true; }
    assert(rejected && !fs::exists(partial));
    create_directory_tree(partial, 0755);
    assert(fs::is_directory(partial));
    assert((fs::status(partial).permissions() & fs::perms::all) == static_cast<fs::perms>(0755));

    // Exercise the three umasks in subprocesses so the parent process is unchanged.
    for (const auto [mask, expected] : {std::pair{002u, 0755u}, {022u, 0755u}, {077u, 0755u}}) {
        const auto child = ::fork();
        assert(child >= 0);
        if (child == 0) {
            ::umask(static_cast<mode_t>(mask));
            try {
                const auto path = root / ("umask-" + std::to_string(mask));
                create_directory_tree(path, 0755);
                const auto mode = fs::status(path).permissions() & fs::perms::all;
                _exit(mode == static_cast<fs::perms>(expected) ? 0 : 1);
            } catch (...) { _exit(2); }
        }
        int status = 0;
        assert(::waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    // Ownership rejection without privileges: a world-writable ancestor is
    // outside the trust contract. Restore fixture permissions before teardown.
    fs::permissions(root, fs::perms::owner_all | fs::perms::others_write);
    const auto guarded = root / "guarded";
    rejected = false;
    try { create_directory_tree(guarded, 0755); }
    catch (const std::exception&) { rejected = true; }
    assert(rejected && !fs::exists(guarded));
    fs::permissions(root, fs::perms::owner_all);
}
