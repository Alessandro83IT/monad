// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/repository/repository.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using athena::repository::Repository;

namespace {

void write_package(
    const std::filesystem::path& path,
    const std::string& name,
    const std::string& version
)
{
    /*
     * The repository test uses real package definition files so that
     * Repository exercises the complete loader → Package → Index path.
     */
    std::ofstream file(path);

    file
        << "name = \"" << name << "\"\n"
        << "version = \"" << version << "\"\n"
        << "description = \"Repository test package\"\n"
        << "source = \"https://example.org/" << name << "-"
        << version << ".tar.gz\"\n"
        << "sha256 = \"test-sha256\"\n"
        << "build_system = \"cmake\"\n";
}

}

int main()
{
    const auto root =
        std::filesystem::temp_directory_path() /
        "athena-repository-test";

    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    /*
     * Create the parent directory for the nested package definition.
     */
    std::filesystem::create_directories(
        root / "zlib-1.3.2"
    );

    write_package(
        root / "zlib-1.3.2" / "package.toml",
        "zlib",
        "1.3.2"
    );

    write_package(
        root / "package.toml",
        "zlib",
        "1.2.13"
    );

    /*
     * Only package.toml files directly inside root are loaded.
     * The nested zlib-1.3.2 definition must therefore remain ignored.
     */
    Repository repository;

    repository.load_directory(root);

    assert(repository.contains("zlib"));
    assert(!repository.contains("openssl"));

    const auto versions =
        repository.find("zlib");

    assert(versions.size() == 1);
    assert(versions[0].version == "1.2.13");

    /*
     * Verify that loading one explicit package file works independently
     * of directory discovery.
     */
    repository.load_package(
        root / "zlib-1.3.2" / "package.toml"
    );

    const auto all_versions =
        repository.find("zlib");

    assert(all_versions.size() == 2);
    assert(all_versions[0].version == "1.2.13");
    assert(all_versions[1].version == "1.3.2");

    assert(repository.index().contains("zlib"));

    std::filesystem::remove_all(root);

    std::cout
        << "Repository tests passed.\n";

    return 0;
}
