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
        case Expression::Kind::True:
        case Expression::Kind::False:
        case Expression::Kind::Conditional:
        case Expression::Kind::Scoped:
            return Precedence::Atom;
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
        case VersionConstraint::Kind::Or: {
            const char* op = constraint.kind == VersionConstraint::Kind::And ? " AND " : " OR ";
            std::string result;
            for (std::size_t i = 0; i < constraint.constraints.size(); ++i) {
                if (i) result += op;
                const auto& child = constraint.constraints[i];
                const bool parens = child.kind == VersionConstraint::Kind::Or && constraint.kind == VersionConstraint::Kind::And;
                if (parens) result += "(";
                result += serialize_constraint(child);
                if (parens) result += ")";
            }
            return result;
        }
        case VersionConstraint::Kind::Not:
            if (!constraint.constraint) throw std::runtime_error("NOT version constraint senza contenuto");
            return "NOT (" + serialize_constraint(*constraint.constraint) + ")";
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

        case Expression::Kind::Capability:
        case Expression::Kind::Feature:
        case Expression::Kind::Platform:
        case Expression::Kind::Architecture:
        case Expression::Kind::ABI: {
            const char* function = "";
            std::string name;
            if (expression->kind == Expression::Kind::Capability) { function = "capability"; const auto* p = dynamic_cast<const CapabilityRef*>(expression.get()); if (!p) throw std::runtime_error("Invalid capability expression"); name = p->name; }
            else if (expression->kind == Expression::Kind::Feature) { function = "feature"; const auto* p = dynamic_cast<const FeatureRef*>(expression.get()); if (!p) throw std::runtime_error("Invalid feature expression"); name = p->name; }
            else if (expression->kind == Expression::Kind::Platform) { function = "platform"; const auto* p = dynamic_cast<const PlatformRef*>(expression.get()); if (!p) throw std::runtime_error("Invalid platform expression"); name = p->name; }
            else if (expression->kind == Expression::Kind::Architecture) { function = "architecture"; const auto* p = dynamic_cast<const ArchitectureRef*>(expression.get()); if (!p) throw std::runtime_error("Invalid architecture expression"); name = p->name; }
            else { function = "abi"; const auto* p = dynamic_cast<const ABIRef*>(expression.get()); if (!p) throw std::runtime_error("Invalid ABI expression"); name = p->name; }
            result = std::string(function) + "(" + name + ")";
            break;
        }

        case Expression::Kind::True: result = "true"; break;
        case Expression::Kind::False: result = "false"; break;

        case Expression::Kind::Scoped: {
            const auto* scoped = dynamic_cast<const ScopedExpression*>(expression.get());
            if (!scoped || !scoped->body) throw std::runtime_error("Invalid scoped expression");
            const char* role = "runtime";
            switch (scoped->kind_role) {
                case DependencyKind::Runtime: role = "runtime"; break;
                case DependencyKind::Build: role = "build"; break;
                case DependencyKind::Test: role = "test"; break;
                case DependencyKind::Optional: role = scoped->enabled ? "optional" : "optional-disabled"; break;
                case DependencyKind::Development: role = "development"; break;
                case DependencyKind::Propagated: role = "propagated"; break;
            }
            const char* context = scoped->context == DependencyContext::Host ? "host" : scoped->context == DependencyContext::Build ? "build" : "target";
            result = std::string(role) + "(" + context + "): (" + serialize_impl(scoped->body, Precedence::Or) + ")";
            break;
        }

        case Expression::Kind::Conditional: {
            const auto* conditional = dynamic_cast<const ConditionalExpression*>(expression.get());
            if (!conditional || !conditional->condition || !conditional->body) throw std::runtime_error("Invalid conditional expression");
            result = "when(" + serialize_impl(conditional->condition, Precedence::Or) + ", " + serialize_impl(conditional->body, Precedence::Or) + ")";
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
                if (comparison->constraint->kind == VersionConstraint::Kind::Comparison)
                    result += " " + constraint;
                else result += " {" + constraint + "}";
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
    if (dependency.kind == DependencyKind::Runtime &&
        dependency.context == DependencyContext::Target && dependency.enabled)
        return serialize_expression(dependency.expression);
    const char* role = "runtime";
    switch (dependency.kind) {
        case DependencyKind::Runtime: role = "runtime"; break;
        case DependencyKind::Build: role = "build"; break;
        case DependencyKind::Test: role = "test"; break;
        case DependencyKind::Optional: role = "optional"; break;
        case DependencyKind::Development: role = "development"; break;
        case DependencyKind::Propagated: role = "propagated"; break;
    }
    if (dependency.kind == DependencyKind::Optional && !dependency.enabled)
        role = "optional-disabled";
    const char* context = "target";
    switch (dependency.context) {
        case DependencyContext::Host: context = "host"; break;
        case DependencyContext::Build: context = "build"; break;
        case DependencyContext::Target: context = "target"; break;
    }
    return std::string(role) + "(" + context + "):" + serialize_expression(dependency.expression);
}

}
