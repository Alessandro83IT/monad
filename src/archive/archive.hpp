// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

namespace athena::archive {

/*
 * Extract a source archive into a destination directory.
 *
 * The function currently supports tar archives compressed with gzip.
 * It returns the directory containing the extracted package source.
 *
 * The archive module is responsible only for extraction. It does not
 * know how the source will later be configured or compiled.
 */
std::filesystem::path extract(
    const std::filesystem::path& archive,
    const std::filesystem::path& destination
);

}
