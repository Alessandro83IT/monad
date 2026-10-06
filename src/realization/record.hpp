// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <athena/core/install_plan.hpp>
#include "../installed/semantics.hpp"

namespace athena::realization {
// Internal durable association, outside the shared payload. Producers may
// assert authority only after successful ObjectStore publication/verification.
// Pure validation/decoding checks structure, never existence or integrity.
struct RealizationRecord {
    core::CandidateIdentity candidate;
    core::ArtifactId artifact_id;
    std::optional<core::BuildInputId> build_input_id;
    core::ArtifactOrigin origin = core::ArtifactOrigin::AmbientSourceBuild;
    // Absent only for pre-M2.4 records. Never infer empty dependencies for them.
    std::optional<installed::SemanticState> semantics;

    void validate() const;
    // Canonical reference relative to the configured authoritative store root.
    // Derived, not independently mutable or a second artifact identity.
    std::string object_reference() const;
    bool operator==(const RealizationRecord&) const = default;
    bool operator<(const RealizationRecord& other) const;
};

// Independent TOML envelope: v1 for historical records, v2 requires semantics.
// Neither codec supplies a persistence location or performs I/O.
std::string serialize(const RealizationRecord& record);
RealizationRecord deserialize_record(const std::string& document);
}
