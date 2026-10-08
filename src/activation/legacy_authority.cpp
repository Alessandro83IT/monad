// SPDX-License-Identifier: GPL-3.0-or-later
#include "legacy.hpp"
#include "codec_detail.hpp"
#include <fcntl.h>
#include <unistd.h>
namespace athena::activation {
core::detail::Fd LegacyAccess::create_record(const ProjectionContext& p, int parent, std::string_view name) {
    core::detail::leaf(name); const std::string leaf(name);
    open_how how{}; how.flags = O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC; how.mode = 0600;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_XDEV;
    const int raw = p.io_.open(parent, leaf.c_str(), how);
    if (raw < 0) throw LegacyError(LegacyCause::Io, ProjectionOutcome::NotMutated, errno, "Exclusive legacy administrative record creation");
    return core::detail::Fd::adopt(raw, p.io_.durable);
}
generation::ValidatedGenerationDocument LegacyAccess::generation_document(const ProjectionContext& p, std::uint64_t id) {
    codec_detail::generation(id);
    auto fail = [](std::string_view message, int error = 0) -> void {
        throw LegacyError(LegacyCause::HistoricalAuthority, ProjectionOutcome::NotMutated, error, message);
    };
    const auto name = std::to_string(id);
    open_how how{}; how.flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_XDEV;
    const int raw = p.io_.open(p.generation_.get(), name.c_str(), how);
    if (raw < 0) fail("Unsafe historical generation directory", errno);
    auto directory = core::detail::Fd::adopt(raw);
    const auto ds = p.identity(directory.get());
    if (!S_ISDIR(ds.stx_mode) || ds.stx_uid != ::geteuid() || (ds.stx_mode & 07022) || ds.stx_mnt_id != p.identity(p.generation_.get()).stx_mnt_id)
        fail("Unsafe historical generation directory metadata/mount");
    p.unchanged(p.generation_.get(), name, directory.get());
    how.flags = O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;
    const int file_raw = p.io_.open(directory.get(), "generation.toml", how);
    if (file_raw < 0) fail("Unsafe historical generation document", errno);
    auto file = core::detail::Fd::adopt(file_raw);
    const auto before = p.identity(file.get());
    constexpr unsigned required = STATX_SIZE | STATX_MTIME | STATX_CTIME;
    if ((before.stx_mask & required) != required || !S_ISREG(before.stx_mode) || before.stx_uid != ::geteuid() ||
        before.stx_nlink != 1 || (before.stx_mode & 07022) || before.stx_size > max_document_size || before.stx_mnt_id != ds.stx_mnt_id)
        fail("Unsafe historical generation document metadata");
    p.unchanged(directory.get(), "generation.toml", file.get());
    std::string bytes; char buffer[4096];
    for (;;) {
        auto count = ::read(file.get(), buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) fail("Read historical generation document", errno);
        if (!count) break;
        if (static_cast<std::size_t>(count) > max_document_size - bytes.size()) fail("Historical generation document byte bound");
        bytes.append(buffer, count);
    }
    const auto after = p.identity(file.get());
    if (before.stx_size != after.stx_size || before.stx_mode != after.stx_mode || before.stx_uid != after.stx_uid ||
        before.stx_nlink != after.stx_nlink || after.stx_size != bytes.size() ||
        before.stx_mtime.tv_sec != after.stx_mtime.tv_sec || before.stx_mtime.tv_nsec != after.stx_mtime.tv_nsec ||
        before.stx_ctime.tv_sec != after.stx_ctime.tv_sec || before.stx_ctime.tv_nsec != after.stx_ctime.tv_nsec)
        fail("Historical generation document changed during read");
    p.unchanged(directory.get(), "generation.toml", file.get()); p.unchanged(p.generation_.get(), name, directory.get());
    return generation::validate_generation_document(bytes, id);
}
}
