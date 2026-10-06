// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

namespace athena::verify {

/*
 * Verify the SHA-256 checksum of a file.
 *
 * The calculated checksum is compared with the checksum declared
 * in the package definition.
 *
 * Returns true when the checksums match, false otherwise.
 */
bool verify_sha256(
    const std::filesystem::path& file,
    const std::string& expected_hash
);

}
