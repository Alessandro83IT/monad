// SPDX-License-Identifier: GPL-3.0-or-later
#include "operational_request.hpp"
#include "operational_request_codec.hpp"
#include "plan_retention.hpp"
#include "../artifact/model.hpp"
#include <algorithm>

namespace athena::activation {
PreparedOperationalRequest PreparedOperationalRequest::create(
    const OperationalPlanInput& supplied, const std::optional<PlanDigest>& expected) {
    // Ownership audit: binding strings; base current/ownership variants; optional
    // base and target documents, source vectors and their source variants and
    // link/directory strings; witness vector, paths and owner variants; all IDs
    // and digests own their strings. No pointer, view or shared storage occurs.
    // Their recursive value copy detaches every caller-owned buffer, including
    // vector elements and heap-backed strings retained before create(std::move(x)).
    // Keep this copy before validation, sorting and every ownership transfer.
    OperationalPlanInput input = supplied;
    // Validate before sorting: sorting must never hide duplicate claims.
    const auto canonical = build_operational_plan(input);
    if (expected && *expected != canonical.digest)
        throw SchemaError(SchemaErrorCode::Reference, "Operational request digest mismatch");
    auto order = [](OperationalGeneration& g) {
        std::size_t count = 0, bytes = 0;
        for (auto& s : g.sources) {
            for (const auto* paths : {&s.links, &s.directories}) {
                if (paths->size() > max_retention_references - count)
                    throw SchemaError(SchemaErrorCode::Bounds, "Request generation path count exceeds bound");
                count += paths->size();
                for (const auto& path : *paths) {
                    if (path.size() > max_retention_bytes - bytes)
                        throw SchemaError(SchemaErrorCode::Bounds, "Request generation path bytes exceed bound");
                    bytes += path.size();
                }
            }
            std::sort(s.links.begin(), s.links.end(), artifact::path_bytes_less);
            std::sort(s.directories.begin(), s.directories.end(), artifact::path_bytes_less);
        }
        std::sort(g.sources.begin(), g.sources.end(), [](const auto& a, const auto& b) {
            return source_identity_less(a.source, b.source);
        });
    };
    if (input.base_generation) order(*input.base_generation);
    order(input.target);
    std::sort(input.old_witnesses.begin(), input.old_witnesses.end(), [](const auto& a, const auto& b) {
        return artifact::path_bytes_less(a.path, b.path);
    });
    const auto identity = PlanIdentity{input.activation_id, canonical.digest};
    request_codec_detail::validate_encoding(input, canonical);
    return PreparedOperationalRequest(std::move(input), identity);
}
}
