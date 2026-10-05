#pragma once

#include <filesystem>
#include <compare>
#include <stdexcept>
#include <string>
#include <utility>

namespace athena::core {

struct TypeError : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

template <typename Tag>
class Identifier {
public:
    Identifier() = default;
    explicit Identifier(std::string value) : value_(std::move(value))
    {
        if (value_.empty()) throw TypeError("Identifier cannot be empty");
    }
    const std::string& value() const noexcept { return value_; }
    bool empty() const noexcept { return value_.empty(); }
    auto operator<=>(const Identifier&) const = default;
private:
    std::string value_;
};

struct PackageIdTag; struct RepositoryIdTag; struct ArtifactIdTag; struct SnapshotIdTag; struct SystemPolicyIdTag;
struct BuildInputIdTag; struct DigestTag;
using PackageId = Identifier<PackageIdTag>;
using RepositoryId = Identifier<RepositoryIdTag>;
using ArtifactId = Identifier<ArtifactIdTag>;
// Digest-shaped value with a distinct type from artifacts and build inputs.
using SnapshotId = Identifier<SnapshotIdTag>;
using SystemPolicyId = Identifier<SystemPolicyIdTag>;
using BuildInputId = Identifier<BuildInputIdTag>;
using ContentDigest = Identifier<DigestTag>;
using ManifestDigest = Identifier<struct ManifestDigestTag>;

class PackageVersion {
public:
    explicit PackageVersion(std::string value) : value_(std::move(value))
    {
        if (value_.empty()) throw TypeError("PackageVersion cannot be empty");
    }
    const std::string& value() const noexcept { return value_; }
    auto operator<=>(const PackageVersion&) const = default;
private:
    std::string value_;
};

struct TargetEnvironment {
    std::string platform;
    std::string architecture;
    std::string abi;
    std::string triple;
    bool operator==(const TargetEnvironment&) const = default;
};

enum class ArtifactOrigin {
    BuiltFromAthenaIR, // Controlled Athena IR build; requires BuildInputId.
    ImportedBinary, // No producer build identity/attestation in this contract.
    AmbientSourceBuild // Uncontrolled native source build; BuildInputId absent.
};

struct Provenance {
    ArtifactOrigin origin = ArtifactOrigin::BuiltFromAthenaIR;
    std::string repository;
    std::string repository_snapshot;
    std::string source_reference;
    std::string signer;
    bool operator==(const Provenance&) const = default;
};

struct DigestSet {
    ContentDigest output_digest;
    ManifestDigest manifest_digest;
    bool operator==(const DigestSet&) const = default;
};

} // namespace athena::core
