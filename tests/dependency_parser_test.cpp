// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/dependency/parser.hpp"
#include "../src/dependency/constraint.hpp"

#include <cassert>
#include <iostream>

using namespace athena::dependency;

namespace {

const PackageRef* get_package_ref(
    const ExpressionPtr& expression
)
{
    return dynamic_cast<const PackageRef*>(
        expression.get()
    );
}

const VersionComparison* get_version_comparison(
    const ExpressionPtr& expression
)
{
    return dynamic_cast<const VersionComparison*>(
        expression.get()
    );
}

const AndExpression* get_and_expression(
    const ExpressionPtr& expression
)
{
    return dynamic_cast<const AndExpression*>(
        expression.get()
    );
}

const OrExpression* get_or_expression(
    const ExpressionPtr& expression
)
{
    return dynamic_cast<const OrExpression*>(
        expression.get()
    );
}

const NotExpression* get_not_expression(
    const ExpressionPtr& expression
)
{
    return dynamic_cast<const NotExpression*>(
        expression.get()
    );
}

}

int main()
{
    {
        const auto dependency =
            parse_dependency("zlib");

        assert(dependency.kind == DependencyKind::Runtime);
        assert(dependency.context == DependencyContext::Target);
        assert(dependency.expression != nullptr);
        assert(
            dependency.expression->kind ==
            Expression::Kind::Package
        );

        const auto* package =
            get_package_ref(dependency.expression);

        assert(package != nullptr);
        assert(package->name == "zlib");
    }

    {
        const auto dependency =
            parse_dependency("zlib >= 1.2");

        assert(
            dependency.expression->kind ==
            Expression::Kind::VersionComparison
        );

        const auto* comparison =
            get_version_comparison(
                dependency.expression
            );

        assert(comparison != nullptr);
        assert(comparison->target != nullptr);
        assert(comparison->constraint != nullptr);

        const auto* package =
            get_package_ref(comparison->target);

        assert(package != nullptr);
        assert(package->name == "zlib");

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
            "1.2"
        );
    }

    {
        const auto dependency =
            parse_dependency("openssl < 4.0");

        const auto* comparison =
            get_version_comparison(
                dependency.expression
            );

        assert(comparison != nullptr);

        assert(
            comparison->constraint->comparison_value.op ==
            ComparisonOperator::Less
        );

        assert(
            comparison->constraint->comparison_value.version ==
            "4.0"
        );
    }

    {
        const auto dependency =
            parse_dependency("cmake != 3.30");

        const auto* comparison =
            get_version_comparison(
                dependency.expression
            );

        assert(comparison != nullptr);

        assert(
            comparison->constraint->comparison_value.op ==
            ComparisonOperator::NotEqual
        );

        assert(
            comparison->constraint->comparison_value.version ==
            "3.30"
        );
    }

    {
        const auto dependency =
            parse_dependency(
                "zlib >= 1.2 AND openssl >= 3.0"
            );

        const auto* and_expression =
            get_and_expression(
                dependency.expression
            );

        assert(and_expression != nullptr);
        assert(and_expression->expressions.size() == 2);

        const auto* zlib =
            get_version_comparison(
                and_expression->expressions[0]
            );

        const auto* openssl =
            get_version_comparison(
                and_expression->expressions[1]
            );

        assert(zlib != nullptr);
        assert(openssl != nullptr);

        const auto* zlib_package =
            get_package_ref(zlib->target);

        const auto* openssl_package =
            get_package_ref(openssl->target);

        assert(zlib_package != nullptr);
        assert(openssl_package != nullptr);

        assert(zlib_package->name == "zlib");
        assert(openssl_package->name == "openssl");
    }

    {
        const auto dependency =
            parse_dependency(
                "zlib >= 1.2 OR libressl >= 3.5"
            );

        const auto* or_expression =
            get_or_expression(
                dependency.expression
            );

        assert(or_expression != nullptr);
        assert(or_expression->expressions.size() == 2);

        const auto* zlib =
            get_version_comparison(
                or_expression->expressions[0]
            );

        const auto* libressl =
            get_version_comparison(
                or_expression->expressions[1]
            );

        assert(zlib != nullptr);
        assert(libressl != nullptr);

        const auto* zlib_package =
            get_package_ref(zlib->target);

        const auto* libressl_package =
            get_package_ref(libressl->target);

        assert(zlib_package != nullptr);
        assert(libressl_package != nullptr);

        assert(zlib_package->name == "zlib");
        assert(libressl_package->name == "libressl");
    }

    {
        const auto dependency =
            parse_dependency("NOT zlib");

        const auto* not_expression =
            get_not_expression(
                dependency.expression
            );

        assert(not_expression != nullptr);
        assert(not_expression->expression != nullptr);

        const auto* package =
            get_package_ref(
                not_expression->expression
            );

        assert(package != nullptr);
        assert(package->name == "zlib");
    }

    {
        const auto dependency =
            parse_dependency(
                "(zlib >= 1.2) AND (openssl >= 3.0)"
            );

        const auto* and_expression =
            get_and_expression(
                dependency.expression
            );

        assert(and_expression != nullptr);
        assert(and_expression->expressions.size() == 2);

        const auto* zlib =
            get_version_comparison(
                and_expression->expressions[0]
            );

        const auto* openssl =
            get_version_comparison(
                and_expression->expressions[1]
            );

        assert(zlib != nullptr);
        assert(openssl != nullptr);

        const auto* zlib_package =
            get_package_ref(zlib->target);

        const auto* openssl_package =
            get_package_ref(openssl->target);

        assert(zlib_package != nullptr);
        assert(openssl_package != nullptr);

        assert(zlib_package->name == "zlib");
        assert(openssl_package->name == "openssl");
    }

    {
        const auto dependency =
            parse_dependency(
                "zlib OR openssl AND curl"
            );

        const auto* or_expression =
            get_or_expression(
                dependency.expression
            );

        assert(or_expression != nullptr);
        assert(or_expression->expressions.size() == 2);

        const auto* zlib =
            get_package_ref(
                or_expression->expressions[0]
            );

        assert(zlib != nullptr);
        assert(zlib->name == "zlib");

        const auto* and_expression =
            get_and_expression(
                or_expression->expressions[1]
            );

        assert(and_expression != nullptr);
        assert(and_expression->expressions.size() == 2);

        const auto* openssl =
            get_package_ref(
                and_expression->expressions[0]
            );

        const auto* curl =
            get_package_ref(
                and_expression->expressions[1]
            );

        assert(openssl != nullptr);
        assert(curl != nullptr);

        assert(openssl->name == "openssl");
        assert(curl->name == "curl");
    }

    {
        bool failed = false;

        try {
            parse_dependency("zlib >");
        }
        catch (const std::runtime_error&) {
            failed = true;
        }

        assert(failed);
    }

    {
        bool failed = false;

        try {
            parse_dependency("");
        }
        catch (const std::runtime_error&) {
            failed = true;
        }

        assert(failed);
    }

    {
        bool failed = false;

        try {
            parse_dependency("(zlib >= 1.2");
        }
        catch (const std::runtime_error&) {
            failed = true;
        }

        assert(failed);
    }

    std::cout
        << "Dependency parser tests passed.\n";

    return 0;
}
