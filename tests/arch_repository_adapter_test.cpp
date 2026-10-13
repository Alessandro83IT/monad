// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "temporary_directory.hpp"

#include <athena/core/api.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <type_traits>

using namespace athena::core;

namespace {

struct TempDirectory {
    athena::test::TemporaryDirectory owned{"athena-m15"};
    std::filesystem::path path = owned.path();
};

std::string desc(const std::string& name, const std::string& version,
                 const std::string& arch = "x86_64",
                 const std::string& extra = {})
{
    return "%FILENAME%\n" + name + "-" + version + "-" + arch + ".pkg.tar.zst\n\n"
        "%NAME%\n" + name + "\n\n%VERSION%\n" + version + "\n\n"
        "%DESC%\nFixture binary\n\n%ARCH%\n" + arch + "\n\n"
        "%CSIZE%\n1234\n\n%SHA256SUM%\n"
        "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd\n\n"
        "%PGPSIG%\nfixture-signature-metadata\n\n"
        "%DEPENDS%\nglibc>=2.38\nzlib\n\n"
        "%OPTDEPENDS%\nca-certificates: certificate support\n\n"
        "%MAKEDEPENDS%\nmeson\n\n%CHECKDEPENDS%\npytest\n\n"
        "%PROVIDES%\nvirtual-demo=1.0\n\n%CONFLICTS%\nold-demo<2.0\n\n"
        "%LICENSE%\nMIT\n\n%URL%\nhttps://example.invalid/project\n\n" + extra;
}

void write(const std::filesystem::path& path, const std::string& data)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << data;
}

std::filesystem::path make_db(const std::filesystem::path& root,
                              const std::vector<std::string>& records,
                              const std::string& name = "core.db.tar")
{
    std::vector<std::string> members;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto member = "pkg" + std::to_string(i) + "/desc";
        write(root / member, records[i]);
        members.push_back("pkg" + std::to_string(i));
    }
    const auto db = root / name;
    std::string command = "tar -cf '" + db.string() + "' -C '" + root.string() + "'";
    for (const auto& member : members) command += " '" + member + "'";
    if (members.empty()) command = "tar -cf '" + db.string() + "' --files-from /dev/null";
    const int tar_status = std::system(command.c_str());
    assert(tar_status == 0);
    return db;
}

ArchRepository repository(const std::filesystem::path& db)
{
    return ArchRepository({RepositoryId("arch-core"), "core", db,
        "https://mirror.example.invalid/core/os/x86_64", "x86_64"});
}

}

int main()
{
    static_assert(std::is_base_of_v<Repository, ArchRepository>);
    bool rejected = false;
    try {
        ArchRepository bad({RepositoryId{}, "core", "/tmp/db.db", "https://m/x", "x86_64"});
    } catch (const RepositoryError& e) {
        rejected = e.code() == RepositoryErrorCode::InvalidConfiguration;
    }
    assert(rejected);
    rejected = false;
    try {
        ArchRepository bad({RepositoryId("r"), "core", "relative.db", "https://m/x", "x86_64"});
    } catch (const RepositoryError& e) {
        rejected = e.code() == RepositoryErrorCode::InvalidConfiguration;
    }
    assert(rejected);
    rejected = false;
    try {
        ArchRepository bad({RepositoryId("r"), "core", "/tmp/db.db", "", ""});
    } catch (const RepositoryError& e) {
        rejected = e.code() == RepositoryErrorCode::InvalidConfiguration;
    }
    assert(rejected);

    TempDirectory first_root;
    const auto db = make_db(first_root.path, {
        desc("demo", "1.9-1"), desc("demo", "1.10-1"), desc("helper", "2.0-1"),
        desc("otherarch", "3.0-1", "aarch64"), desc("universal", "1.0-1", "any")
    });
    const auto arch = repository(db);
    const auto snapshot = arch.load_snapshot();
    assert(arch.repository_id() == RepositoryId("arch-core"));
    assert(snapshot.candidates().size() == 4); // aarch64 candidate is filtered; "any" remains available
    assert(snapshot.candidates().front().imported_binary.has_value());
    assert(!snapshot.candidates().front().source_recipe.has_value());
    const auto old_demo = std::find_if(snapshot.candidates().begin(), snapshot.candidates().end(),
        [](const auto& candidate) { return candidate.package.version == PackageVersion("1.9-1"); });
    assert(old_demo != snapshot.candidates().end());
    const auto& imported = *old_demo->imported_binary;
    assert(imported.format == ImportedBinaryFormat::ArchPackage);
    assert(imported.archive_reference.value().starts_with("https://mirror.example.invalid/"));
    assert(imported.original_digest.has_value());
    assert(imported.size_bytes == 1234);
    assert(imported.ecosystem_fields.at("FILENAME").front() == "demo-1.9-1-x86_64.pkg.tar.zst");
    assert(imported.ecosystem_fields.at("PGPSIG").front() == "fixture-signature-metadata");
    assert(imported.ecosystem_fields.at("ATHENA_REPOSITORY").front() == "core");
    assert(snapshot.candidates().front().package.architectures == std::vector<std::string>{"x86_64"});
    assert(snapshot.candidates().front().package.capabilities == std::vector<std::string>{"virtual-demo"});
    assert(snapshot.candidates().front().package.conflicts.size() == 1);
    assert(snapshot.candidates().front().package.dependencies.size() == 2);
    assert(imported.ecosystem_fields.at("OPTDEPENDS").front() ==
           "ca-certificates: certificate support");
    assert(imported.ecosystem_fields.contains("MAKEDEPENDS"));
    assert(imported.ecosystem_fields.contains("CHECKDEPENDS"));

    const auto queried = arch.query({SchemaVersion(1), {RepositoryId("arch-core")},
                                     {PackageId("demo")}, TargetEnvironment{
                                         "linux", "x86_64", "gnu", "x86_64-linux-gnu"}});
    assert(queried.size() == 2);
    assert(queried[0].package.version == PackageVersion("1.9-1"));
    assert(queried[1].package.version == PackageVersion("1.10-1"));
    assert(queried[0].provenance.snapshot == snapshot.reference());
    assert(arch.query({SchemaVersion(1), {RepositoryId("other")},
                       {PackageId("demo")}, {}}).empty());
    assert(arch.query({SchemaVersion(1), {RepositoryId("arch-core")},
                       {PackageId("demo")}, {}}) == queried);

    TempDirectory copy_root;
    const auto copy_db = make_db(copy_root.path, {
        desc("helper", "2.0-1"), desc("demo", "1.10-1"), desc("demo", "1.9-1"),
        desc("otherarch", "3.0-1", "aarch64"), desc("universal", "1.0-1", "any")
    });
    const auto copy_snapshot = repository(copy_db).load_snapshot();
    assert(snapshot.revision() == copy_snapshot.revision());
    assert(snapshot.id() == copy_snapshot.id());
    assert(serialize(snapshot) == serialize(copy_snapshot));
    auto detached = snapshot.candidates().front();
    detached.package.description = "detached change";
    assert(snapshot.candidates().front().package.description == "Fixture binary");

    TempDirectory changed_root;
    const auto changed_db = make_db(changed_root.path, {
        desc("demo", "1.9-1"), desc("demo", "1.10-1"), desc("helper", "2.0-1"),
        desc("otherarch", "3.0-1", "aarch64"), desc("universal", "1.0-1", "any"),
        desc("new-package", "1.0-1")
    });
    const auto changed_snapshot = repository(changed_db).load_snapshot();
    assert(snapshot.revision() != changed_snapshot.revision());
    assert(snapshot.id() != changed_snapshot.id());

    TempDirectory empty_root;
    const auto empty_db = make_db(empty_root.path, {});
    const auto empty = repository(empty_db).load_snapshot();
    assert(empty.candidates().empty());

    TempDirectory malformed_root;
    const auto malformed_db = make_db(malformed_root.path, {
        "%NAME%\nbroken\n\n%VERSION%\n1.0-1\n\n%ARCH%\nx86_64\n\n"
        "%FILENAME%\nbroken.pkg.tar.zst\n\n%DEPENDS%\ninvalid>=\n\n"
    });
    bool malformed = false;
    try { (void)repository(malformed_db).load_snapshot(); }
    catch (const RepositoryError& e) { malformed = e.code() == RepositoryErrorCode::MetadataInvalid; }
    assert(malformed);

    TempDirectory candidate_root;
    const auto candidate_db = make_db(candidate_root.path, {
        "%NAME%\nbroken\n\n%VERSION%\n1.0-1\n\n%ARCH%\nx86_64\n\n%DESC%\nmissing filename\n\n"
    });
    bool invalid_candidate = false;
    try { (void)repository(candidate_db).load_snapshot(); }
    catch (const RepositoryError& e) { invalid_candidate = e.code() == RepositoryErrorCode::InvalidCandidate; }
    assert(invalid_candidate);

    TempDirectory format_root;
    const auto unsupported = make_db(format_root.path, {}, "core.txt");
    bool unsupported_format = false;
    try { (void)repository(unsupported).load_snapshot(); }
    catch (const RepositoryError& e) { unsupported_format = e.code() == RepositoryErrorCode::UnsupportedSchema; }
    assert(unsupported_format);

    std::cout << "Arch repository adapter tests passed.\n";
}
