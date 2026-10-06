// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <filesystem>
#include <athena/core/types.hpp>
#include <vector>

namespace athena::store {

/*
 * GarbageCollector removes immutable store entries that are no longer
 * reachable from any persisted Athena generation.
 *
 * A store entry referenced by at least one generation is a GC root and
 * must be preserved so that historical generations can still be used
 * for rollback.
 */
class GarbageCollector {

public:

    GarbageCollector(
        std::filesystem::path store_directory,
        std::filesystem::path state_directory
    );

    /*
     * Remove every valid Athena store entry that is not referenced by
     * any persisted generation.
     *
     * The returned paths identify the entries that were actually
     * removed.
     */
    std::vector<std::filesystem::path> collect();

private:

    std::filesystem::path store_directory_;
    std::filesystem::path state_directory_;

    /*
     * Determine which immutable store paths are reachable from all
     * persisted generations.
     */
    std::vector<std::filesystem::path> referenced_stores() const;

    /*
     * Collect ArtifactIds reachable from every persisted generation.
     *
     * Historical generations are roots just like the current generation:
     * collecting one of their artifacts would make rollback/switch invalid.
     */
    std::vector<core::ArtifactId> referenced_artifacts() const;

    /*
     * A valid store entry is identified by Athena metadata.
     */
    static bool is_store_entry(
        const std::filesystem::path& path
    );
};

}
