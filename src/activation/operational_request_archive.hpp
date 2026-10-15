// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "operational_request_runtime.hpp"
#include <functional>
#include <unistd.h>

namespace athena::activation {
enum class RequestArchiveStage { Validation, Read, Preparation, Write, FileBarrier,
    Publication, DirectoryBarrier, Cleanup };
// Archive-local outcome, NEVER a statement about selection/activation history.
enum class RequestArchiveOutcome { NotMutated, Uncertain };
struct RequestArchiveError : std::runtime_error {
    RequestArchiveStage stage;
    RequestArchiveOutcome outcome;
    std::exception_ptr underlying;
    RequestArchiveError(RequestArchiveStage, RequestArchiveOutcome, std::exception_ptr);
};
struct RequestArchiveIo {
    ProjectionIo files;
    std::function<ssize_t(int, const void*, size_t)> write = ::write;
};
enum class RequestPublication { Published, AlreadyPublished };
using RequestEnvelopeDigest = Sha256Digest<struct RequestEnvelopeDigestTag>;
struct RequestArchiveReference {
    PlanIdentity plan;
    RequestEnvelopeDigest envelope; // SHA-256 of exact canonical B1.1 bytes.
};
// Derive from the independently known complete request, not the archive candidate.
// This reference is an identity pin, not execution authority or a GC root.
RequestArchiveReference identify_operational_request(const PreparedOperationalRequest&);
struct RecoveredOperationalRequest {
    PreparedOperationalRequest request; // Immutable claims, NOT execution authority.
    RequestPublication publication;
};

// Internal immutable archive, not a selection journal or GC root. Caller supplies
// an existing durable, private 0700 effective-owner directory in trusted ancestry,
// dedicated to this binding, disjoint from generation/store/projection authorities.
// No implicit mkdir, binding initialization, ownership acquisition or lock occurs.
class OperationalRequestArchive {
public:
    static OperationalRequestArchive open(const std::filesystem::path&,
        const ProjectionBinding&, RequestArchiveIo = {});
    OperationalRequestArchive(OperationalRequestArchive&&) noexcept = default;
    OperationalRequestArchive& operator=(OperationalRequestArchive&&) noexcept = default;
    // Exclusive State already held. Caller authorizes archive use and protects
    // generations/inventories as in B1. A matching partial private preparation
    // can be resumed ONLY with the original complete supplied request.
    RequestPublication persist(ProjectionContext&, const ProjectionState&,
        const PreparedOperationalRequest&);
    // Independent expected identity is mandatory, never obtained by enumerating
    // this archive. Only complete canonical records may be promoted/recovered.
    // Revalidates through B1.3; no execution, rebase, recreation or cleanup of final
    // records. Execution separately requires caller authorization and B1 -> D8.2.
    RecoveredOperationalRequest recover(ProjectionContext&, const ProjectionState&,
        const RequestArchiveReference& independently_expected);
private:
    struct Record {
        core::detail::Fd fd;
        struct statx observed;
        std::string bytes;
    };
    OperationalRequestArchive(std::filesystem::path, ProjectionBinding, RequestArchiveIo);
    void validate_root() const;
    void authority(ProjectionContext&, const ProjectionState&) const;
    struct statx stat(int, std::string_view = {}) const;
    static bool identity(const struct statx&, const struct statx&);
    static bool observation(const struct statx&, const struct statx&);
    void file(const struct statx&) const;
    void unchanged(std::string_view, const Record&) const;
    std::optional<Record> read(std::string_view, bool writable = false) const;
    void sync(int, RequestArchiveStage, RequestArchiveStage&, bool&);
    RequestPublication publish(std::string_view, std::string_view, Record&,
        RequestArchiveStage&, bool&);
    void finish(std::string_view, Record&, std::string_view, std::optional<Record>&,
        RequestArchiveStage&, bool&);
    std::filesystem::path path_;
    std::string leaf_;
    ProjectionBinding binding_;
    RequestArchiveIo io_;
    core::detail::Fd parent_, root_;
    struct statx root_identity_{};
};
// For every operation: caller holds exclusive State after TransactionLease and
// before StoreGc, excludes equivalent-authority archive writers/mount changes,
// and keeps the archive dedicated to this configured binding across restarts.
// Request files do not pin generations or replace D5 retention/transaction pins.
}
