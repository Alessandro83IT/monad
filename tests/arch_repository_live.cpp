// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/repository/arch_repository.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>

int main()
{
    const std::filesystem::path database =
        "/tmp/athena-arch-live/core.db";

    athena::repository::ArchRepository repository(
        database,
        "https://geo.mirror.pkgbuild.com/core/os/x86_64"
    );

    const std::string database_url =
        "https://geo.mirror.pkgbuild.com/core/os/x86_64/core.db";

    repository.download(database_url);

    repository.load();

    assert(std::filesystem::exists(database));

    const auto glibc =
        repository.find("glibc");

    assert(!glibc.empty());

    std::cout
        << "Arch live repository loaded successfully.\n"
        << "glibc versions found: "
        << glibc.size()
        << '\n';

    const auto& package = glibc.front();

    /*
     * Use the filename supplied by Arch repository metadata rather than
     * reconstructing it from the package name and version.
     */
    const auto source_separator =
        package.source.find_last_of('/');

    assert(source_separator != std::string::npos);

    const auto filename =
        package.source.substr(source_separator + 1);

    assert(!filename.empty());

    const auto package_path =
        std::filesystem::path("/tmp/athena-arch-live") /
        filename;

    repository.download_package(
        package,
        package_path
    );

    std::cout
        << "Arch package downloaded: "
        << package_path
        << '\n';

    repository.verify_package_sha256(
        package,
        package_path
    );

    std::cout
        << "Arch package SHA-256 verified successfully.\n";

    repository.verify_package_signature(
        package,
        package_path,
        "/etc/pacman.d/gnupg/pubring.gpg"
    );

    std::cout
        << "Arch package signature verified successfully.\n";

    const auto extraction_path =
        std::filesystem::path(
            "/tmp/athena-arch-live/glibc-extracted"
        );

    std::filesystem::remove_all(extraction_path);

    repository.extract_package(
        package_path,
        extraction_path
    );

    assert(
        std::filesystem::exists(
            extraction_path / ".PKGINFO"
        )
    );

    std::cout
        << "Arch package extracted successfully.\n";

    const auto imported_package =
        repository.load_package_metadata(
            extraction_path
        );

    assert(imported_package.name == package.name);
    assert(imported_package.version == package.version);
    assert(!imported_package.dependencies.empty());

    std::cout
        << "PKGINFO package: "
        << imported_package.name
        << ' '
        << imported_package.version
        << '\n'
        << "PKGINFO dependencies: "
        << imported_package.dependencies.size()
        << '\n';

    return 0;
}
