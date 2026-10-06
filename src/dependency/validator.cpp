// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "validator.hpp"
#include "constraint.hpp"
#include "expression_validation.hpp"

#include <stdexcept>
#include <string>

namespace athena::dependency {
DependencyValidator::DependencyValidator(
    const InstalledEnvironment& environment
)
    : environment_(environment)
{
}

bool DependencyValidator::satisfies(
    const ExpressionPtr& expression
) const
{
    // This compatibility validator has only legacy package state. Structural
    // rejection precedes evaluation, including NOT and short-circuit branches.
    validate_expression(expression, ExpressionSupport::PackageOnly);
    return satisfies_expression(expression);
}

bool DependencyValidator::satisfies_expression(
    const ExpressionPtr& expression
) const
{
    if (!expression) {
        return false;
    }

    switch (expression->kind) {

        case Expression::Kind::True:
            return true;

        case Expression::Kind::False:
            return false;

        case Expression::Kind::Package: {

            const auto* package_expression =
                dynamic_cast<const PackageRef*>(
                    expression.get()
                );

            if (package_expression == nullptr) {
                return false;
            }

            return environment_.contains(
                package_expression->name
            );
        }

        case Expression::Kind::VersionComparison: {

            const auto* comparison =
                dynamic_cast<const VersionComparison*>(
                    expression.get()
                );

            if (comparison == nullptr ||
                !comparison->target ||
                !comparison->constraint) {

                return false;
            }

            /*
             * Version comparisons currently operate on package
             * references. More complex target expressions can be
             * introduced later when capabilities acquire concrete
             * environment semantics.
             */
            const auto* package_expression =
                dynamic_cast<const PackageRef*>(
                    comparison->target.get()
                );

            if (package_expression == nullptr) {
                return false;
            }

            const auto* package =
                environment_.find(
                    package_expression->name
                );

            if (package == nullptr) {
                return false;
            }

            return ::athena::dependency::satisfies(
                *comparison->constraint,
                package->version
            );
        }

        case Expression::Kind::And: {

            const auto* and_expression =
                dynamic_cast<const AndExpression*>(
                    expression.get()
                );

            if (and_expression == nullptr) {
                return false;
            }

            for (const auto& child :
                 and_expression->expressions) {

                if (!satisfies_expression(child)) {
                    return false;
                }
            }

            return true;
        }

        case Expression::Kind::Or: {

            const auto* or_expression =
                dynamic_cast<const OrExpression*>(
                    expression.get()
                );

            if (or_expression == nullptr) {
                return false;
            }

            for (const auto& child :
                 or_expression->expressions) {

                if (satisfies_expression(child)) {
                    return true;
                }
            }

            return false;
        }

        case Expression::Kind::Not: {

            const auto* not_expression =
                dynamic_cast<const NotExpression*>(
                    expression.get()
                );

            if (not_expression == nullptr ||
                !not_expression->expression) {

                return false;
            }

            return !satisfies_expression(
                not_expression->expression
            );
        }

        /*
         * These expression kinds already exist in the semantic
         * model, but the installed environment does not yet expose
         * capability/feature/platform/ABI information. Returning
         * false here is unreachable for public entry points: structural
         * validation rejects these kinds before boolean evaluation.
         */
        case Expression::Kind::Capability:
        case Expression::Kind::Feature:
        case Expression::Kind::Platform:
        case Expression::Kind::Architecture:
        case Expression::Kind::ABI:
        case Expression::Kind::Conditional:
            return false;
    }

    return false;
}

void DependencyValidator::validate() const
{
    /*
     * Validate the complete candidate environment, not just one
     * package. This is essential for removal: removing one package
     * can break a dependency belonging to any other package.
     */
    for (const auto& package : environment_.packages()) {

        for (const auto& dependency : package.dependencies) {

            validate_expression(dependency.expression, ExpressionSupport::PackageOnly);
            if (!satisfies_expression(dependency.expression)) {

                throw std::runtime_error(
                    "Unsatisfied dependency for package '" +
                    package.name +
                    "': " +
                    "dependency expression is not satisfied"
                );
            }
        }
    }
}

} // namespace athena::dependency
