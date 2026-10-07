// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <athena/core/types.hpp>
#include "../core/detail/filesystem.hpp"
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>

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

struct ObjectPublication {
    std::filesystem::path path;
    core::detail::Publication result; // Published means barriers completed; AlreadyExists means verified winner.
};

class TransactionPins;
class GcTrash;
class PreparedObject {
public:
    ~PreparedObject();
    PreparedObject(PreparedObject&&) noexcept;
    PreparedObject& operator=(PreparedObject&&) noexcept;
    const core::ArtifactId& artifact_id() const;
private:
    friend class ObjectStore;
    struct Impl;
    explicit PreparedObject(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
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

    // Authoritative deletion is available only through bound GarbageCollector.

    std::filesystem::path publish_unprotected_for_test(const std::filesystem::path& staging_root,
                                  const core::ArtifactId& expected,
                                  const core::detail::Syscalls& ops = core::detail::linux_syscalls()) const;
    // UNSAFE test-only M2.5-B mechanics seam. Production must use TransactionPins::publish.
    // No lifetime guarantee; deliberately named to prevent accidental transaction use.
    ObjectPublication publish_durable_unprotected_for_test(const std::filesystem::path&, const core::ArtifactId&,
                                     const core::detail::Syscalls& = core::detail::linux_syscalls()) const;
    PreparedObject prepare(const std::filesystem::path&, const core::ArtifactId&,
                           const core::detail::Syscalls& = core::detail::linux_syscalls()) const;
    const std::filesystem::path& root() const { return root_; }
    void verify(const core::ArtifactId& expected) const;
private:
    friend class TransactionPins;
    friend class GcTrash;
    // Private walker entry point; only a validated GcTrash claim can invoke it.
    static void reclaim_private_trash_payload(int, std::string_view,
        const core::detail::Syscalls&);
    ObjectPublication publish_prepared_locked(PreparedObject&, const TransactionPins&,
        const core::detail::Syscalls&) const;
    ObjectPublication finish_prepared(PreparedObject&, const core::detail::Syscalls&) const;
    std::filesystem::path root_;
};
}
