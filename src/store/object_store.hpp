#pragma once
#include <athena/core/types.hpp>
#include <filesystem>
#include <stdexcept>
#include <string>

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
// No mutation/removal API for authoritative objects; no legacy integration.
class ObjectStore {
public:
    explicit ObjectStore(std::filesystem::path root);
    std::filesystem::path object_path(const core::ArtifactId& id) const;
    std::filesystem::path publish(const std::filesystem::path& staging_root,
                                  const core::ArtifactId& expected) const;
    void verify(const core::ArtifactId& expected) const;
private:
    std::filesystem::path root_;
};
}
