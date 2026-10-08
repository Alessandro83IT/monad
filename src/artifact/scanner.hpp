// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "model.hpp"
#include <filesystem>
#include <sys/stat.h>

namespace athena::artifact {

// Read-only Linux scanner. The caller supplies a private, quiescent staging
// tree. No symlink component of the root locator is followed. Observed changes
// fail; this is not an atomic filesystem snapshot against hostile writers.
// Metadata inspection must be complete, including normally hidden trusted.*
// xattrs. Missing inspection privileges/support fail closed.
NormalizedArtifact scan(const std::filesystem::path& root);

// Bounded scan of an already pinned real directory. The caller retains the
// descriptor and validates its named authority. All descendants must remain on
// its statx mount; metadata inspection retains the ordinary scanner gate.
struct ScanLimits {
    std::size_t entries = 65536;
    std::size_t path = 4095;
    std::size_t component = 255;
    std::size_t depth = 128;
    std::size_t bytes = 64 * 1024 * 1024;
};
struct ScanObservation { std::string path; struct stat status; };
// Observations are ephemeral substitution evidence, never durable identifiers.
NormalizedArtifact scan_bounded(int root, const ScanLimits& = {}, std::vector<ScanObservation>* observations = nullptr);
// Metadata safety for a legacy alias inode outside a scanned payload root.
// Leaf only; applies the same complete xattr/flags and hardlink gates.
void validate_alias_metadata(int parent, std::string_view leaf);

} // namespace athena::artifact
