// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "arch_repository.hpp"

#include "../dependency/parser.hpp"
#include "../process/run.hpp"
#include "../verify/verifier.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <memory>
#include <map>
#include <set>
#include <deque>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace athena::repository {

namespace {
namespace fs = std::filesystem;
constexpr std::size_t max_archive_entries = 100000;
constexpr std::uint64_t max_expanded_bytes = 8ULL * 1024 * 1024 * 1024;
constexpr std::uint64_t max_entry_bytes = 2ULL * 1024 * 1024 * 1024;
constexpr std::uint64_t max_desc_bytes = 16ULL * 1024 * 1024;

[[noreturn]] void archive_fail(const std::string& reason) {
    throw std::runtime_error("Unsafe Arch archive: " + reason);
}

std::string archive_error(::archive* reader) {
    const char* error = archive_error_string(reader);
    return error ? error : "invalid archive";
}

struct ArchiveReader {
    std::unique_ptr<::archive, decltype(&archive_read_free)> reader{nullptr, archive_read_free};
    int fd = -1;
    ~ArchiveReader() { reader.reset(); if (fd >= 0) ::close(fd); }
    ArchiveReader() = default;
    ArchiveReader(const ArchiveReader&) = delete;
    ArchiveReader& operator=(const ArchiveReader&) = delete;
    ArchiveReader(ArchiveReader&& other) noexcept
        : reader(std::move(other.reader)), fd(std::exchange(other.fd, -1)) {}
    ::archive* get() const noexcept { return reader.get(); }
};
ArchiveReader open_tar(const fs::path& path) {
    ArchiveReader result;
    result.fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (result.fd < 0) archive_fail("cannot open archive without following links: " + path.string());
    struct stat st{};
    if (::fstat(result.fd, &st) != 0 || !S_ISREG(st.st_mode)) archive_fail("archive source is not a regular file");
    result.reader.reset(archive_read_new());
    if (!result.reader) archive_fail("cannot allocate archive reader");
    archive_read_support_filter_all(result.get());
    archive_read_support_format_tar(result.get());
    if (archive_read_open_fd(result.get(), result.fd, 64 * 1024) != ARCHIVE_OK)
        archive_fail("cannot open archive: " + archive_error(result.get()));
    return result;
}

std::string safe_member_name(archive_entry* entry, bool& root_directory) {
    root_directory = false;
    const char* raw = archive_entry_pathname(entry);
    if (!raw || !*raw) archive_fail("empty member path");
    std::string name(raw);
    if (name.find('\\') != std::string::npos || name.find('\0') != std::string::npos)
        archive_fail("unsupported member path");
    fs::path path(name);
    if (path.is_absolute() || path.has_root_name()) archive_fail("absolute member path");
    for (const auto& component : path)
        if (component == "..") archive_fail("parent traversal member path");
    path = path.lexically_normal();
    while (!path.empty() && path.filename().empty()) path = path.parent_path();
    if (path.empty() || path == ".") {
        if (archive_entry_filetype(entry) != AE_IFDIR) archive_fail("non-directory archive root entry");
        root_directory = true;
        return {};
    }
    return path.generic_string();
}

struct DownloadStage {
    fs::path directory;
    fs::path file;
    explicit DownloadStage(const fs::path& parent) {
        std::string pattern = (parent / ".monad-arch-download-XXXXXX").string();
        if (!::mkdtemp(pattern.data()))
            throw fs::filesystem_error("create private Arch download directory", parent,
                std::error_code(errno, std::generic_category()));
        directory = pattern;
        file = directory / "payload";
    }
    ~DownloadStage() { std::error_code ignored; fs::remove_all(directory, ignored); }
};

void download_file(const std::string& url, const fs::path& destination) {
    if (url.empty() || url.find('\0') != std::string::npos)
        throw std::runtime_error("Arch download URL is empty or contains NUL");
    const auto parent = destination.parent_path().empty() ? fs::current_path() : destination.parent_path();
    fs::create_directories(parent);
    DownloadStage stage(parent);
    const int result = athena::process::run({"curl", "--fail", "--silent", "--show-error",
        "--location", "--retry", "3", "--output", stage.file.string(), "--", url});
    if (result != 0) throw std::runtime_error("Arch download failed (exit " + std::to_string(result) + "): " + url);
    std::error_code error;
    fs::rename(stage.file, destination, error);
    if (error) throw fs::filesystem_error("atomically install Arch download", stage.file, destination, error);
}

bool is_desc_member(const std::string& member) {
    return member == "desc" || (member.size() > 5 && member.ends_with("/desc"));
}

std::vector<std::pair<std::string, std::string>> read_desc_members(const fs::path& path) {
    auto reader = open_tar(path);
    std::set<std::string> seen;
    std::map<std::string, mode_t> kinds;
    std::vector<std::pair<std::string, std::string>> result;
    std::uint64_t expanded = 0;
    std::size_t count = 0;
    bool root_seen = false;
    archive_entry* entry = nullptr;
    for (;;) {
        const int status = archive_read_next_header(reader.get(), &entry);
        if (status == ARCHIVE_EOF) break;
        if (status != ARCHIVE_OK) archive_fail("corrupt repository database: " + archive_error(reader.get()));
        if ((archive_format(reader.get()) & ARCHIVE_FORMAT_BASE_MASK) != ARCHIVE_FORMAT_TAR)
            archive_fail("repository database is not tar");
        if (++count > max_archive_entries) archive_fail("entry count limit exceeded");
        bool root = false;
        const auto name = safe_member_name(entry, root);
        const auto type = archive_entry_filetype(entry);
        if (archive_entry_symlink(entry) || archive_entry_hardlink(entry) ||
            (type != AE_IFREG && type != AE_IFDIR) || (archive_entry_perm(entry) & 07000))
            archive_fail("link, special file, or privileged mode in repository database");
        if (root) {
            if (root_seen) archive_fail("duplicate root directory entry");
            root_seen = true;
            if (archive_entry_size(entry) != 0 || archive_read_data_skip(reader.get()) != ARCHIVE_OK)
                archive_fail("invalid root directory entry");
            continue;
        }
        if (!seen.insert(name).second) archive_fail("duplicate member: " + name);
        const auto size = archive_entry_size(entry);
        if (size < 0 || static_cast<std::uint64_t>(size) > max_entry_bytes ||
            static_cast<std::uint64_t>(size) > max_expanded_bytes - expanded)
            archive_fail("expanded size limit exceeded");
        expanded += static_cast<std::uint64_t>(size);
        for (auto parent = fs::path(name).parent_path(); !parent.empty(); parent = parent.parent_path()) {
            auto found = kinds.find(parent.generic_string());
            if (found != kinds.end() && found->second != AE_IFDIR) archive_fail("file/directory path collision");
        }
        const auto found = kinds.find(name);
        if (found != kinds.end()) archive_fail("file/directory path collision");
        const auto child = kinds.lower_bound(name + "/");
        if (type != AE_IFDIR && child != kinds.end() && child->first.starts_with(name + "/"))
            archive_fail("file/directory path collision");
        kinds[name] = type;
        if (type == AE_IFDIR) {
            if (size != 0 || archive_read_data_skip(reader.get()) != ARCHIVE_OK)
                archive_fail("invalid directory entry");
            continue;
        }
        if (is_desc_member(name)) {
            if (static_cast<std::uint64_t>(size) > max_desc_bytes) archive_fail("desc metadata size limit exceeded");
            std::string contents;
            contents.reserve(static_cast<std::size_t>(size));
            std::array<char, 16384> buffer{};
            for (;;) {
                const auto amount = archive_read_data(reader.get(), buffer.data(), buffer.size());
                if (amount < 0) archive_fail("corrupt desc member: " + archive_error(reader.get()));
                if (amount == 0) break;
                contents.append(buffer.data(), static_cast<std::size_t>(amount));
                if (contents.size() > max_desc_bytes) archive_fail("desc metadata size limit exceeded");
            }
            if (contents.size() != static_cast<std::size_t>(size)) archive_fail("truncated desc member");
            result.emplace_back(name, std::move(contents));
        } else if (archive_read_data_skip(reader.get()) != ARCHIVE_OK) {
            archive_fail("corrupt repository member: " + archive_error(reader.get()));
        }
    }
    if (archive_read_close(reader.get()) != ARCHIVE_OK) archive_fail("repository archive close failed: " + archive_error(reader.get()));
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return result;
}

struct FileDescriptor {
    int value = -1;
    explicit FileDescriptor(int fd = -1) : value(fd) {}
    ~FileDescriptor() { if (value >= 0) ::close(value); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept : value(std::exchange(other.value, -1)) {}
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            if (value >= 0) ::close(value);
            value = std::exchange(other.value, -1);
        }
        return *this;
    }
    int release() noexcept { return std::exchange(value, -1); }
};

void write_all(int fd, const char* data, std::size_t size) {
    while (size) {
        const auto written = ::write(fd, data, size);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) archive_fail("cannot write extracted package member");
        data += written;
        size -= static_cast<std::size_t>(written);
    }
}

std::string safe_link_target(const std::string& target) {
    if (target.empty() || target.front() == '/' || target.find('\\') != std::string::npos ||
        target.find('\0') != std::string::npos)
        archive_fail("absolute or malformed symbolic link target");
    return target;
}

std::string safe_link_name(const char* raw) {
    if (!raw || !*raw) archive_fail("empty hardlink target");
    const std::string value(raw);
    if (value.find('\\') != std::string::npos || value.find('\0') != std::string::npos)
        archive_fail("malformed hardlink target");
    fs::path path(value);
    if (path.is_absolute() || path.has_root_name()) archive_fail("absolute hardlink target");
    for (const auto& component : path)
        if (component == "..") archive_fail("hardlink target traversal");
    path = path.lexically_normal();
    if (path.empty() || path == ".") archive_fail("invalid hardlink target");
    return path.generic_string();
}

bool contained_link_target(const std::string& link_name, const std::string& target,
                           const std::map<std::string, std::string>& links) {
    std::vector<std::string> resolved;
    const auto parent = fs::path(link_name).parent_path();
    for (const auto& part : parent) if (part != ".") resolved.push_back(part.string());
    std::deque<std::string> pending;
    auto append = [&](const std::string& value) {
        std::stringstream components(value);
        std::string component;
        while (std::getline(components, component, '/')) if (!component.empty()) pending.push_back(component);
    };
    auto prepend = [&](const std::string& value) {
        std::vector<std::string> parts;
        std::stringstream components(value);
        std::string component;
        while (std::getline(components, component, '/')) if (!component.empty()) parts.push_back(component);
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) pending.push_front(*it);
    };
    append(target);
    std::size_t expansions = 0;
    while (!pending.empty()) {
        auto component = std::move(pending.front());
        pending.pop_front();
        if (component == ".") continue;
        if (component == "..") {
            if (resolved.empty()) return false;
            resolved.pop_back();
            continue;
        }
        resolved.push_back(component);
        std::string path;
        for (const auto& part : resolved) path += (path.empty() ? "" : "/") + part;
        const auto found = links.find(path);
        if (found != links.end()) {
            if (++expansions > 40) return false;
            resolved.pop_back();
            prepend(found->second);
        }
    }
    return true;
}

void extract_tar(const fs::path& source, const fs::path& destination) {
    const auto parent = destination.parent_path().empty() ? fs::current_path() : destination.parent_path();
    fs::create_directories(parent);
    std::error_code status_error;
    const auto status = fs::symlink_status(destination, status_error);
    if (!status_error && status.type() != fs::file_type::not_found)
        throw std::runtime_error("Arch extraction destination already exists: " + destination.string());
    if (status_error != std::errc::no_such_file_or_directory && status_error)
        throw fs::filesystem_error("inspect Arch extraction destination", destination, status_error);
    if (::mkdir(destination.c_str(), 0700) != 0)
        throw fs::filesystem_error("create private Arch extraction destination", destination,
            std::error_code(errno, std::generic_category()));
    try {
        FileDescriptor root(::open(destination.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (root.value < 0) archive_fail("cannot open private destination");
        auto reader = open_tar(source);
        std::set<std::string> seen;
        std::map<std::string, mode_t> kinds;
        std::map<std::string, std::string> symbolic_links;
        std::vector<std::pair<std::string, std::string>> deferred_symlinks;
        std::uint64_t expanded = 0;
        std::size_t count = 0;
        bool root_seen = false;
        archive_entry* entry = nullptr;
        for (;;) {
            const int result = archive_read_next_header(reader.get(), &entry);
            if (result == ARCHIVE_EOF) break;
            if (result != ARCHIVE_OK) archive_fail("corrupt package archive: " + archive_error(reader.get()));
            if ((archive_format(reader.get()) & ARCHIVE_FORMAT_BASE_MASK) != ARCHIVE_FORMAT_TAR)
                archive_fail("package archive is not tar");
            if (++count > max_archive_entries) archive_fail("entry count limit exceeded");
            bool root_entry = false;
            const auto name = safe_member_name(entry, root_entry);
            const auto type = archive_entry_filetype(entry);
            const bool is_symlink = archive_entry_symlink(entry) != nullptr;
            const bool is_hardlink = archive_entry_hardlink(entry) != nullptr;
            if ((type != AE_IFREG && type != AE_IFDIR && type != AE_IFLNK && !is_hardlink) ||
                (archive_entry_perm(entry) & 07000) || (is_symlink && is_hardlink))
                archive_fail("link, special file, or privileged mode in package archive (" + name +
                    ", type=" + std::to_string(type) + ", symlink=" + std::to_string(is_symlink) +
                    ", hardlink=" + std::to_string(is_hardlink) + ")");
            if (root_entry) {
                if (root_seen) archive_fail("duplicate package root directory entry");
                root_seen = true;
                if (archive_entry_size(entry) != 0 || archive_read_data_skip(reader.get()) != ARCHIVE_OK)
                    archive_fail("invalid package root entry");
                continue;
            }
            if (!seen.insert(name).second) archive_fail("duplicate member: " + name);
            const auto declared = archive_entry_size(entry);
            if (is_symlink) {
                const std::string target = safe_link_target(archive_entry_symlink(entry));
                for (auto ancestor = fs::path(name).parent_path(); !ancestor.empty(); ancestor = ancestor.parent_path()) {
                    const auto existing = kinds.find(ancestor.generic_string());
                    if (existing != kinds.end() && existing->second != AE_IFDIR)
                        archive_fail("file/directory path collision");
                }
                if (kinds.contains(name)) archive_fail("file/directory path collision");
                const auto child = kinds.lower_bound(name + "/");
                if (child != kinds.end() && child->first.starts_with(name + "/"))
                    archive_fail("file/directory path collision");
                symbolic_links.emplace(name, target);
                deferred_symlinks.emplace_back(name, target);
                kinds[name] = AE_IFLNK;
                if (archive_read_data_skip(reader.get()) != ARCHIVE_OK) archive_fail("invalid symbolic link entry");
                continue;
            }
            if (is_hardlink) {
                const std::string target = safe_link_name(archive_entry_hardlink(entry));
                if (!kinds.contains(target) || kinds.at(target) != AE_IFREG)
                    archive_fail("hardlink target must be an earlier regular package member");
            }
            if (declared < 0 || static_cast<std::uint64_t>(declared) > max_entry_bytes ||
                static_cast<std::uint64_t>(declared) > max_expanded_bytes - expanded)
                archive_fail("expanded size limit exceeded");
            for (auto ancestor = fs::path(name).parent_path(); !ancestor.empty(); ancestor = ancestor.parent_path()) {
                const auto existing = kinds.find(ancestor.generic_string());
                if (existing != kinds.end() && existing->second != AE_IFDIR)
                    archive_fail("file/directory path collision");
            }
            if (kinds.contains(name)) archive_fail("file/directory path collision");
            const auto child = kinds.lower_bound(name + "/");
            if (type != AE_IFDIR && child != kinds.end() && child->first.starts_with(name + "/"))
                archive_fail("file/directory path collision");
            kinds[name] = type;

            fs::path relative(name);
            FileDescriptor directory(::dup(root.value));
            if (directory.value < 0) archive_fail("cannot duplicate destination descriptor");
            auto component = relative.begin();
            for (; std::next(component) != relative.end(); ++component) {
                const std::string part = component->string();
                if (::mkdirat(directory.value, part.c_str(), 0755) != 0 && errno != EEXIST)
                    archive_fail("cannot create package parent directory");
                FileDescriptor child(::openat(directory.value, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
                if (child.value < 0) archive_fail("unsafe package parent directory");
                directory = std::move(child);
            }
            const std::string leaf = component->string();
            if (type == AE_IFDIR) {
                if (declared != 0) archive_fail("directory entry carries data");
                if (::mkdirat(directory.value, leaf.c_str(), 0755) != 0 && errno != EEXIST)
                    archive_fail("cannot create package directory");
                FileDescriptor child(::openat(directory.value, leaf.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
                if (child.value < 0 || ::fchmod(child.value, 0755) != 0 || archive_read_data_skip(reader.get()) != ARCHIVE_OK)
                    archive_fail("invalid package directory entry");
                continue;
            }
            if (is_hardlink) {
                const auto safe_target = safe_link_name(archive_entry_hardlink(entry));
                fs::path target_path(safe_target);
                FileDescriptor target_directory(::dup(root.value));
                auto target_component = target_path.begin();
                for (; std::next(target_component) != target_path.end(); ++target_component) {
                    FileDescriptor child(::openat(target_directory.value, target_component->c_str(),
                        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
                    if (child.value < 0) archive_fail("unsafe hardlink parent");
                    target_directory = std::move(child);
                }
                if (::linkat(target_directory.value, target_path.filename().c_str(), directory.value, leaf.c_str(), 0) != 0)
                    archive_fail("cannot create package hardlink");
                kinds[name] = AE_IFREG;
                if (archive_read_data_skip(reader.get()) != ARCHIVE_OK) archive_fail("invalid hardlink entry");
                continue;
            }
            FileDescriptor output(::openat(directory.value, leaf.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
            if (output.value < 0) archive_fail("cannot create unique package file");
            struct stat output_stat{};
            if (::fstat(output.value, &output_stat) != 0 || !S_ISREG(output_stat.st_mode))
                archive_fail("created package member is not regular");
            std::uint64_t actual = 0;
            std::array<char, 64 * 1024> buffer{};
            for (;;) {
                const auto amount = archive_read_data(reader.get(), buffer.data(), buffer.size());
                if (amount < 0) archive_fail("corrupt package member: " + archive_error(reader.get()));
                if (amount == 0) break;
                const auto bytes = static_cast<std::uint64_t>(amount);
                if (bytes > max_entry_bytes - actual || bytes > max_expanded_bytes - expanded)
                    archive_fail("expanded size limit exceeded");
                write_all(output.value, buffer.data(), static_cast<std::size_t>(amount));
                actual += bytes;
                expanded += bytes;
            }
            if (actual != static_cast<std::uint64_t>(declared)) archive_fail("truncated package member");
            const mode_t safe_mode = 0644 | (archive_entry_perm(entry) & 0111);
            if (::fchmod(output.value, safe_mode) != 0) archive_fail("cannot set safe package file mode");
        }
        for (const auto& [name, target] : deferred_symlinks)
            if (!contained_link_target(name, target, symbolic_links))
                archive_fail("symbolic link target escapes package root or cycles");
        for (const auto& [name, target] : deferred_symlinks) {
            const fs::path relative(name);
            FileDescriptor directory(::dup(root.value));
            auto component = relative.begin();
            for (; std::next(component) != relative.end(); ++component) {
                if (::mkdirat(directory.value, component->c_str(), 0755) != 0 && errno != EEXIST)
                    archive_fail("cannot create symbolic link parent directory");
                FileDescriptor child(::openat(directory.value, component->c_str(),
                    O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
                if (child.value < 0) archive_fail("unsafe symbolic link parent");
                directory = std::move(child);
            }
            if (::symlinkat(target.c_str(), directory.value, relative.filename().c_str()) != 0)
                archive_fail("cannot create package symbolic link");
        }
        if (archive_read_close(reader.get()) != ARCHIVE_OK)
            archive_fail("package archive close failed: " + archive_error(reader.get()));
        if (::fchmod(root.value, 0755) != 0) archive_fail("cannot finalize extraction directory mode");
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(destination, ignored);
        throw;
    }
}

using Fields = std::map<
    std::string,
    std::vector<std::string>
>;

Fields parse_desc(const std::string& text)
{
    Fields fields;
    std::istringstream input(text);

    std::string line;
    std::string current_field;

    while (std::getline(input, line)) {

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            current_field.clear();
            continue;
        }

        if (line.front() == '%' &&
            line.back() == '%' &&
            line.size() >= 3) {

            current_field = line.substr(
                1,
                line.size() - 2
            );

            continue;
        }

        if (!current_field.empty()) {
            fields[current_field].push_back(line);
        }
    }

    return fields;
}

std::string first_field(
    const Fields& fields,
    const std::string& name
)
{
    const auto iterator = fields.find(name);

    if (iterator == fields.end() ||
        iterator->second.empty()) {

        return "";
    }

    return iterator->second.front();
}

std::string package_url(
    const std::string& base_url,
    const std::string& filename
)
{
    if (base_url.empty()) {
        return filename;
    }

    if (base_url.back() == '/') {
        return base_url + filename;
    }

    return base_url + "/" + filename;
}

/*
 * Arch optional dependencies have the form:
 *
 *     package: human readable reason
 *
 * Athena's dependency expression contains the package selector but
 * currently has no field for the explanatory text, so only the
 * dependency selector is imported.
 */
std::string optional_dependency_name(
    const std::string& value
)
{
    const auto separator = value.find(':');

    if (separator == std::string::npos) {
        return value;
    }

    return value.substr(0, separator);
}

/*
 * Arch provides entries may carry a version:
 *
 *     libfoo=1.2
 *
 * The current Athena PackageProvides model stores semantic names but
 * does not yet have versioned capability constraints. Preserve the
 * capability name and leave versioned provides for a later semantic
 * extension.
 */
std::string provided_capability_name(
    const std::string& value
)
{
    const auto separator =
        value.find_first_of("=<>");

    if (separator == std::string::npos) {
        return value;
    }

    return value.substr(0, separator);
}

void append_dependencies(
    athena::package::Package& package,
    const Fields& fields,
    const std::string& field_name,
    athena::dependency::DependencyKind kind,
    athena::dependency::DependencyContext context,
    bool optional_reason
)
{
    const auto iterator = fields.find(field_name);

    if (iterator == fields.end()) {
        return;
    }

    for (const auto& raw_value : iterator->second) {

        const std::string value =
            optional_reason
                ? optional_dependency_name(raw_value)
                : raw_value;

        if (value.empty()) {
            continue;
        }

        auto dependency =
            athena::dependency::parse_dependency(value);

        dependency.kind = kind;
        dependency.context = context;

        package.dependencies.push_back(
            std::move(dependency)
        );
    }
}

athena::package::Package package_from_desc(
    const Fields& fields,
    const std::string& base_url
)
{
    athena::package::Package package;

    package.name = first_field(fields, "NAME");
    package.version = first_field(fields, "VERSION");
    package.description = first_field(fields, "DESC");

    const std::string filename =
        first_field(fields, "FILENAME");

    package.source =
        package_url(base_url, filename);

    package.sha256 =
        first_field(fields, "SHA256SUM");

    // Preserve the complete repository record for the public imported-binary
    // adapter, including fields whose semantics Athena does not yet model.
    package.arch_metadata = fields;

    /*
     * Arch binary packages are already built. The build backend is
     * therefore intentionally empty at this stage.
     */
    package.build_system = "";

    append_dependencies(
        package,
        fields,
        "DEPENDS",
        athena::dependency::DependencyKind::Runtime,
        athena::dependency::DependencyContext::Target,
        false
    );

    append_dependencies(
        package,
        fields,
        "MAKEDEPENDS",
        athena::dependency::DependencyKind::Build,
        athena::dependency::DependencyContext::Build,
        false
    );

    append_dependencies(
        package,
        fields,
        "CHECKDEPENDS",
        athena::dependency::DependencyKind::Test,
        athena::dependency::DependencyContext::Build,
        false
    );

    append_dependencies(
        package,
        fields,
        "OPTDEPENDS",
        athena::dependency::DependencyKind::Optional,
        athena::dependency::DependencyContext::Target,
        true
    );

    const auto provides =
        fields.find("PROVIDES");

    if (provides != fields.end()) {

        for (const auto& value : provides->second) {

            const std::string capability =
                provided_capability_name(value);

            if (!capability.empty()) {
                package.provides.capabilities.push_back(
                    capability
                );
            }
        }
    }

    return package;
}

} // namespace

void ArchRepository::download(const std::string& url)
{
    if (url.empty()) {
        throw std::runtime_error(
            "Arch repository database URL vuoto"
        );
    }

    if (url.find('\0') != std::string::npos) throw std::runtime_error("Arch repository URL contains NUL");
    download_file(url, database_);
}

void ArchRepository::download_package(
    const athena::package::Package& package,
    const std::filesystem::path& destination
) const
{
    if (package.source.empty()) {
        throw std::runtime_error(
            "Il pacchetto Arch non contiene un URL sorgente"
        );
    }

    if (destination.empty()) {
        throw std::runtime_error(
            "Destinazione del pacchetto Arch vuota"
        );
    }

    download_file(package.source, destination);
}

void ArchRepository::verify_package_sha256(
    const athena::package::Package& package,
    const std::filesystem::path& package_path
) const
{
    if (package.sha256.empty()) {
        throw std::runtime_error(
            "Il pacchetto Arch non contiene SHA-256 nel repository"
        );
    }

    std::error_code path_error;
    const auto package_status = std::filesystem::symlink_status(package_path, path_error);
    if (path_error || !std::filesystem::is_regular_file(package_status)) {
        throw std::runtime_error(
            "Pacchetto Arch non trovato: " +
            package_path.string()
        );
    }

    if (!athena::verify::verify_sha256(package_path, package.sha256)) {
        throw std::runtime_error(
            "Verifica SHA-256 del pacchetto Arch fallita: " +
            package_path.string()
        );
    }
}

void ArchRepository::verify_package_signature(
    const athena::package::Package& package,
    const std::filesystem::path& package_path,
    const std::filesystem::path& keyring
) const
{
    if (package.source.empty()) {
        throw std::runtime_error(
            "Il pacchetto Arch non contiene un URL sorgente"
        );
    }

    std::error_code package_error;
    const auto package_status = std::filesystem::symlink_status(package_path, package_error);
    if (package_error || !std::filesystem::is_regular_file(package_status)) {
        throw std::runtime_error(
            "Pacchetto Arch non trovato: " +
            package_path.string()
        );
    }

    std::error_code keyring_error;
    const auto keyring_status = std::filesystem::symlink_status(keyring, keyring_error);
    if (keyring_error || !std::filesystem::is_regular_file(keyring_status)) {
        throw std::runtime_error(
            "Keyring OpenPGP non trovato: " +
            keyring.string()
        );
    }

    const auto parent = package_path.parent_path().empty() ? fs::current_path() : package_path.parent_path();
    DownloadStage stage(parent);
    const auto signature_url = package.source + ".sig";
    const int download_status = athena::process::run({"curl", "--fail", "--silent", "--show-error",
        "--location", "--retry", "3", "--output", stage.file.string(), "--", signature_url});
    if (download_status != 0) {
        throw std::runtime_error("Download della firma del pacchetto Arch fallito: " + signature_url);
    }
    const int status = athena::process::run({"gpgv", "--keyring", keyring.string(), "--",
        stage.file.string(), package_path.string()});
    if (status != 0) {
        throw std::runtime_error(
            "Verifica della firma OpenPGP del pacchetto Arch fallita"
        );
    }
}

ArchRepository::ArchRepository(
    std::filesystem::path database,
    std::string base_url
)
    : database_(std::move(database)),
      base_url_(std::move(base_url))
{
}

void ArchRepository::load()
{
    std::error_code database_error;
    const auto database_status = std::filesystem::symlink_status(database_, database_error);
    if (database_error || !std::filesystem::is_regular_file(database_status)) {
        throw std::runtime_error(
            "Database Arch inesistente: " +
            database_.string()
        );
    }

    index_ = RepositoryIndex{};

    for (const auto& [member, contents] : read_desc_members(database_)) {
        const auto fields = parse_desc(contents);

        const auto package =
            package_from_desc(
                fields,
                base_url_
            );

        if (package.name.empty()) {
            throw ArchPackageMetadataError(
                "Voce Arch priva di %NAME% nel database '" +
                database_.string() +
                "'"
            );
        }

        if (package.version.empty()) {
            throw ArchPackageMetadataError(
                "Pacchetto Arch '" +
                package.name +
                "' privo di %VERSION%"
            );
        }

        index_.add(package);
    }
}

const RepositoryIndex& ArchRepository::index() const
{
    return index_;
}

std::vector<athena::package::Package>
ArchRepository::find(
    const std::string& name
) const
{
    return index_.find(name);
}

bool ArchRepository::contains(
    const std::string& name
) const
{
    return index_.contains(name);
}


void ArchRepository::extract_package(
    const std::filesystem::path& package_path,
    const std::filesystem::path& destination
) const
{
    if (destination.empty()) throw std::runtime_error("Arch extraction destination is empty");
    std::error_code package_error;
    const auto package_status = std::filesystem::symlink_status(package_path, package_error);
    if (package_error || !std::filesystem::is_regular_file(package_status)) {
        throw std::runtime_error(
            "Arch package does not exist: " +
            package_path.string()
        );
    }

    extract_tar(package_path, destination);
}

athena::package::Package ArchRepository::load_package_metadata(
    const std::filesystem::path& extracted_package
) const
{
    const auto pkginfo =
        extracted_package / ".PKGINFO";

    if (!std::filesystem::exists(pkginfo)) {
        throw std::runtime_error(
            "Arch package does not contain .PKGINFO: " +
            extracted_package.string()
        );
    }

    std::ifstream input(pkginfo);

    if (!input) {
        throw std::runtime_error(
            "failed to open Arch .PKGINFO: " +
            pkginfo.string()
        );
    }

    athena::package::Package package;

    std::string line;

    /*
     * Arch .PKGINFO records are key/value pairs in the form:
     *
     *     pkgname = glibc
     *     pkgver = 2.44...
     *     depend = filesystem
     *
     * Keys and values are separated by '=' with optional whitespace.
     * Some keys occur multiple times, such as depend, optdepend and
     * makedepend, so every occurrence must be processed independently.
     */
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }

        const auto separator = line.find('=');

        if (separator == std::string::npos) {
            continue;
        }

        std::string key =
            line.substr(0, separator);

        std::string value =
            line.substr(separator + 1);

        const auto trim = [](std::string& value) {
            const auto first =
                value.find_first_not_of(" \t\r");

            if (first == std::string::npos) {
                value.clear();
                return;
            }

            const auto last =
                value.find_last_not_of(" \t\r");

            value =
                value.substr(
                    first,
                    last - first + 1
                );
        };

        trim(key);
        trim(value);

        if (key == "pkgname") {
            package.name = value;
        }
        else if (key == "pkgver") {
            package.version = value;
        }
        else if (key == "pkgdesc") {
            package.description = value;
        }
        else if (key == "depend") {
            auto dependency =
                athena::dependency::parse_dependency(value);

            dependency.kind =
                athena::dependency::DependencyKind::Runtime;

            dependency.context =
                athena::dependency::DependencyContext::Target;

            package.dependencies.push_back(
                std::move(dependency)
            );
        }
        else if (key == "optdepend") {
            /*
             * Arch stores optional dependencies as:
             *
             *     package: reason
             *
             * Athena keeps the dependency expression separate from the
             * human-readable reason, so only the package expression is
             * imported here.
             */
            const auto dependency_name =
                optional_dependency_name(value);

            if (!dependency_name.empty()) {
                auto dependency =
                    athena::dependency::parse_dependency(
                        dependency_name
                    );

                dependency.kind =
                    athena::dependency::DependencyKind::Optional;

                dependency.context =
                    athena::dependency::DependencyContext::Target;

                package.dependencies.push_back(
                    std::move(dependency)
                );
            }
        }
        else if (key == "makedepend") {
            auto dependency =
                athena::dependency::parse_dependency(value);

            dependency.kind =
                athena::dependency::DependencyKind::Build;

            dependency.context =
                athena::dependency::DependencyContext::Build;

            package.dependencies.push_back(
                std::move(dependency)
            );
        }
        else if (key == "checkdepend") {
            auto dependency =
                athena::dependency::parse_dependency(value);

            dependency.kind =
                athena::dependency::DependencyKind::Test;

            dependency.context =
                athena::dependency::DependencyContext::Build;

            package.dependencies.push_back(
                std::move(dependency)
            );
        }
        else if (key == "provides") {
            const auto capability =
                provided_capability_name(value);

            if (!capability.empty()) {
                package.provides.capabilities.push_back(
                    capability
                );
            }
        }
    }

    if (package.name.empty()) {
        throw std::runtime_error(
            "Arch .PKGINFO does not contain pkgname"
        );
    }

    if (package.version.empty()) {
        throw std::runtime_error(
            "Arch .PKGINFO does not contain pkgver"
        );
    }

    return package;
}


} // namespace athena::repository
