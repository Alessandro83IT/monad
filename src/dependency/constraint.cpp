// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "constraint.hpp"

#include "../version/version.hpp"

namespace athena::dependency {

namespace {

bool satisfies_comparison(
    const VersionConstraint::Comparison& comparison,
    const std::string& version
)
{
    switch (comparison.op) {

        case ComparisonOperator::Equal:
            return !version::less(version, comparison.version) &&
                   !version::less(comparison.version, version);

        case ComparisonOperator::NotEqual:
            return version::less(version, comparison.version) ||
                   version::less(comparison.version, version);

        case ComparisonOperator::Less:
            return version::less(version, comparison.version);

        case ComparisonOperator::LessEqual:
            return version::less(version, comparison.version) ||
                   (!version::less(version, comparison.version) &&
                    !version::less(comparison.version, version));

        case ComparisonOperator::Greater:
            return version::less(comparison.version, version);

        case ComparisonOperator::GreaterEqual:
            return version::less(comparison.version, version) ||
                   (!version::less(version, comparison.version) &&
                    !version::less(comparison.version, version));
    }

    return false;
}

} // namespace

bool satisfies(
    const VersionConstraint& constraint,
    const std::string& version
)
{
    switch (constraint.kind) {

        case VersionConstraint::Kind::Any:
            return true;

        case VersionConstraint::Kind::Comparison:
            return satisfies_comparison(
                constraint.comparison_value,
                version
            );

        case VersionConstraint::Kind::And:
            for (const auto& child : constraint.constraints) {
                if (!satisfies(child, version)) {
                    return false;
                }
            }

            return true;

        case VersionConstraint::Kind::Or:
            for (const auto& child : constraint.constraints) {
                if (satisfies(child, version)) {
                    return true;
                }
            }

            return false;

        case VersionConstraint::Kind::Not:
            if (!constraint.constraint) {
                return false;
            }

            return !satisfies(
                *constraint.constraint,
                version
            );
    }

    return false;
}

} // namespace athena::dependency
