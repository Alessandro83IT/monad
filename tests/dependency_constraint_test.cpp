// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/dependency/constraint.hpp"

#include <cassert>
#include <iostream>

using namespace athena::dependency;

int main()
{
    /*
     * Any version.
     */
    const auto any =
        VersionConstraint::any();

    assert(any.kind == VersionConstraint::Kind::Any);

    assert(satisfies(any, "1.0"));
    assert(satisfies(any, "1.3"));
    assert(satisfies(any, "99.0"));

    /*
     * Equality: = 1.3
     */
    const auto equal =
        VersionConstraint::comparison(
            ComparisonOperator::Equal,
            "1.3"
        );

    assert(satisfies(equal, "1.3"));
    assert(satisfies(equal, "1.3.0"));
    assert(!satisfies(equal, "1.2"));
    assert(!satisfies(equal, "1.4"));

    /*
     * Inequality: != 1.3
     */
    const auto not_equal =
        VersionConstraint::comparison(
            ComparisonOperator::NotEqual,
            "1.3"
        );

    assert(!satisfies(not_equal, "1.3"));
    assert(!satisfies(not_equal, "1.3.0"));
    assert(satisfies(not_equal, "1.2"));
    assert(satisfies(not_equal, "1.4"));

    /*
     * Less: < 1.3
     */
    const auto less =
        VersionConstraint::comparison(
            ComparisonOperator::Less,
            "1.3"
        );

    assert(satisfies(less, "1.2"));
    assert(!satisfies(less, "1.3"));
    assert(!satisfies(less, "1.4"));

    /*
     * Less or equal: <= 1.3
     */
    const auto less_equal =
        VersionConstraint::comparison(
            ComparisonOperator::LessEqual,
            "1.3"
        );

    assert(satisfies(less_equal, "1.2"));
    assert(satisfies(less_equal, "1.3"));
    assert(satisfies(less_equal, "1.3.0"));
    assert(!satisfies(less_equal, "1.4"));

    /*
     * Greater: > 1.3
     */
    const auto greater =
        VersionConstraint::comparison(
            ComparisonOperator::Greater,
            "1.3"
        );

    assert(!satisfies(greater, "1.2"));
    assert(!satisfies(greater, "1.3"));
    assert(satisfies(greater, "1.4"));

    /*
     * Greater or equal: >= 1.3
     */
    const auto greater_equal =
        VersionConstraint::comparison(
            ComparisonOperator::GreaterEqual,
            "1.3"
        );

    assert(!satisfies(greater_equal, "1.2"));
    assert(satisfies(greater_equal, "1.3"));
    assert(satisfies(greater_equal, "1.3.0"));
    assert(satisfies(greater_equal, "1.4"));

    /*
     * Range: >= 1.3 AND < 2
     */
    const auto range =
        VersionConstraint::all({
            greater_equal,
            VersionConstraint::comparison(
                ComparisonOperator::Less,
                "2"
            )
        });

    assert(satisfies(range, "1.3"));
    assert(satisfies(range, "1.4"));
    assert(satisfies(range, "1.9"));
    assert(!satisfies(range, "1.2"));
    assert(!satisfies(range, "2.0"));
    assert(!satisfies(range, "2.1"));

    /*
     * Alternatives: = 1.3 OR = 1.4
     */
    const auto alternatives =
        VersionConstraint::any_of({
            VersionConstraint::comparison(
                ComparisonOperator::Equal,
                "1.3"
            ),
            VersionConstraint::comparison(
                ComparisonOperator::Equal,
                "1.4"
            )
        });

    assert(satisfies(alternatives, "1.3"));
    assert(satisfies(alternatives, "1.4"));
    assert(!satisfies(alternatives, "1.2"));
    assert(!satisfies(alternatives, "1.5"));

    /*
     * NOT (= 1.3)
     */
    const auto negated =
        VersionConstraint::not_(
            VersionConstraint::comparison(
                ComparisonOperator::Equal,
                "1.3"
            )
        );

    assert(satisfies(negated, "1.2"));
    assert(!satisfies(negated, "1.3"));
    assert(satisfies(negated, "1.4"));

    /*
     * Structural checks.
     */
    assert(any.kind == VersionConstraint::Kind::Any);

    assert(
        greater_equal.kind ==
        VersionConstraint::Kind::Comparison
    );

    assert(
        greater_equal.comparison_value.op ==
        ComparisonOperator::GreaterEqual
    );

    assert(
        greater_equal.comparison_value.version ==
        "1.3"
    );

    assert(
        range.kind ==
        VersionConstraint::Kind::And
    );

    assert(range.constraints.size() == 2);

    assert(
        alternatives.kind ==
        VersionConstraint::Kind::Or
    );

    assert(alternatives.constraints.size() == 2);

    assert(
        negated.kind ==
        VersionConstraint::Kind::Not
    );

    assert(negated.constraint != nullptr);

    assert(
        negated.constraint->kind ==
        VersionConstraint::Kind::Comparison
    );

    std::cout
        << "Dependency constraint tests passed.\n";

    return 0;
}
