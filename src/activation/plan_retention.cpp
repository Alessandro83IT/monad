// SPDX-License-Identifier: GPL-3.0-or-later
#include "plan_retention.hpp"
#include "plan_persistence.hpp"
#include "../store/transaction_pins.hpp"
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace athena::activation {
namespace d = core::detail;
namespace a = store::administrative;
namespace {
using Cause = PlanPersistenceCause;
[[noreturn]] void reject(Cause cause, std::string_view message, int error = 0) {
    throw PlanPersistenceError(cause, ProjectionOutcome::NotMutated, error, message);
}
struct BindingObservation {
    d::Fd fd;
    struct statx metadata;
    std::string bytes;
};
constexpr const char* binding_name = ".athena-projection-authority.toml";
struct statx stat(const ProjectionIo& io, int fd, const char* name, int flags) {
    struct statx value{};
    if (io.stat(fd, name, flags, STATX_BASIC_STATS | STATX_MNT_ID, &value) < 0)
        reject(Cause::Io, "D5 binding statx", errno);
    // Shared D4 full-observation contract, including kernel returned field mask.
    (void)plan_detail::OperationEvidence::same_observation(value, value);
    return value;
}
BindingObservation read_binding(int generation, const ProjectionIo& io) {
    const auto parent = stat(io, generation, "", AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
    const auto before = stat(io, generation, binding_name, AT_SYMLINK_NOFOLLOW);
    if (!S_ISREG(before.stx_mode) || before.stx_uid != ::geteuid() || (before.stx_mode & 07777) != 0600 || before.stx_nlink != 1)
        reject(Cause::UnsafeEntry, "D5 unsafe projection binding");
    if (parent.stx_mnt_id != before.stx_mnt_id || parent.stx_dev_major != before.stx_dev_major || parent.stx_dev_minor != before.stx_dev_minor)
        reject(Cause::UnsafeEntry, "D5 projection binding mount crossing");
    if (before.stx_size > max_document_size) reject(Cause::AdministrativeLimit, "D5 binding byte limit");
    open_how how{};
    how.flags = O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV;
    const int raw = io.open(generation, binding_name, how);
    if (raw < 0) reject(Cause::Io, "D5 constrained projection binding open", errno);
    auto fd = d::Fd::adopt(raw, io.durable);
    auto check = [&] {
        if (!plan_detail::OperationEvidence::same_observation(before, stat(io, fd.get(), "", AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW)) ||
            !plan_detail::OperationEvidence::same_observation(before, stat(io, generation, binding_name, AT_SYMLINK_NOFOLLOW)))
            reject(Cause::Substituted, "D5 projection binding substituted");
    };
    check();
    std::string bytes; std::array<char, 4096> buffer{};
    for (;;) {
        const auto n = io.pread(fd.get(), buffer.data(), buffer.size(), static_cast<off_t>(bytes.size()));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) reject(Cause::Io, "D5 projection binding read", errno);
        if (static_cast<std::size_t>(n) > buffer.size()) reject(Cause::Io, "D5 invalid binding read count");
        if (!n) break;
        if (static_cast<std::size_t>(n) > max_document_size - bytes.size()) reject(Cause::AdministrativeLimit, "D5 binding byte limit");
        bytes.append(buffer.data(), static_cast<std::size_t>(n));
    }
    check();
    if (bytes.size() != before.stx_size) reject(Cause::Substituted, "D5 binding size/EOF disagreement");
    return {std::move(fd), before, std::move(bytes)};
}
}
PlanReferences scan_plan_references(const std::filesystem::path& generations, const core::FileLock& state,
    const std::optional<std::filesystem::path>& expected_store, ProjectionIo io) {
    const auto authority = a::locator(generations);
    if (!state.owns_lock() || state.path() != core::state_lock_path(authority))
        reject(Cause::AuthorityMismatch, "D5 requires held namespace-derived State");
    auto generation = d::open_directory(authority, false, true);
    const auto generation_before = d::stat_fd(generation.get());
    struct statx presence{};
    if (io.stat(generation.get(), binding_name, AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_MNT_ID, &presence) < 0) {
        if (errno != ENOENT) reject(Cause::Io, "D5 binding discovery", errno);
        return {}; // Only a successfully observed absent binding permits this.
    }
    auto binding_record = read_binding(generation.get(), io);
    const auto binding = parse_projection_binding(binding_record.bytes);
    if (binding.generation_authority != authority.native() ||
        (expected_store && binding.store_authority != a::locator(*expected_store).native()))
        reject(Cause::AuthorityMismatch, "D5 projection authority differs from caller");
    auto context = ProjectionContext::open(binding, store::ObjectStore(binding.store_authority), io);
    auto references = PlanAccess::retention(context);
    auto final_binding = read_binding(generation.get(), io);
    if (final_binding.bytes != binding_record.bytes ||
        !plan_detail::OperationEvidence::same_observation(final_binding.metadata, binding_record.metadata))
        reject(Cause::Substituted, "D5 discovery binding changed during retention scan");
    auto named = d::open_directory(authority, false, true);
    const auto current = d::stat_fd(named.get());
    if (current.st_dev != generation_before.st_dev || current.st_ino != generation_before.st_ino)
        reject(Cause::Substituted, "D5 generation authority substituted");
    return references;
}
}
