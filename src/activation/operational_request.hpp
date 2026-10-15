// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "operational_plan.hpp"
#include "plan_persistence.hpp"

namespace athena::activation {
// Immutable claims, NOT ownership, durability evidence or execution authority.
// No filesystem access, locks, identity generation or rebasing occurs here.
class PreparedOperationalRequest {
public:
    // Even an rvalue is copied: moves from caller-owned containers can preserve
    // mutable element references. Only the detached snapshot is normalized/moved.
    static PreparedOperationalRequest create(const OperationalPlanInput&,
        const std::optional<PlanDigest>& expected_digest = {});
    const OperationalPlanInput& input() const noexcept { return input_; }
    const PlanIdentity& identity() const noexcept { return identity_; }
    static constexpr std::uint64_t format_version = 1;
private:
    PreparedOperationalRequest(OperationalPlanInput input, PlanIdentity identity)
        : input_(std::move(input)), identity_(std::move(identity)) {}
    OperationalPlanInput input_;
    PlanIdentity identity_;
};
}
