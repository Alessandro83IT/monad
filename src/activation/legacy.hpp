// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "projection_fs.hpp"
#include "../artifact/scanner.hpp"
#include <exception>

namespace athena::activation {
enum class LegacyCause { UnsafeSource, Manifest, SealMismatch, HistoricalAuthority,
    ProjectionConflict, PreparationConflict, WitnessMismatch, Io, Durability };
struct LegacyError : std::runtime_error {
    LegacyCause cause;
    ProjectionOutcome outcome;
    int error;
    std::exception_ptr underlying;
    LegacyError(LegacyCause, ProjectionOutcome, int, std::string_view, std::exception_ptr = {});
};
using LegacySource = std::variant<LegacyTreeSource, LegacyAliasSource>;
struct LegacyEntryMetadata {
    std::string path; // Empty means the source payload root.
    std::uint32_t type, permissions, uid, gid;
    bool operator==(const LegacyEntryMetadata&) const = default;
};
struct LegacyInventory {
    LegacySource source;
    artifact::NormalizedArtifact tree;
    std::vector<std::string> projection;
    std::vector<LegacyEntryMetadata> metadata;
};
// State is already held; methods never recursively acquire it. External writers
// must be excluded through verification AND the operation relying on its result.
// Every verification fsyncs the current instance; a returned inventory is not
// reusable durability evidence across a State/external-writer exclusion boundary.
// Alias verification acquires StoreGc after State.
LegacyInventory seal_legacy(ProjectionContext&, const ProjectionState&, std::uint64_t generation,
    std::string_view locator, const artifact::ScanLimits& = {});
LegacyInventory verify_legacy_seal(ProjectionContext&, const ProjectionState&, std::uint64_t generation,
    const LegacySource&, const artifact::ScanLimits& = {});

struct AdoptedEntry {
    std::uint64_t id, source;
    std::string path, target;
    WitnessReference witness;
    bool operator==(const AdoptedEntry&) const = default;
};
struct AdoptionRecord {
    AdoptionId id;
    ProjectionBinding binding;
    HistoricalCurrent current;
    generation::GenerationDocumentDigest generation_digest;
    std::vector<ActivationSource> sources;
    std::vector<AdoptedEntry> entries;
    std::vector<std::string> directories;
    void validate() const;
    bool operator==(const AdoptionRecord&) const = default;
};
std::string serialize(const AdoptionRecord&);
AdoptionRecord parse_adoption_record(std::string_view);
std::string adoption_source_locator(const ProjectionBinding&, const ActivationSource&);
// Expected paths and destinations are derived from current's validated document
// and seals, never supplied by the caller. Does not write current or managed links.
ProjectionOutcome adopt_legacy_projection(ProjectionContext&, const ProjectionState&, const AdoptionId&,
    const std::vector<LegacySource>&);

// Internal D2 resolution boundary. Validates a published record and authority,
// then returns only its canonical witness directory, never a caller path.
class LegacyAccess {
public:
    // Pure interpretation of scanned bytes; does not inspect or authorize a
    // filesystem source. Scanner safety remains a separate prerequisite.
    static std::vector<std::string> manifest_projection(const artifact::NormalizedArtifact&);
    static LegacyInventory inspect(ProjectionContext&, const ProjectionState&, std::uint64_t,
        std::string_view, bool publish, const artifact::ScanLimits&, const LegacySource* expected = nullptr);
    static ProjectionOutcome adopt(ProjectionContext&, const ProjectionState&, const AdoptionId&,
        const std::vector<LegacySource>&);
    static void namespace_shape(const ProjectionContext&);
    static core::detail::Fd resolve(const ProjectionContext&, std::string_view, std::string_view,
        const WitnessReference&);
private:
    static generation::ValidatedGenerationDocument generation_document(const ProjectionContext&, std::uint64_t);
    static core::detail::Fd create_record(const ProjectionContext&, int, std::string_view);
    static LegacyInventory inspect_impl(ProjectionContext&, const ProjectionState*, std::uint64_t,
        std::string_view, bool publish, const artifact::ScanLimits&, const LegacySource* expected);
    static AdoptionRecord expected(ProjectionContext&, const ProjectionState*, const AdoptionId&,
        HistoricalCurrent, const std::vector<LegacySource>&);
};
}
