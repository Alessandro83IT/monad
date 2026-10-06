// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../package/package.hpp"
#include "../../include/athena/core/install_plan.hpp"

#include <optional>
#include <stdexcept>
#include <string>

namespace athena::repository {

enum class ResolverCandidateErrorCode {
    InvalidCandidate,
    SnapshotMismatch,
    InvalidMetadata
};

class ResolverCandidateError : public std::invalid_argument {
public:
    ResolverCandidateError(ResolverCandidateErrorCode code, std::string message)
        : std::invalid_argument(std::move(message)), code_(code) {}
    ResolverCandidateErrorCode code() const noexcept { return code_; }
private:
    ResolverCandidateErrorCode code_;
};

/*
 * One resolver-visible package candidate. The Package base is the semantic
 * view used by M1.9; public_candidate and snapshot retain its original public
 * repository record for identity-aware selection and result construction.
 * Legacy candidates have neither public identity value and never receive
 * fabricated repository identifiers.
 */
struct ResolverCandidate : athena::package::Package {
    std::optional<athena::core::RepositoryCandidate> public_candidate;
    std::optional<athena::core::RepositorySnapshotRef> snapshot;
    std::optional<athena::core::CandidateIdentity> identity;

    static ResolverCandidate legacy(const athena::package::Package& package);
    static ResolverCandidate from_public(
        const athena::core::RepositoryCandidate& candidate,
        const athena::core::RepositorySnapshotRef& snapshot
    );

    void validate() const;
    bool has_public_identity() const noexcept { return identity.has_value(); }
};

} // namespace athena::repository
