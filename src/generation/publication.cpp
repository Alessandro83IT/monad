// SPDX-License-Identifier: GPL-3.0-or-later
#include "publication.hpp"
#include <charconv>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <memory>
#include <system_error>
namespace athena::generation::publication {
std::optional<std::uint64_t> canonical_id(std::string_view name) {
    if (name.empty() || (name.size() > 1 && name.front() == '0')) return {};
    for (char c : name) if (c < '0' || c > '9') return {};
    std::uint64_t id;
    auto [end, error] = std::from_chars(name.data(), name.data() + name.size(), id);
    if (error == std::errc::result_out_of_range) throw std::overflow_error("Generation identifier out of range");
    if (error != std::errc{} || end != name.data() + name.size()) return {};
    if (id > max_id) throw std::overflow_error("Generation identifier exceeds TOML integer range");
    return id;
}
std::vector<std::uint64_t> existing_ids(const std::filesystem::path& path) {
    namespace d = core::detail;
    if (!path.is_absolute()) throw std::invalid_argument("generation authority must be absolute");
    auto directory = d::open_directory("/");
    for (const auto& component : path.relative_path()) {
        const auto name = component.string();
        d::leaf(name);
        struct stat status{};
        if (::fstatat(directory.get(), name.c_str(), &status, AT_SYMLINK_NOFOLLOW) < 0) {
            if (errno == ENOENT) return {};
            throw std::system_error(errno, std::generic_category(), "inspect generation namespace");
        }
        directory = d::open_at(directory.get(), name, O_RDONLY | O_DIRECTORY);
    }
    const int copy = ::fcntl(directory.get(), F_DUPFD_CLOEXEC, 0);
    if (copy < 0) throw std::system_error(errno, std::generic_category(), "duplicate generation namespace");
    DIR* stream = ::fdopendir(copy);
    if (!stream) {
        const int error = errno; ::close(copy);
        throw std::system_error(error, std::generic_category(), "open generation directory stream");
    }
    const auto close = [](DIR* value) { ::closedir(value); };
    std::unique_ptr<DIR, decltype(close)> owner(stream, close);
    std::vector<std::uint64_t> result;
    for (;;) {
        errno = 0;
        const auto* entry = ::readdir(stream);
        if (!entry) {
            if (errno) throw std::system_error(errno, std::generic_category(), "read generation namespace");
            break;
        }
        const std::string name(entry->d_name);
        if (name == "." || name == "..") continue;
        struct stat status{};
        if (::fstatat(directory.get(), name.c_str(), &status, AT_SYMLINK_NOFOLLOW) < 0)
            throw std::system_error(errno, std::generic_category(), "inspect generation entry");
        if (!S_ISDIR(status.st_mode)) continue; // Links and auxiliary files are not roots.
        const auto id = canonical_id(name);
        if (id) result.push_back(*id);
    }
    return result;
}
void persist(const std::filesystem::path& path, std::uint64_t id, std::string_view document,
             const core::detail::Syscalls& ops) {
    namespace d = core::detail;
    if (id > max_id) throw std::overflow_error("Generation identifier exceeds TOML integer range");
    auto parent = d::open_directory(path);
    d::require_durable_directory(parent.get(), ops);
    const auto name = d::private_directory(parent.get(), ops);
    // Only known private entries are cleaned; final numeric namespace is never removed.
    auto cleanup = [&]() noexcept { d::cleanup_private_tree(parent.get(), name, ops); };
    try {
        auto private_dir = d::open_at(parent.get(), name, O_RDONLY | O_DIRECTORY);
        auto file = d::open_at(private_dir.get(), "generation.toml", O_WRONLY | O_CREAT | O_EXCL);
        d::complete_write(file.get(), document, ops);
        d::file_barrier(file.get(), ops);
        d::directory_barrier(private_dir.get(), false, ops);
        if (d::publish_noreplace(parent.get(), name, std::to_string(id), ops) == d::Publication::AlreadyExists)
            throw std::runtime_error("Generation publication collision: " + std::to_string(id));
    } catch (d::PersistenceError& error) {
        error.cleanup_failure = d::cleanup_private_tree(parent.get(), name, ops);
        throw;
    } catch (...) { cleanup(); throw; }
    // Successful rename consumed the private name; there is nothing to remove.
}
}
