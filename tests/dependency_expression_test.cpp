// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/dependency/expression.hpp"
#include "../src/dependency/constraint.hpp"

#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

using namespace athena::dependency;

int main()
{
    auto zlib =
        std::make_shared<const PackageRef>("zlib");

    auto openssl =
        std::make_shared<const PackageRef>("openssl");

    auto zlib_constraint =
        std::make_shared<const VersionConstraint>(
            VersionConstraint::comparison(
                ComparisonOperator::GreaterEqual,
                "1.3"
            )
        );

    auto zlib_version =
        std::make_shared<const VersionComparison>(
            zlib,
            zlib_constraint
        );

    auto expression =
        std::make_shared<const AndExpression>(
            std::vector<ExpressionPtr>{
                zlib_version,
                openssl
            }
        );

    Dependency dependency{
        expression,
        DependencyKind::Runtime,
        DependencyContext::Target
    };

    assert(
        dependency.kind ==
        DependencyKind::Runtime
    );

    assert(
        dependency.context ==
        DependencyContext::Target
    );

    assert(
        dependency.expression->kind ==
        Expression::Kind::And
    );

    const auto* and_expression =
        dynamic_cast<const AndExpression*>(
            dependency.expression.get()
        );

    assert(and_expression != nullptr);
    assert(and_expression->expressions.size() == 2);

    const auto* comparison =
        dynamic_cast<const VersionComparison*>(
            and_expression->expressions[0].get()
        );

    assert(comparison != nullptr);

    assert(comparison->constraint != nullptr);

    assert(
        comparison->constraint->kind ==
        VersionConstraint::Kind::Comparison
    );

    assert(
        comparison->constraint->comparison_value.op ==
        ComparisonOperator::GreaterEqual
    );

    assert(
        comparison->constraint->comparison_value.version ==
        "1.3"
    );

    const auto* package =
        dynamic_cast<const PackageRef*>(
            comparison->target.get()
        );

    assert(package != nullptr);
    assert(package->name == "zlib");

    const auto* openssl_ref =
        dynamic_cast<const PackageRef*>(
            and_expression->expressions[1].get()
        );

    assert(openssl_ref != nullptr);
    assert(openssl_ref->name == "openssl");

    std::cout
        << "Dependency expression tests passed.\n";

    return 0;
}
