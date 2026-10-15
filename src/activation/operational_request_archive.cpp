// SPDX-License-Identifier: GPL-3.0-or-later
#include "operational_request_archive.hpp"
#include "operational_request_codec.hpp"
#include "../hash/sha256.hpp"
#include <fcntl.h>
#include <linux/fs.h>
#include <unistd.h>
#include <cerrno>
#include <system_error>

namespace athena::activation {
namespace d = core::detail;
namespace {
[[noreturn]] void invalid(const char* text) { throw std::runtime_error(text); }
[[noreturn]] void io_error(const char* text) {
    const int error = errno;
    throw std::system_error(error, std::generic_category(), text);
}
bool overlaps(std::string_view a, std::string_view b) {
    auto contains = [](std::string_view parent, std::string_view child) {
        return parent == child || (child.starts_with(parent) &&
            (parent == "/" || (child.size() > parent.size() && child[parent.size()] == '/')));
    };
    return contains(a,b) || contains(b,a);
}
RequestArchiveError failure(RequestArchiveStage stage, bool uncertain) {
    return {stage, uncertain ? RequestArchiveOutcome::Uncertain : RequestArchiveOutcome::NotMutated,
        std::current_exception()};
}
}
RequestArchiveError::RequestArchiveError(RequestArchiveStage s, RequestArchiveOutcome o, std::exception_ptr cause)
    : std::runtime_error("Operational request archive failed; inspect underlying error"),
      stage(s), outcome(o), underlying(std::move(cause)) {}
RequestArchiveReference identify_operational_request(const PreparedOperationalRequest& request) {
    return {request.identity(), RequestEnvelopeDigest(hash::sha256(serialize_operational_request(request)))};
}

OperationalRequestArchive::OperationalRequestArchive(std::filesystem::path path,
    ProjectionBinding binding, RequestArchiveIo io)
    : path_(std::move(path)), leaf_(path_.filename().native()), binding_(std::move(binding)), io_(std::move(io)) {
    binding_.validate(); validate_absolute_locator(path_.native()); d::leaf(leaf_);
    for (const auto* authority : {&binding_.generation_authority, &binding_.store_authority, &binding_.projection_root})
        if (overlaps(path_.native(), *authority)) invalid("Archive overlaps an operational authority");
    parent_ = d::open_directory(path_.parent_path(), false, true);
    open_how how{}; how.flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV;
    const auto raw = io_.files.open(parent_.get(), leaf_.c_str(), how);
    if (raw < 0) io_error("Open private request archive");
    root_ = d::Fd::adopt(raw);
    root_identity_ = stat(root_.get());
    d::require_durable_directory(root_.get());
    validate_root();
}
OperationalRequestArchive OperationalRequestArchive::open(const std::filesystem::path& path,
    const ProjectionBinding& binding, RequestArchiveIo io) {
    try { return {path, binding, std::move(io)}; }
    catch (...) { throw failure(RequestArchiveStage::Validation, false); }
}
struct statx OperationalRequestArchive::stat(int fd, std::string_view name) const {
    struct statx value{}; const std::string leaf(name);
    if (io_.files.stat(fd, leaf.c_str(), AT_SYMLINK_NOFOLLOW | (name.empty() ? AT_EMPTY_PATH : 0),
        STATX_BASIC_STATS | STATX_MNT_ID, &value) < 0) io_error("Request archive statx");
    constexpr auto required = STATX_TYPE | STATX_MODE | STATX_UID | STATX_INO | STATX_NLINK |
        STATX_SIZE | STATX_MTIME | STATX_CTIME | STATX_MNT_ID;
    if ((value.stx_mask & required) != required) invalid("Incomplete request archive statx identity");
    return value;
}
bool OperationalRequestArchive::identity(const struct statx& a, const struct statx& b) {
    return a.stx_ino == b.stx_ino && a.stx_dev_major == b.stx_dev_major &&
        a.stx_dev_minor == b.stx_dev_minor && a.stx_mnt_id == b.stx_mnt_id &&
        a.stx_mode == b.stx_mode && a.stx_uid == b.stx_uid;
}
bool OperationalRequestArchive::observation(const struct statx& a, const struct statx& b) {
    return identity(a,b) && a.stx_size == b.stx_size && a.stx_nlink == b.stx_nlink &&
        a.stx_mtime.tv_sec == b.stx_mtime.tv_sec && a.stx_mtime.tv_nsec == b.stx_mtime.tv_nsec &&
        a.stx_ctime.tv_sec == b.stx_ctime.tv_sec && a.stx_ctime.tv_nsec == b.stx_ctime.tv_nsec;
}
void OperationalRequestArchive::validate_root() const {
    const auto now = stat(root_.get());
    if (!S_ISDIR(now.stx_mode) || now.stx_uid != ::geteuid() || (now.stx_mode & 07777) != 0700 ||
        !identity(now, root_identity_) || !identity(now, stat(parent_.get(), leaf_)))
        invalid("Unsafe or substituted request archive");
    // Also reject replacement of an ancestor while retaining old directory fds.
    auto named = d::open_directory(path_, false, true);
    if (!identity(now, stat(named.get()))) invalid("Request archive ancestry substituted");
}
void OperationalRequestArchive::authority(ProjectionContext& context, const ProjectionState& state) const {
    if (context.binding() != binding_ || state.generations() != binding_.generation_authority)
        invalid("Request archive authority mismatch");
    state.revalidate(); context.revalidate(); validate_root();
}
void OperationalRequestArchive::file(const struct statx& value) const {
    if (!S_ISREG(value.stx_mode) || value.stx_uid != ::geteuid() || (value.stx_mode & 07777) != 0600 ||
        value.stx_nlink != 1 || value.stx_size > max_operational_request_size ||
        value.stx_mnt_id != root_identity_.stx_mnt_id || value.stx_dev_major != root_identity_.stx_dev_major ||
        value.stx_dev_minor != root_identity_.stx_dev_minor)
        invalid("Unsafe request file metadata, mount or size");
}
void OperationalRequestArchive::unchanged(std::string_view name, const Record& record) const {
    validate_root();
    if (!observation(record.observed, stat(record.fd.get())) ||
        !observation(record.observed, stat(root_.get(), name))) invalid("Request file substituted or changed");
}
std::optional<OperationalRequestArchive::Record> OperationalRequestArchive::read(std::string_view name,
    bool writable) const {
    const std::string leaf(name); d::leaf(name);
    struct statx named{};
    if (io_.files.stat(root_.get(), leaf.c_str(), AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_MNT_ID, &named) < 0) {
        if (errno == ENOENT) return {};
        io_error("Inspect request file");
    }
    file(stat(root_.get(), name)); // Reject special entries before opening.
    open_how how{}; how.flags = (writable ? O_RDWR | O_APPEND : O_RDONLY) | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV;
    const auto raw = io_.files.open(root_.get(), leaf.c_str(), how);
    if (raw < 0) io_error("Constrained request file open");
    Record result{d::Fd::adopt(raw), {}, {}};
    result.observed = stat(result.fd.get()); file(result.observed); unchanged(name, result);
    char buffer[4096];
    for (;;) {
        const auto count = io_.files.pread(result.fd.get(), buffer, sizeof(buffer), result.bytes.size());
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) io_error("Read request file");
        if (static_cast<std::size_t>(count) > sizeof(buffer) ||
            static_cast<std::size_t>(count) > max_operational_request_size - result.bytes.size())
            invalid("Request read exceeds bound");
        if (!count) break;
        result.bytes.append(buffer, count);
    }
    unchanged(name, result);
    if (result.bytes.size() != result.observed.stx_size) invalid("Incomplete request read");
    return result;
}
void OperationalRequestArchive::sync(int fd, RequestArchiveStage next, RequestArchiveStage& stage, bool& uncertain) {
    stage = next; uncertain = true;
    int rc; do { rc = io_.files.fsync(fd); } while (rc < 0 && errno == EINTR);
    if (rc < 0) io_error("Request archive barrier");
}
void OperationalRequestArchive::finish(std::string_view final, Record& record, std::string_view pending,
    std::optional<Record>& residue, RequestArchiveStage& stage, bool& uncertain) {
    unchanged(final, record);
    sync(record.fd.get(), RequestArchiveStage::FileBarrier, stage, uncertain);
    unchanged(final, record);
    sync(root_.get(), RequestArchiveStage::DirectoryBarrier, stage, uncertain);
    sync(parent_.get(), RequestArchiveStage::DirectoryBarrier, stage, uncertain);
    unchanged(final, record);
    if (residue) {
        if (residue->bytes != record.bytes) invalid("Conflicting request preparation residue");
        unchanged(pending, *residue);
        stage = RequestArchiveStage::Cleanup; uncertain = true;
        const std::string name(pending);
        if (io_.files.unlink(root_.get(), name.c_str(), 0) < 0) io_error("Remove exact request preparation residue");
        sync(root_.get(), RequestArchiveStage::DirectoryBarrier, stage, uncertain);
        unchanged(final, record);
    }
}
RequestPublication OperationalRequestArchive::publish(std::string_view pending, std::string_view final,
    Record& record, RequestArchiveStage& stage, bool& uncertain) {
    unchanged(pending, record);
    sync(record.fd.get(), RequestArchiveStage::FileBarrier, stage, uncertain);
    unchanged(pending, record);
    // Persist the preparation name before promotion; a crash never requires
    // reconstructing a complete record from an untrusted enumeration.
    sync(root_.get(), RequestArchiveStage::DirectoryBarrier, stage, uncertain);
    unchanged(pending, record);
    stage = RequestArchiveStage::Publication; uncertain = true;
    const std::string source(pending), destination(final);
    if (io_.files.rename(root_.get(), source.c_str(), root_.get(), destination.c_str(), RENAME_NOREPLACE) < 0)
        io_error("Publish immutable request without replacement");
    // Rename may change ctime. Reacquire observation, but require same instance
    // and exact bytes before establishing final publication barriers.
    auto published = read(final);
    if (!published || !identity(record.observed, published->observed) || published->bytes != record.bytes)
        invalid("Published request instance/content mismatch");
    std::optional<Record> none;
    finish(final, *published, pending, none, stage, uncertain);
    return RequestPublication::Published;
}
RequestPublication OperationalRequestArchive::persist(ProjectionContext& context, const ProjectionState& state,
    const PreparedOperationalRequest& request) {
    auto stage = RequestArchiveStage::Validation; bool uncertain = false;
    try {
        authority(context, state); request.identity().activation_id.validate(); request.identity().digest.validate();
        (void)revalidate_operational_request(context, state, request);
        const auto bytes = serialize_operational_request(request);
        const auto final = request.identity().activation_id.value() + ".toml";
        const auto pending = "." + request.identity().activation_id.value() + ".pending";
        stage = RequestArchiveStage::Read;
        auto published = read(final); auto preparation = read(pending, !published);
        if (published) {
            if (published->bytes != bytes || (preparation && preparation->bytes != bytes))
                invalid("Immutable request collision");
            finish(final, *published, pending, preparation, stage, uncertain);
            return RequestPublication::AlreadyPublished;
        }
        if (preparation && !std::string_view(bytes).starts_with(preparation->bytes))
            invalid("Preparation does not match original complete request");
        if (!preparation) {
            stage = RequestArchiveStage::Preparation;
            open_how how{}; how.flags = O_CREAT | O_EXCL | O_RDWR | O_APPEND | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;
            how.mode = 0600;
            how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV;
            uncertain = true; // Conservatively arm before attempting creation.
            const auto raw = io_.files.open(root_.get(), pending.c_str(), how);
            if (raw < 0) io_error("Exclusive request preparation");
            uncertain = true;
            preparation = Record{d::Fd::adopt(raw), {}, {}};
            preparation->observed = stat(preparation->fd.get()); file(preparation->observed);
        }
        unchanged(pending, *preparation);
        stage = RequestArchiveStage::Write;
        auto remaining = std::string_view(bytes).substr(preparation->bytes.size());
        while (!remaining.empty()) {
            uncertain = true;
            const auto count = io_.write(preparation->fd.get(), remaining.data(), remaining.size());
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { if (!count) errno = EIO; io_error("Write request preparation"); }
            if (static_cast<std::size_t>(count) > remaining.size()) invalid("Invalid request write count");
            remaining.remove_prefix(count);
        }
        const auto instance = preparation->observed;
        preparation = read(pending);
        if (!preparation || !identity(instance, preparation->observed) || preparation->bytes != bytes)
            invalid("Request preparation changed during write");
        stage = RequestArchiveStage::Validation;
        authority(context, state); (void)revalidate_operational_request(context, state, request);
        return publish(pending, final, *preparation, stage, uncertain);
    } catch (...) { throw failure(stage, uncertain); }
}
RecoveredOperationalRequest OperationalRequestArchive::recover(ProjectionContext& context, const ProjectionState& state,
    const RequestArchiveReference& expected) {
    auto stage = RequestArchiveStage::Validation; bool uncertain = false;
    try {
        authority(context, state); expected.plan.activation_id.validate(); expected.plan.digest.validate(); expected.envelope.validate();
        const auto final = expected.plan.activation_id.value() + ".toml";
        const auto pending = "." + expected.plan.activation_id.value() + ".pending";
        stage = RequestArchiveStage::Read;
        auto published = read(final); auto preparation = read(pending);
        auto* record = published ? &*published : preparation ? &*preparation : nullptr;
        if (!record) invalid("Expected request is absent");
        if (RequestEnvelopeDigest(hash::sha256(record->bytes)) != expected.envelope)
            invalid("Recovered envelope differs from independent exact-byte digest");
        auto request = parse_operational_request(record->bytes);
        if (request.identity().activation_id != expected.plan.activation_id || request.identity().digest != expected.plan.digest)
            invalid("Recovered request differs from independent expected identity");
        if (published && preparation && published->bytes != preparation->bytes)
            invalid("Conflicting request preparation residue");
        stage = RequestArchiveStage::Validation;
        authority(context, state); (void)revalidate_operational_request(context, state, request, expected.plan.digest);
        if (!published) return {std::move(request), publish(pending, final, *preparation, stage, uncertain)};
        finish(final, *published, pending, preparation, stage, uncertain);
        return {std::move(request), RequestPublication::AlreadyPublished};
    } catch (...) { throw failure(stage, uncertain); }
}
}
