// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "identity.hpp"
#include <variant>

namespace athena::activation {
struct HistoricalCurrent {
    std::uint64_t generation;
    std::string bytes; // Exact compatibility input, including optional one LF.
    bool operator==(const HistoricalCurrent&) const = default;
};
struct CurrentV1 {
    std::uint64_t generation;
    ActivationId activation_id;
    PlanDigest plan_digest;
    bool operator==(const CurrentV1&) const = default;
};
using CurrentSelection = std::variant<HistoricalCurrent, CurrentV1>;
CurrentSelection parse_current_selection(std::string_view);
// Historical serialization preserves compatibility bytes; it is not a new-write policy.
std::string serialize(const CurrentSelection&);
std::uint64_t selected_generation(const CurrentSelection&);
}
