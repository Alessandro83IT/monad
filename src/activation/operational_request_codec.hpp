// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "operational_request.hpp"
#include "plan_retention.hpp"
#include <limits>

namespace athena::activation {
// Envelope bound includes hex expansion of two generation documents, existing
// intent/plan/source codecs, inventories and table overhead. Inner limits remain
// independently enforced; this bound does not enlarge D1/D8.1 budgets.
static_assert(max_retention_bytes <= std::numeric_limits<std::size_t>::max() / 4);
static_assert(max_document_size <=
    (std::numeric_limits<std::size_t>::max() - 4 * max_retention_bytes) / 8);
inline constexpr std::size_t max_operational_request_size =
    8 * max_document_size + 4 * max_retention_bytes;
std::string serialize_operational_request(const PreparedOperationalRequest&);
PreparedOperationalRequest parse_operational_request(std::string_view);
namespace request_codec_detail {
// Exact envelope preflight, shared with the factory. Existing D8.1/document
// limits remain in force; no expanded hex buffer is allocated by this check.
void validate_encoding(const OperationalPlanInput&, const OperationalPlanResult&);
// Only the canonical envelope grammar is admitted: root, source arrays/tables,
// path arrays and scalar leaves (depth <= 4). No TOML AST is constructed.
inline constexpr std::size_t max_envelope_nodes =
    9 + 8 * max_sources + 2 * max_retention_references;
struct EnvelopeLimits {
    std::size_t envelope_bytes = max_operational_request_size;
    std::size_t nodes = max_envelope_nodes;
    std::size_t sources_per_generation = max_sources;
    std::size_t paths_per_generation = max_retention_references;
    std::size_t path_bytes_per_generation = max_retention_bytes;
    std::size_t document_bytes = max_document_size;
    std::size_t path_bytes = max_path_size;
    std::size_t decoded_bytes = max_operational_request_size / 2;
};
// Allocation-free successful preflight; tighter budgets support small regression
// fixtures. Production parsing always uses the defaults. No semantic decoding.
void validate_envelope(std::string_view, const EnvelopeLimits& = {});
}
}
