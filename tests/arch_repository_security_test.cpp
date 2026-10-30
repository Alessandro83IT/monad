// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "temporary_directory.hpp"
#include "../src/repository/arch_repository.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using athena::repository::ArchRepository;

namespace {
struct Member {
    std::string name;
    mode_t type = AE_IFREG;
    std::string bytes;
    std::string link;
    la_int64_t declared = -1;
};

void make_archive(const fs::path& path, const std::vector<Member>& members, bool zstd = false) {
    std::unique_ptr<::archive, decltype(&archive_write_free)> writer(archive_write_new(), archive_write_free);
    assert(writer);
    assert((zstd ? archive_write_add_filter_zstd(writer.get()) : archive_write_add_filter_gzip(writer.get())) == ARCHIVE_OK);
    assert(archive_write_set_format_pax_restricted(writer.get()) == ARCHIVE_OK);
    assert(archive_write_open_filename(writer.get(), path.c_str()) == ARCHIVE_OK);
    for (const auto& member : members) {
        archive_entry* entry = archive_entry_new();
        assert(entry);
        archive_entry_set_pathname(entry, member.name.c_str());
        archive_entry_set_filetype(entry, member.type);
        archive_entry_set_perm(entry, member.type == AE_IFDIR ? 0755 : 0644);
        if (!member.link.empty()) {
            if (member.type == AE_IFLNK) archive_entry_set_symlink(entry, member.link.c_str());
            else archive_entry_set_hardlink(entry, member.link.c_str());
        }
        const auto size = member.declared >= 0 ? member.declared :
            (member.type == AE_IFREG ? static_cast<la_int64_t>(member.bytes.size()) : 0);
        archive_entry_set_size(entry, size);
        const int header = archive_write_header(writer.get(), entry);
        assert(header == ARCHIVE_OK);
        if (!member.bytes.empty())
            assert(archive_write_data(writer.get(), member.bytes.data(), member.bytes.size()) ==
                static_cast<la_ssize_t>(member.bytes.size()));
        archive_entry_free(entry);
    }
    assert(archive_write_close(writer.get()) == ARCHIVE_OK);
}
}

int main() {
    athena::test::TemporaryDirectory fixture("athena-arch-security");
    const auto root = fixture.path();
    ArchRepository repository(root / "unused.db");

    const auto safe_archive = root / "safe.pkg.tar.zst";
    make_archive(safe_archive, {{".PKGINFO", AE_IFREG, "pkgname = safe\n"},
                               {"usr", AE_IFDIR}, {"usr/bin/tool", AE_IFREG, "tool"}}, true);
    const auto safe_output = root / "safe-output";
    repository.extract_package(safe_archive, safe_output);
    assert(fs::is_regular_file(safe_output / ".PKGINFO"));
    assert(fs::is_regular_file(safe_output / "usr/bin/tool"));
    assert((fs::status(safe_output).permissions() & fs::perms::owner_exec) != fs::perms::none);
    assert((fs::status(safe_output / "usr/bin").permissions() & fs::perms::owner_exec) != fs::perms::none);

    const auto links_archive = root / "links.pkg.tar.gz";
    make_archive(links_archive, {{"pkg/file", AE_IFREG, "same bytes"},
        {"pkg/hard", AE_IFREG, {}, "pkg/file"},
        {"usr/lib/tool-real", AE_IFREG, "tool"},
        {"usr/bin/tool", AE_IFLNK, {}, "../../lib/tool-real"}});
    const auto links_output = root / "links-output";
    repository.extract_package(links_archive, links_output);
    struct stat regular_stat{}, hardlink_stat{};
    assert(::stat((links_output / "pkg/file").c_str(), &regular_stat) == 0);
    assert(::stat((links_output / "pkg/hard").c_str(), &hardlink_stat) == 0);
    assert(regular_stat.st_ino == hardlink_stat.st_ino && regular_stat.st_dev == hardlink_stat.st_dev);
    assert(fs::is_symlink(links_output / "usr/bin/tool"));
    assert(fs::read_symlink(links_output / "usr/bin/tool") == "../../lib/tool-real");

    auto rejected = [&](const std::string& label, const std::vector<Member>& members,
                        const std::string& diagnostic = {}) {
        const auto archive = root / (label + ".pkg.tar.gz");
        const auto output = root / (label + "-output");
        make_archive(archive, members);
        bool failed = false;
        try { repository.extract_package(archive, output); }
        catch (const std::exception& error) {
            failed = diagnostic.empty() || std::string(error.what()).find(diagnostic) != std::string::npos;
        }
        assert(failed);
        assert(!fs::exists(output)); // failed extraction leaves no partial tree
    };

    rejected("parent-traversal", {{"pkg/../../escape", AE_IFREG, "bad"}});
    rejected("absolute", {{"/tmp/escape", AE_IFREG, "bad"}});
    rejected("backslash", {{"pkg\\..\\escape", AE_IFREG, "bad"}});
    rejected("symlink", {{"pkg/link", AE_IFLNK, {}, "../../outside"}});
    rejected("symlink-chain-escape", {{"pkg/a", AE_IFLNK, {}, "b"},
                                        {"pkg/b", AE_IFLNK, {}, "../../outside"}});
    rejected("symlink-cycle", {{"pkg/a", AE_IFLNK, {}, "b"}, {"pkg/b", AE_IFLNK, {}, "a"}});
    rejected("hardlink", {{"pkg/link", AE_IFREG, {}, "../../outside"}});
    rejected("hardlink-forward", {{"pkg/link", AE_IFREG, {}, "pkg/file"},
                                   {"pkg/file", AE_IFREG, "content"}});
    rejected("special", {{"pkg/device", AE_IFCHR}});
    rejected("duplicate", {{"pkg/file", AE_IFREG, "one"}, {"pkg/file", AE_IFREG, "two"}});
    rejected("file-directory-collision", {{"pkg/file", AE_IFREG, "one"}, {"pkg/file/child", AE_IFREG, "two"}});
    rejected("oversize", {{"pkg/large", AE_IFREG, {}, {}, 2LL * 1024 * 1024 * 1024 + 1}},
             "expanded size limit exceeded");

    const auto corrupt = root / "corrupt.pkg.tar.gz";
    make_archive(corrupt, {{"pkg/file", AE_IFREG, "payload"}});
    fs::resize_file(corrupt, fs::file_size(corrupt) - 8);
    bool corrupt_rejected = false;
    try { repository.extract_package(corrupt, root / "corrupt-output"); }
    catch (const std::exception&) { corrupt_rejected = true; }
    assert(corrupt_rejected && !fs::exists(root / "corrupt-output"));

    bool existing_destination_rejected = false;
    try { repository.extract_package(safe_archive, safe_output); }
    catch (const std::exception&) { existing_destination_rejected = true; }
    assert(existing_destination_rejected && fs::is_regular_file(safe_output / ".PKGINFO"));
    const auto safe_archive_link = root / "safe-archive-link";
    fs::create_symlink(safe_archive, safe_archive_link);
    bool archive_symlink_rejected = false;
    try { repository.extract_package(safe_archive_link, root / "archive-link-output"); }
    catch (const std::exception&) { archive_symlink_rejected = true; }
    assert(archive_symlink_rejected && !fs::exists(root / "archive-link-output"));

    const auto db_path = root / "unsafe.db.tar";
    make_archive(db_path, {{"pkg/../escape/desc", AE_IFREG, "%NAME%\nbad\n"}});
    bool database_rejected = false;
    try { ArchRepository bad(db_path); bad.load(); }
    catch (const std::exception&) { database_rejected = true; }
    assert(database_rejected);

    auto rejected_database = [&](const std::string& label, const std::vector<Member>& members) {
        const auto archive = root / ("db-" + label + ".tar.gz");
        make_archive(archive, members);
        bool failed = false;
        try { ArchRepository bad(archive); bad.load(); }
        catch (const std::exception&) { failed = true; }
        assert(failed);
    };
    rejected_database("symlink", {{"pkg/desc", AE_IFLNK, {}, "../../outside"}});
    rejected_database("hardlink", {{"pkg/desc", AE_IFREG, {}, "../../outside"}});
    rejected_database("special", {{"pkg/device", AE_IFCHR}});
    rejected_database("duplicate", {{"pkg/desc", AE_IFREG, "%NAME%\none\n"},
                                     {"pkg/desc", AE_IFREG, "%NAME%\ntwo\n"}});
    rejected_database("collision", {{"pkg", AE_IFREG, "file"},
                                     {"pkg/desc", AE_IFREG, "%NAME%\nbad\n"}});
    rejected_database("oversize", {{"pkg/desc", AE_IFREG, {}, {}, 16LL * 1024 * 1024 + 1}});

    const auto corrupt_db = root / "db-corrupt.tar.gz";
    make_archive(corrupt_db, {{"pkg/desc", AE_IFREG, "%NAME%\nvalid\n"}});
    fs::resize_file(corrupt_db, fs::file_size(corrupt_db) - 8);
    bool corrupt_db_rejected = false;
    try { ArchRepository bad(corrupt_db); bad.load(); }
    catch (const std::exception&) { corrupt_db_rejected = true; }
    assert(corrupt_db_rejected);

    const auto source = root / "download-source";
    { std::ofstream output(source); output << "fresh database"; }
    const auto cache = root / "cache.db";
    { std::ofstream output(cache); output << "old database"; }
    ArchRepository downloader(cache);
    const auto local_url = "file://" + source.string();
    downloader.download(local_url);
    { std::ifstream input(cache); std::string bytes; std::getline(input, bytes); assert(bytes == "fresh database"); }
    athena::package::Package package;
    package.source = local_url;
    const auto package_download = root / "downloaded.pkg.tar.zst";
    downloader.download_package(package, package_download);
    package.sha256 = "361e6df767d8f39a29ed8ce6cc2ab4658a8a19fe4936cf877bfc7598ae579d26";
    downloader.verify_package_sha256(package, package_download);
    package.sha256 = "0000000000000000000000000000000000000000000000000000000000000000";
    bool wrong_hash_rejected = false;
    try { downloader.verify_package_sha256(package, package_download); }
    catch (const std::exception&) { wrong_hash_rejected = true; }
    assert(wrong_hash_rejected);
    bool failed_download = false;
    try { downloader.download("unsupported-scheme://not-a-real-url"); }
    catch (const std::exception&) { failed_download = true; }
    assert(failed_download);
    { std::ifstream input(cache); std::string bytes; std::getline(input, bytes); assert(bytes == "fresh database"); }

    const auto symlink_package = root / "package-link";
    fs::create_symlink(source, symlink_package);
    bool symlink_hash_rejected = false;
    try { downloader.verify_package_sha256(package, symlink_package); }
    catch (const std::exception&) { symlink_hash_rejected = true; }
    assert(symlink_hash_rejected);
    const auto real_package = root / "real-package";
    { std::ofstream output(real_package); output << "package"; }
    const auto real_keyring = root / "real-keyring";
    { std::ofstream output(real_keyring); output << "keyring"; }
    const auto symlink_keyring = root / "keyring-link";
    fs::create_symlink(real_keyring, symlink_keyring);
    bool symlink_keyring_rejected = false;
    try { downloader.verify_package_signature(package, real_package, symlink_keyring); }
    catch (const std::exception&) { symlink_keyring_rejected = true; }
    assert(symlink_keyring_rejected);
}
