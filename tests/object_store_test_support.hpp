// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "store/object_store.hpp"
#include "store/object_store_detail.hpp"
#include "artifact/identity.hpp"
#include "artifact_test_support.hpp"
#include <chrono>
#include <fstream>
#include <functional>
#include <map>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
namespace fs = std::filesystem;
using athena::store::ObjectError;
using athena::store::ObjectErrorCode;
using athena::store::ObjectStore;
namespace mechanics = athena::store::object_detail;
using namespace athena::artifact;
struct StoreFixture {
    fs::path root;
    StoreFixture() {
        std::string pattern = (fs::temp_directory_path() / "athena-object-XXXXXX").native();
        require(::mkdtemp(pattern.data()) != nullptr, "fixture mkdtemp"); root = pattern;
    }
    ~StoreFixture() {
        std::error_code error;
        for (const auto& entry : fs::recursive_directory_iterator(root))
            if (fs::is_directory(entry.symlink_status())) ::chmod(entry.path().c_str(), 0700);
        fs::remove_all(root, error);
    }
};
inline NormalizedArtifact payload_model() {
    NormalizedArtifact model{1, {
        {".athena", Directory{}}, {".athena/data", RegularFile{false, "ordinary payload"}},
        {"absolute", SymbolicLink{"/outside/target"}}, {"bin", Directory{}},
        {"bin/tool", RegularFile{true, "hello\n"}},
        {"binary", RegularFile{false, std::string("\0\xff\n", 3)}},
        {"empty", Directory{}}, {"escaping", SymbolicLink{"../outside//target/"}},
        {"link", SymbolicLink{"bin/tool"}}, {std::string("raw-\xff", 5), RegularFile{false, "bytes"}}
    }};
    normalize(model); return model;
}
inline std::string read_bytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "read payload");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
inline mode_t mode(const fs::path& path) {
    struct stat value{}; require(::lstat(path.c_str(), &value) == 0, "stat fixture"); return value.st_mode & 07777;
}
inline std::map<std::string, struct stat> snapshot(const fs::path& root) {
    std::map<std::string, struct stat> result;
    require(::lstat(root.c_str(), &result[""]) == 0, "snapshot root");
    for (const auto& entry : fs::recursive_directory_iterator(root))
        require(::lstat(entry.path().c_str(), &result[entry.path().lexically_relative(root).native()]) == 0, "snapshot entry");
    return result;
}
inline void unchanged(const fs::path& root, const std::map<std::string, struct stat>& before) {
    const auto after = snapshot(root);
    require(before.size() == after.size(), "source membership changed");
    for (const auto& [path, a] : before) {
        const auto found = after.find(path); require(found != after.end(), "source path changed");
        const auto& b = found->second;
        require(a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_mode == b.st_mode && a.st_nlink == b.st_nlink &&
                a.st_uid == b.st_uid && a.st_gid == b.st_gid && a.st_size == b.st_size &&
                a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
                a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec, "source metadata mutated");
    }
}
inline void exact_payload(const fs::path& root, const NormalizedArtifact& model, bool hardened) {
    std::size_t count = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root)) { (void)entry; ++count; }
    require(count == model.entries.size(), "unexpected payload entries");
    require(mode(root) == (hardened ? 0555 : 0755), "root permissions");
    for (const auto& entry : model.entries) {
        const auto path = root / entry.path;
        const auto status = fs::symlink_status(path);
        if (std::holds_alternative<Directory>(entry.value)) {
            require(fs::is_directory(status), "directory type");
            require(mode(path) == (hardened ? 0555 : 0755), "directory mode");
        } else if (const auto* file = std::get_if<RegularFile>(&entry.value)) {
            require(fs::is_regular_file(status), "regular type");
            require(read_bytes(path) == file->content, "file bytes");
            require(mode(path) == (file->executable ? (hardened ? 0555 : 0755) : (hardened ? 0444 : 0644)), "file mode");
            struct stat value{}; require(::lstat(path.c_str(), &value) == 0 && value.st_nlink == 1, "no hardlinks");
        } else {
            require(fs::is_symlink(status), "symlink type");
            require(fs::read_symlink(path).native() == std::get<SymbolicLink>(entry.value).target, "literal link bytes");
        }
    }
}
template<class Action> void rejects(ObjectErrorCode code, Action action) {
    try { action(); } catch (const ObjectError& error) {
        require(error.code() == code, "wrong object error category"); return;
    }
    throw std::runtime_error("expected object error");
}
inline void no_private_work(const fs::path& directory) {
    for (const auto& entry : fs::directory_iterator(directory))
        require(!entry.path().filename().native().starts_with(".publish-"), "private staging leaked");
}
// Two distinct processes wait at a pipe barrier. Parent observes every visible
// destination as complete/finalized, including after both children exit.
inline void concurrent(const fs::path& destination, const NormalizedArtifact& model,
                       const std::function<void(int)>& publish) {
    int gate[2]; require(::pipe(gate) == 0, "barrier pipe");
    pid_t children[2];
    for (int i = 0; i < 2; ++i) {
        children[i] = ::fork(); require(children[i] >= 0, "fork publisher");
        if (children[i] == 0) {
            ::close(gate[1]); char token;
            if (::read(gate[0], &token, 1) != 1) ::_exit(2);
            ::close(gate[0]);
            try { publish(i); ::_exit(0); }
            catch (const std::exception& error) { std::cerr << error.what() << '\n'; ::_exit(3); }
        }
    }
    ::close(gate[0]); require(::write(gate[1], "go", 2) == 2, "release publishers"); ::close(gate[1]);
    bool finished[2]{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!finished[0] || !finished[1]) {
        if (fs::exists(destination)) exact_payload(destination, model, true);
        for (int i = 0; i < 2; ++i) if (!finished[i]) {
            int status = 0; const auto result = ::waitpid(children[i], &status, WNOHANG);
            require(result >= 0, "waitpid");
            if (result) { finished[i] = true; require(WIFEXITED(status) && WEXITSTATUS(status) == 0, "publisher child failed"); }
        }
        require(std::chrono::steady_clock::now() < deadline, "publisher timeout");
        ::usleep(1000);
    }
    exact_payload(destination, model, true);
}
