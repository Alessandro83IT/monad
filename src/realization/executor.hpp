// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "record.hpp"
#include "../store/object_store.hpp"
#include <exception>

namespace athena::realization {
enum class ExecutionErrorCode {
    InvalidInput, UnsupportedKind, UnsupportedRecipe, Acquisition,
    SourceVerification, Extraction, Build, ArtifactIdentity, Publication, Cleanup
};
class ExecutionError : public std::runtime_error {
public:
    ExecutionError(ExecutionErrorCode code, std::string detail,
                   std::exception_ptr cause = {})
        : std::runtime_error(std::move(detail)), code_(code), cause_(cause) {}
    ExecutionErrorCode code() const noexcept { return code_; }
    const std::exception_ptr& cause() const noexcept { return cause_; }
    const std::string& cleanup_failure() const noexcept { return cleanup_; }
    void record_cleanup_failure(std::string detail) { cleanup_ = std::move(detail); }
private:
    ExecutionErrorCode code_;
    std::exception_ptr cause_;
    std::string cleanup_;
};
struct ExecutionOptions {
    // Existing, executor-managed absolute directory without hostile writers.
    // C.4 rejects shell-sensitive path bytes used by the existing build backend.
    std::filesystem::path workspace_root;
};
// Preserves selections() order. Publishes only; never creates or activates a
// generation. Earlier objects may remain unreferenced after a later failure.
std::vector<RealizationRecord> realize(const core::InstallPlan& plan,
    const store::ObjectStore& objects, const ExecutionOptions& options);
}
