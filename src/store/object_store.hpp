// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <athena/core/types.hpp>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace athena::store {
enum class ObjectErrorCode {
    MalformedArtifactId, InvalidSource, SourceIdentityMismatch, StoreDirectory,
    PrivateStaging, Materialization, Publication, ExistingObjectVerification,
    CollisionCorruption, PostPublicationIntegrity, Cleanup
};
class ObjectError : public std::runtime_error {
public:
    ObjectError(ObjectErrorCode code, const std::string& detail)
        : std::runtime_error(detail), code_(code) {}
    ObjectErrorCode code() const noexcept { return code_; }
    const std::string& cleanup_failure() const noexcept { return cleanup_failure_; }
    void record_cleanup_failure(std::string detail) { cleanup_failure_ = std::move(detail); }
private:
    ObjectErrorCode code_;
    std::string cleanup_failure_;
};

// Internal Linux v1 payload store. Root must already exist and be managed by
// Athena, with no hostile namespace writers. Sources must be quiescent.
// GC deletion is selected by ArtifactId only; no arbitrary-path removal.
class ObjectStore {
public:
    explicit ObjectStore(std::filesystem::path root);
    std::filesystem::path object_path(const core::ArtifactId& id) const;

    // Enumerate authoritative v1 objects by ArtifactId. Only canonical
    // lowercase SHA-256 object names are returned; unrelated namespace
    // entries are outside ObjectStore ownership.
    std::vector<core::ArtifactId> list() const;

    // Remove one authoritative object selected only by ArtifactId.
    // Returns false when the object is already absent. Requires Linux openat2
    // and statx mount IDs; unavailable identity or mount crossings fail closed.
    bool remove_if_present(const core::ArtifactId& id) const;

    std::filesystem::path publish(const std::filesystem::path& staging_root,
                                  const core::ArtifactId& expected) const;
    void verify(const core::ArtifactId& expected) const;
private:
    std::filesystem::path root_;
};
}
