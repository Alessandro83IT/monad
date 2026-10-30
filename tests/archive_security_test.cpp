// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later
#include "temporary_directory.hpp"
#include "../src/archive/archive.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace fs = std::filesystem;

static void make_archive(const fs::path& path, const std::string& name,
                         mode_t type, const std::string& data = {},
                         const char* link = nullptr) {
    std::unique_ptr<::archive, decltype(&archive_write_free)> writer(archive_write_new(), archive_write_free);
    assert(writer);
    assert(archive_write_add_filter_gzip(writer.get()) == ARCHIVE_OK);
    assert(archive_write_set_format_pax_restricted(writer.get()) == ARCHIVE_OK);
    assert(archive_write_open_filename(writer.get(), path.c_str()) == ARCHIVE_OK);
    archive_entry* entry = archive_entry_new();
    archive_entry_set_pathname(entry, name.c_str());
    archive_entry_set_filetype(entry, type);
    archive_entry_set_perm(entry, type == AE_IFDIR ? 0755 : 0644);
    archive_entry_set_size(entry, type == AE_IFREG ? static_cast<la_int64_t>(data.size()) : 0);
    if (link) archive_entry_set_symlink(entry, link);
    assert(archive_write_header(writer.get(), entry) == ARCHIVE_OK);
    if (!data.empty()) assert(archive_write_data(writer.get(), data.data(), data.size()) == static_cast<la_ssize_t>(data.size()));
    archive_entry_free(entry);
    assert(archive_write_close(writer.get()) == ARCHIVE_OK);
}

int main() {
    athena::test::TemporaryDirectory fixture("athena-archive-security");
    const auto root = fixture.path();
    const auto out = root / "out";
    fs::create_directory(out);
    auto rejected = [&](const fs::path& file) {
        try { (void)athena::archive::extract(file, out); }
        catch (const std::exception&) { return true; }
        return false;
    };

    const auto safe = root / "safe.tar.gz";
    make_archive(safe, "pkg/file", AE_IFREG, "ok");
    const auto extracted = athena::archive::extract(safe, out);
    assert(std::ifstream(extracted / "file").good());

    const auto traversal = root / "traversal.tar.gz";
    make_archive(traversal, "pkg/../../escape", AE_IFREG, "bad");
    assert(rejected(traversal));
    assert(!fs::exists(root / "escape"));

    const auto symlink = root / "symlink.tar.gz";
    make_archive(symlink, "pkg/escape", AE_IFLNK, {}, "../../outside");
    assert(rejected(symlink));

    const auto special = root / "special.tar.gz";
    make_archive(special, "pkg/device", AE_IFCHR);
    assert(rejected(special));

    const auto duplicate = root / "duplicate.tar.gz";
    {
        std::unique_ptr<::archive, decltype(&archive_write_free)> writer(archive_write_new(), archive_write_free);
        assert(archive_write_add_filter_gzip(writer.get()) == ARCHIVE_OK);
        assert(archive_write_set_format_pax_restricted(writer.get()) == ARCHIVE_OK);
        assert(archive_write_open_filename(writer.get(), duplicate.c_str()) == ARCHIVE_OK);
        for (int i = 0; i != 2; ++i) {
            archive_entry* entry = archive_entry_new();
            archive_entry_set_pathname(entry, "pkg/same"); archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0644); archive_entry_set_size(entry, 1);
            assert(archive_write_header(writer.get(), entry) == ARCHIVE_OK);
            assert(archive_write_data(writer.get(), "x", 1) == 1); archive_entry_free(entry);
        }
        assert(archive_write_close(writer.get()) == ARCHIVE_OK);
    }
    assert(rejected(duplicate));

    const auto truncated = root / "truncated.tar.gz";
    make_archive(truncated, "pkg/file", AE_IFREG, "payload");
    fs::resize_file(truncated, fs::file_size(truncated) - 8);
    assert(rejected(truncated));
}
