// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sha256.hpp"
#include <array>
#include <openssl/evp.h>
namespace athena::hash {
std::string sha256(std::string_view bytes)
{
    std::array<unsigned char, 32> digest{};
    unsigned int size = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), digest.data(), &size,
                   EVP_sha256(), nullptr) != 1 || size != digest.size())
        throw Error("SHA-256 digest calculation failed");
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "sha256:";
    for (unsigned char byte : digest) {
        result += hex[byte >> 4];
        result += hex[byte & 15];
    }
    return result;
}
}
