#include "serializer.hpp"
#include "constraint.hpp"

#include <stdexcept>
#include <string>

namespace athena::dependency {

namespace {

enum class Precedence {
    Or = 1,
    And = 2,
    Not = 3,
    Atom = 4
};

Precedence precedence(const Expression& expression)
{
    switch (expression.kind) {
        case Expression::Kind::Or:
            return Precedence::Or;

        case Expression::Kind::And:
            return Precedence::And;

        case Expression::Kind::Not:
            return Precedence::Not;

        case Expression::Kind::Package:
        case Expression::Kind::Capability:
        case Expression::Kind::Feature:
        case Expression::Kind::Platform:
        case Expression::Kind::Architecture:
        case Expression::Kind::ABI:
        case Expression::Kind::VersionComparison:
            return Precedence::Atom;

        case Expression::Kind::True:
        case Expression::Kind::False:
        case Expression::Kind::Conditional:
            throw std::runtime_error(
                "Dependency expression kind is not supported by the "
                "current textual serializer."
            );
    }

    throw std::runtime_error(
        "Unknown dependency expression kind."
    );
}

bool needs_parentheses(
    const Expression& child,
    Precedence parent_precedence
)
{
    return precedence(child) < parent_precedence;
}

std::string serialize_constraint(
    const VersionConstraint& constraint
)
{
    switch (constraint.kind) {
        case VersionConstraint::Kind::Any:
            return "";

        case VersionConstraint::Kind::Comparison:
            switch (constraint.comparison_value.op) {
                case ComparisonOperator::Equal:
                    return "= " +
                        constraint.comparison_value.version;

                case ComparisonOperator::NotEqual:
                    return "!= " +
                        constraint.comparison_value.version;

                case ComparisonOperator::Less:
                    return "< " +
                        constraint.comparison_value.version;

                case ComparisonOperator::LessEqual:
                    return "<= " +
                        constraint.comparison_value.version;

                case ComparisonOperator::Greater:
                    return "> " +
                        constraint.comparison_value.version;

                case ComparisonOperator::GreaterEqual:
                    return ">= " +
                        constraint.comparison_value.version;
            }

            break;

        case VersionConstraint::Kind::And:
        case VersionConstraint::Kind::Or:
        case VersionConstraint::Kind::Not:
            throw std::runtime_error(
                "Compound version constraints are not supported "
                "by the current dependency parser."
            );
    }

    throw std::runtime_error(
        "Unknown version constraint kind."
    );
}

std::string serialize_impl(
    const ExpressionPtr& expression,
    Precedence parent_precedence
)
{
    if (!expression) {
        throw std::runtime_error(
            "Cannot serialize a null dependency expression."
        );
    }

    std::string result;

    switch (expression->kind) {
        case Expression::Kind::Package: {
            const auto* package =
                dynamic_cast<const PackageRef*>(
                    expression.get()
                );

            if (!package) {
                throw std::runtime_error(
                    "Invalid package dependency expression."
                );
            }

            result = package->name;
            break;
        }

        case Expression::Kind::VersionComparison: {
            const auto* comparison =
                dynamic_cast<const VersionComparison*>(
                    expression.get()
                );

            if (!comparison || !comparison->target) {
                throw std::runtime_error(
                    "Invalid version comparison expression."
                );
            }

            if (!comparison->constraint) {
                throw std::runtime_error(
                    "Version comparison has no constraint."
                );
            }

            result = serialize_impl(
                comparison->target,
                Precedence::Atom
            );

            const auto constraint =
                serialize_constraint(
                    *comparison->constraint
                );

            if (!constraint.empty()) {
                result += " " + constraint;
            }

            break;
        }

        case Expression::Kind::And: {
            const auto* and_expression =
                dynamic_cast<const AndExpression*>(
                    expression.get()
                );

            if (!and_expression) {
                throw std::runtime_error(
                    "Invalid AND dependency expression."
                );
            }

            for (std::size_t i = 0;
                 i < and_expression->expressions.size();
                 ++i) {

                if (i != 0) {
                    result += " AND ";
                }

                result += serialize_impl(
                    and_expression->expressions[i],
                    Precedence::And
                );
            }

            break;
        }

        case Expression::Kind::Or: {
            const auto* or_expression =
                dynamic_cast<const OrExpression*>(
                    expression.get()
                );

            if (!or_expression) {
                throw std::runtime_error(
                    "Invalid OR dependency expression."
                );
            }

            for (std::size_t i = 0;
                 i < or_expression->expressions.size();
                 ++i) {

                if (i != 0) {
                    result += " OR ";
                }

                result += serialize_impl(
                    or_expression->expressions[i],
                    Precedence::Or
                );
            }

            break;
        }

        case Expression::Kind::Not: {
            const auto* not_expression =
                dynamic_cast<const NotExpression*>(
                    expression.get()
                );

            if (!not_expression || !not_expression->expression) {
                throw std::runtime_error(
                    "Invalid NOT dependency expression."
                );
            }

            result = "NOT " +
                serialize_impl(
                    not_expression->expression,
                    Precedence::Not
                );

            break;
        }

        case Expression::Kind::True:
        case Expression::Kind::False:
        case Expression::Kind::Capability:
        case Expression::Kind::Feature:
        case Expression::Kind::Platform:
        case Expression::Kind::Architecture:
        case Expression::Kind::ABI:
        case Expression::Kind::Conditional:
            throw std::runtime_error(
                "This dependency expression kind is not supported "
                "by the current textual serializer."
            );
    }

    if (precedence(*expression) < parent_precedence) {
        return "(" + result + ")";
    }

    return result;
}

}

std::string serialize_expression(
    const ExpressionPtr& expression
)
{
    return serialize_impl(
        expression,
        Precedence::Or
    );
}

std::string serialize_dependency(
    const Dependency& dependency
)
{
    return serialize_expression(dependency.expression);
}

}
