// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "temporary_directory.hpp"

#include <athena/core/api.hpp>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <type_traits>

using namespace athena::core;

namespace {

struct TempDirectory {
    athena::test::TemporaryDirectory owned{"athena-m14"};
    std::filesystem::path path = owned.path();
};

void write_package(const std::filesystem::path& path, const std::string& version,
                   const std::string& description = "fixture", int schema = 1)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << "schema_version = " << schema << "\n"
        << "name = \"sample\"\n"
        << "version = \"" << version << "\"\n"
        << "description = \"" << description << "\"\n"
        << "source = \"https://example.invalid/sample.tar.gz\"\n"
        << "sha256 = \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"\n"
        << "build_system = \"cmake\"\n";
}

LocalRepository repository_at(const std::filesystem::path& root)
{
    return LocalRepository({RepositoryId("test-local"), root});
}

}

int main()
{
    static_assert(std::is_base_of_v<Repository, LocalRepository>);

    bool rejected = false;
    try {
        LocalRepository invalid({RepositoryId("repo"), "relative/path"});
    } catch (const RepositoryError& error) {
        rejected = error.code() == RepositoryErrorCode::InvalidConfiguration;
    }
    assert(rejected);

    TempDirectory first_root;
    TempDirectory second_root;
    std::filesystem::create_directories(first_root.path / "packages");
    write_package(first_root.path / "packages/sample/1.0/package.toml", "1.0");
    write_package(first_root.path / "packages/sample/2.0/package.toml", "2.0");
    write_package(second_root.path / "packages/sample/1.0/package.toml", "1.0");
    write_package(second_root.path / "packages/sample/2.0/package.toml", "2.0");

    auto first = repository_at(first_root.path);
    auto second = repository_at(second_root.path);
    const auto snapshot = first.load_snapshot();
    const auto equivalent = second.load_snapshot();
    assert(snapshot.candidates().size() == 2);
    assert(snapshot == equivalent);
    assert(first.revision() == second.revision());
    assert(snapshot.id() == equivalent.id());
    assert(snapshot.candidates().front().source_recipe.has_value());

    RepositoryRequest request{
        SchemaVersion(1), {RepositoryId("other"), RepositoryId("test-local")},
        {PackageId("sample")}, std::nullopt
    };
    const auto matches = first.query(request);
    assert(matches.size() == 2);
    assert(matches[0].package.version == PackageVersion("1.0"));
    assert(matches[0].provenance.snapshot == snapshot.reference());
    assert(first.query({SchemaVersion(1), {RepositoryId("other")},
                        {PackageId("sample")}, {}}).empty());
    assert(first.query(request) == matches);

    auto detached = matches.front();
    detached.package.description = "changed";
    assert(snapshot.candidates().front().package.description == "fixture");

    TempDirectory empty_root;
    const auto empty = repository_at(empty_root.path).load_snapshot();
    assert(empty.candidates().empty());

    TempDirectory absent_parent;
    bool missing = false;
    try {
        (void)repository_at(absent_parent.path / "missing").load_snapshot();
    } catch (const RepositoryError& error) {
        missing = error.code() == RepositoryErrorCode::MetadataMissing;
    }
    assert(missing);

    TempDirectory invalid_root;
    write_package(invalid_root.path / "bad/package.toml", "1.0");
    { std::ofstream out(invalid_root.path / "bad/package.toml"); out << "not toml = [\n"; }
    bool invalid_metadata = false;
    try { (void)repository_at(invalid_root.path).load_snapshot(); }
    catch (const RepositoryError& error) {
        invalid_metadata = error.code() == RepositoryErrorCode::MetadataInvalid;
    }
    assert(invalid_metadata);

    TempDirectory schema_root;
    write_package(schema_root.path / "bad/package.toml", "1.0", "fixture", 99);
    bool incompatible = false;
    try { (void)repository_at(schema_root.path).load_snapshot(); }
    catch (const RepositoryError& error) {
        incompatible = error.code() == RepositoryErrorCode::UnsupportedSchema;
    }
    assert(incompatible);

    TempDirectory candidate_root;
    write_package(candidate_root.path / "bad/package.toml", "");
    bool invalid_candidate = false;
    try { (void)repository_at(candidate_root.path).load_snapshot(); }
    catch (const RepositoryError& error) {
        invalid_candidate = error.code() == RepositoryErrorCode::InvalidCandidate;
    }
    assert(invalid_candidate);

    std::cout << "Local repository tests passed.\n";
}
