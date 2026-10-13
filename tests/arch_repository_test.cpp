// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "temporary_directory.hpp"

#include "../src/repository/arch_repository.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void write_file(
    const std::filesystem::path& path,
    const std::string& content
)
{
    std::ofstream output(path);

    if (!output) {
        throw std::runtime_error(
            "Impossibile creare il fixture: " +
            path.string()
        );
    }

    output << content;
}

int run_command(const std::string& command)
{
    return std::system(command.c_str());
}

} // namespace

int main()
{
    athena::test::TemporaryDirectory fixture("athena-arch-repository-test");
    const auto root = fixture.path();

    const auto package_a =
        root / "openssl-3.5.2-1";

    const auto package_b =
        root / "libressl-4.0.0-1";

    std::filesystem::create_directories(package_a);
    std::filesystem::create_directories(package_b);

    write_file(
        package_a / "desc",
        "%FILENAME%\n"
        "openssl-3.5.2-1-x86_64.pkg.tar.zst\n"
        "\n"
        "%NAME%\n"
        "openssl\n"
        "\n"
        "%VERSION%\n"
        "3.5.2-1\n"
        "\n"
        "%DESC%\n"
        "The Open Source SSL library\n"
        "\n"
        "%CSIZE%\n"
        "123456\n"
        "\n"
        "%SHA256SUM%\n"
        "abcdef123456\n"
        "\n"
        "%DEPENDS%\n"
        "glibc>=2.38\n"
        "zlib\n"
        "\n"
        "%OPTDEPENDS%\n"
        "ca-certificates: for certificate validation\n"
        "\n"
        "%MAKEDEPENDS%\n"
        "perl\n"
        "\n"
        "%CHECKDEPENDS%\n"
        "valgrind\n"
        "\n"
        "%PROVIDES%\n"
        "libssl.so\n"
        "libcrypto.so\n"
    );

    write_file(
        package_b / "desc",
        "%FILENAME%\n"
        "libressl-4.0.0-1-x86_64.pkg.tar.zst\n"
        "\n"
        "%NAME%\n"
        "libressl\n"
        "\n"
        "%VERSION%\n"
        "4.0.0-1\n"
        "\n"
        "%DESC%\n"
        "LibreSSL\n"
        "\n"
        "%SHA256SUM%\n"
        "fedcba654321\n"
        "\n"
        "%DEPENDS%\n"
        "glibc\n"
        "\n"
        "%PROVIDES%\n"
        "ssl\n"
        "tls\n"
    );

    const auto database =
        root / "core.db.tar";

    const std::string command =
        "tar -cf '" +
        database.string() +
        "' -C '" +
        root.string() +
        "' " +
        package_a.filename().string() +
        " " +
        package_b.filename().string();

    const int tar_status = run_command(command);
    assert(tar_status == 0);

    athena::repository::ArchRepository repository(
        database,
        "https://mirror.example.org/core/os/x86_64"
    );

    repository.load();

    assert(repository.contains("openssl"));
    assert(repository.contains("libressl"));
    assert(!repository.contains("missing"));

    const auto openssl =
        repository.find("openssl");

    assert(openssl.size() == 1);
    assert(openssl[0].version == "3.5.2-1");
    assert(
        openssl[0].source ==
        "https://mirror.example.org/core/os/x86_64/"
        "openssl-3.5.2-1-x86_64.pkg.tar.zst"
    );
    assert(openssl[0].sha256 == "abcdef123456");

    assert(openssl[0].dependencies.size() == 5);

    bool found_runtime = false;
    bool found_optional = false;
    bool found_build = false;
    bool found_test = false;

    for (const auto& dependency : openssl[0].dependencies) {

        switch (dependency.kind) {

        case athena::dependency::DependencyKind::Runtime:
            assert(
                dependency.context ==
                athena::dependency::DependencyContext::Target
            );
            found_runtime = true;
            break;

        case athena::dependency::DependencyKind::Optional:
            assert(
                dependency.context ==
                athena::dependency::DependencyContext::Target
            );
            found_optional = true;
            break;

        case athena::dependency::DependencyKind::Build:
            assert(
                dependency.context ==
                athena::dependency::DependencyContext::Build
            );
            found_build = true;
            break;

        case athena::dependency::DependencyKind::Test:
            assert(
                dependency.context ==
                athena::dependency::DependencyContext::Build
            );
            found_test = true;
            break;

        default:
            break;
        }
    }

    assert(found_runtime);
    assert(found_optional);
    assert(found_build);
    assert(found_test);

    assert(
        openssl[0].provides.capabilities.size() == 2
    );
    assert(
        openssl[0].provides.capabilities[0] ==
        "libssl.so"
    );

    const auto libressl =
        repository.find("libressl");

    assert(libressl.size() == 1);
    assert(
        libressl[0].provides.capabilities.size() == 2
    );

    std::cout
        << "Arch repository tests passed.\n";

    return 0;
}
