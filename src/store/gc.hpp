// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <filesystem>
#include <athena/core/types.hpp>
#include <vector>
#include "transaction_pins.hpp"

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

    struct GenerationDirectoryTag {};
    GarbageCollector(std::filesystem::path store_directory,
                     std::filesystem::path generation_directory, GenerationDirectoryTag);
    explicit GarbageCollector(AuthorityContext authority);

    /*
     * Remove every valid Athena store entry that is not referenced by
     * any persisted generation.
     *
     * The returned paths identify the entries that were actually
     * removed.
     */
    std::vector<std::filesystem::path> collect(
        const core::detail::Syscalls& = core::detail::linux_syscalls());
    void reclaim_trash(const core::detail::Syscalls& = core::detail::linux_syscalls());

private:

    std::filesystem::path store_directory_;
    std::filesystem::path state_directory_;
    std::filesystem::path bound_store_;

    static bool is_store_entry(
        const std::filesystem::path& path
    );
};

}
