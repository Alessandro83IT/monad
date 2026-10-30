// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "archive.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

namespace athena::archive {
namespace {
namespace fs = std::filesystem;
constexpr std::size_t max_entries = 100000;
constexpr std::uint64_t max_expanded_bytes = 8ULL * 1024 * 1024 * 1024;
constexpr std::uint64_t max_entry_bytes = 2ULL * 1024 * 1024 * 1024;

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }
std::string archive_error(::archive* reader) {
    const char* message = archive_error_string(reader);
    return message ? message : "invalid archive";
}

fs::path make_private_directory(const fs::path& parent) {
    static std::atomic<unsigned long long> sequence{0};
    for (unsigned int attempt = 0; attempt != 128; ++attempt) {
        const fs::path candidate = parent / (".monad-extract-" +
            std::to_string(static_cast<unsigned long long>(::getpid())) + "-" +
            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        if (::mkdir(candidate.c_str(), 0700) == 0) return candidate;
        if (errno != EEXIST) throw fs::filesystem_error("create extraction directory", candidate,
                                                         std::error_code(errno, std::generic_category()));
    }
    fail("Cannot allocate private extraction directory");
}
}

fs::path extract(const fs::path& archive_path, const fs::path& destination) {
    fs::create_directories(destination);
    const fs::path root = make_private_directory(destination);
    try {
        std::unique_ptr<::archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
        if (!reader) fail("Cannot allocate archive reader");
        archive_read_support_filter_gzip(reader.get());
        archive_read_support_format_tar(reader.get());
        if (archive_read_open_filename(reader.get(), archive_path.c_str(), 16384) != ARCHIVE_OK)
            fail("Cannot open source archive: " + archive_error(reader.get()));

        std::set<std::string> top_levels;
        std::set<std::string> seen;
        std::size_t entries = 0;
        std::uint64_t expanded = 0;
        archive_entry* entry = nullptr;
        for (;;) {
            const int status = archive_read_next_header(reader.get(), &entry);
            if (status == ARCHIVE_EOF) break;
            if (status != ARCHIVE_OK) fail("Corrupt source archive: " + archive_error(reader.get()));
            if ((archive_format(reader.get()) & ARCHIVE_FORMAT_BASE_MASK) != ARCHIVE_FORMAT_TAR ||
                archive_filter_code(reader.get(), 0) != ARCHIVE_FILTER_GZIP)
                fail("Only gzip-compressed tar archives are supported");
            if (++entries > max_entries) fail("Source archive entry limit exceeded");

            const char* raw_name = archive_entry_pathname(entry);
            if (!raw_name || !*raw_name) fail("Source archive has an empty path");
            std::string name(raw_name);
            if (name.find('\\') != std::string::npos || name.find('\0') != std::string::npos)
                fail("Unsupported source archive path");
            fs::path relative(name);
            if (relative.is_absolute() || relative.has_root_name()) fail("Absolute source archive path rejected");
            for (const auto& component : relative)
                if (component == "..") fail("Source archive traversal rejected");
            relative = relative.lexically_normal();
            while (!relative.empty() && relative.filename().empty()) relative = relative.parent_path();
            if (relative.empty() || relative == ".") fail("Invalid source archive path");
            const auto normalized = relative.generic_string();
            if (!seen.insert(normalized).second) fail("Duplicate source archive entry rejected");
            top_levels.insert(relative.begin()->string());
            if (top_levels.size() != 1) fail("Expected exactly one source directory");

            const auto type = archive_entry_filetype(entry);
            if (archive_entry_symlink(entry) || archive_entry_hardlink(entry) ||
                (type != AE_IFREG && type != AE_IFDIR) || (archive_entry_perm(entry) & 07000))
                fail("Unsupported source archive entry type or permissions");
            const auto declared = archive_entry_size(entry);
            if (declared < 0 || static_cast<std::uint64_t>(declared) > max_entry_bytes ||
                static_cast<std::uint64_t>(declared) > max_expanded_bytes - expanded)
                fail("Source archive expanded-size limit exceeded");

            const fs::path output = root / relative;
            if (type == AE_IFDIR) {
                if (declared != 0) fail("Directory archive entry carries unexpected data");
                if (fs::exists(output) && !fs::is_directory(fs::symlink_status(output)))
                    fail("Source archive file/directory collision");
                fs::create_directories(output);
                if (archive_read_data_skip(reader.get()) != ARCHIVE_OK)
                    fail("Cannot skip source archive directory data: " + archive_error(reader.get()));
                continue;
            }
            if (fs::exists(output)) fail("Source archive file/directory collision");
            fs::create_directories(output.parent_path());
            std::ofstream stream(output, std::ios::binary | std::ios::out);
            if (!stream) fail("Cannot create extracted source file");
            std::uint64_t actual = 0;
            char buffer[16384];
            for (;;) {
                const auto count = archive_read_data(reader.get(), buffer, sizeof(buffer));
                if (count < 0) fail("Truncated source archive entry: " + archive_error(reader.get()));
                if (count == 0) break;
                const auto amount = static_cast<std::uint64_t>(count);
                if (amount > max_entry_bytes - actual || amount > max_expanded_bytes - expanded)
                    fail("Source archive expanded-size limit exceeded");
                stream.write(buffer, count);
                if (!stream) fail("Cannot write extracted source file");
                actual += amount;
                expanded += amount;
            }
            if (actual != static_cast<std::uint64_t>(declared)) fail("Truncated source archive entry");
            stream.close();
            if (!stream) fail("Cannot finish extracted source file");
            fs::permissions(output, (archive_entry_perm(entry) & 0111) ?
                fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec :
                fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read);
        }
        if (archive_read_close(reader.get()) != ARCHIVE_OK) fail("Cannot close source archive: " + archive_error(reader.get()));
        if (top_levels.size() != 1 || !fs::is_directory(root / *top_levels.begin()))
            fail("Expected exactly one source directory");
        return root / *top_levels.begin();
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(root, ignored);
        throw;
    }
}
}
