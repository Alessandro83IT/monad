#include "../src/repository/repository_index.hpp"

#include <cassert>
#include <iostream>

using athena::package::Package;
using athena::repository::RepositoryIndex;


void test_semantic_providers()
{
    RepositoryIndex index;

    Package openssl{
        "openssl",
        "3.5",
        "OpenSSL",
        "",
        "",
        "",
        {}
    };

    openssl.provides.capabilities = {"ssl", "tls"};
    openssl.provides.features = {"openssl"};

    Package libressl{
        "libressl",
        "3.9",
        "LibreSSL",
        "",
        "",
        "",
        {}
    };

    libressl.provides.capabilities = {"ssl", "tls"};
    libressl.provides.features = {"libressl"};

    index.add(openssl);
    index.add(libressl);

    const auto ssl =
        index.find_capability("ssl");

    assert(ssl.size() == 2);
    assert(ssl[0].name == "libressl");
    assert(ssl[1].name == "openssl");

    const auto openssl_feature =
        index.find_feature("openssl");

    assert(openssl_feature.size() == 1);
    assert(openssl_feature[0].name == "openssl");

    assert(index.find_capability("missing").empty());
    assert(index.find_feature("missing").empty());
}

int main()
{
    RepositoryIndex index;

    /*
     * Add multiple versions of the same package plus an unrelated
     * package. The repository must preserve all available versions.
     */
    index.add({
        "zlib",
        "1.3.2",
        "Zlib compression library",
        "https://example.org/zlib-1.3.2.tar.xz",
        "sha256-zlib-1.3.2",
        "cmake",
        {}
    });

    index.add({
        "zlib",
        "1.2.13",
        "Zlib compression library",
        "https://example.org/zlib-1.2.13.tar.xz",
        "sha256-zlib-1.2.13",
        "cmake",
        {}
    });

    index.add({
        "openssl",
        "3.5.0",
        "OpenSSL",
        "https://example.org/openssl-3.5.0.tar.xz",
        "sha256-openssl-3.5.0",
        "cmake",
        {}
    });

    assert(index.contains("zlib"));
    assert(index.contains("openssl"));
    assert(!index.contains("missing"));

    const auto zlib_versions =
        index.find("zlib");

    assert(zlib_versions.size() == 2);

    /*
     * Results must be ordered according to Athena version semantics,
     * independently of insertion order.
     */
    assert(zlib_versions[0].version == "1.2.13");
    assert(zlib_versions[1].version == "1.3.2");

    const auto openssl_versions =
        index.find("openssl");

    assert(openssl_versions.size() == 1);
    assert(openssl_versions[0].version == "3.5.0");

    const auto missing =
        index.find("missing");

    assert(missing.empty());

    std::cout
        << "Repository index tests passed.\n";

    test_semantic_providers();

    return 0;
}
