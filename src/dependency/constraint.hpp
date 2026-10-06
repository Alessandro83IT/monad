// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "comparison.hpp"

#include <string>
#include <memory>
#include <utility>
#include <vector>

namespace athena::dependency {

/*
 * A version constraint describes which versions of a target
 * expression are acceptable.
 *
 * The constraint model is intentionally independent from the
 * concrete versioning scheme. The interpretation of the version
 * string will eventually be delegated to the version subsystem.
 */
struct VersionConstraint {

    enum class Kind {
        Any,
        Comparison,
        And,
        Or,
        Not
    };

    /*
     * A single version comparison.
     *
     * Examples:
     *
     *     >= 1.3
     *     = 2.0
     *     < 3
     */
    struct Comparison {

        ComparisonOperator op;

        std::string version;
    };

    /*
     * The constraint is "any version".
     */
    static VersionConstraint any()
    {
        VersionConstraint constraint;

        constraint.kind = Kind::Any;

        return constraint;
    }

    /*
     * Create a single comparison constraint.
     */
    static VersionConstraint comparison(
        ComparisonOperator op,
        std::string version
    )
    {
        VersionConstraint constraint;

        constraint.kind = Kind::Comparison;
        constraint.comparison_value = Comparison{
            op,
            std::move(version)
        };

        return constraint;
    }

    /*
     * Logical conjunction.
     *
     * Every constraint must be satisfied.
     */
    static VersionConstraint all(
        std::vector<VersionConstraint> constraints
    )
    {
        VersionConstraint constraint;

        constraint.kind = Kind::And;
        constraint.constraints = std::move(constraints);

        return constraint;
    }

    /*
     * Logical disjunction.
     *
     * At least one constraint must be satisfied.
     */
    static VersionConstraint any_of(
        std::vector<VersionConstraint> constraints
    )
    {
        VersionConstraint constraint;

        constraint.kind = Kind::Or;
        constraint.constraints = std::move(constraints);

        return constraint;
    }

    /*
     * Logical negation.
     */
    static VersionConstraint not_(
        VersionConstraint constraint)
    {
        VersionConstraint result;

        result.kind = Kind::Not;
        result.constraint =
            std::make_shared<const VersionConstraint>(
                std::move(constraint)
            );

        return result;
    }

    Kind kind = Kind::Any;

    Comparison comparison_value{
        ComparisonOperator::Equal,
        ""
    };

    std::vector<VersionConstraint> constraints;

    std::shared_ptr<const VersionConstraint> constraint;
};

bool satisfies(
    const VersionConstraint& constraint,
    const std::string& version
);

} // namespace athena::dependency
