// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "expression_validation.hpp"
#include "constraint.hpp"
#include <stdexcept>
namespace athena::dependency {
namespace dep = athena::dependency;
namespace {
using K = Expression::Kind;
[[noreturn]] void unsupported() { throw std::runtime_error("Unsupported installed semantic expression"); }
template<class T> const T& node(const ExpressionPtr& p) {
    const auto* n = dynamic_cast<const T*>(p.get());
    if (!n) unsupported();
    return *n;
}
void valid_constraint(const dep::VersionConstraint& c) {
    using V = dep::VersionConstraint;
    switch (c.kind) {
    case V::Kind::Any: return;
    case V::Kind::Comparison:
        if (c.comparison_value.version.empty() || static_cast<unsigned>(c.comparison_value.op) >
            static_cast<unsigned>(dep::ComparisonOperator::GreaterEqual)) unsupported();
        return;
    case V::Kind::And:
    case V::Kind::Or:
        if (c.constraints.empty()) unsupported();
        for (const auto& child : c.constraints) valid_constraint(child);
        return;
    case V::Kind::Not:
        if (!c.constraint) unsupported();
        valid_constraint(*c.constraint); return;
    }
    unsupported();
}
}
void validate_scope(dep::DependencyKind role, dep::DependencyContext context) {
    if (static_cast<unsigned>(role) > static_cast<unsigned>(dep::DependencyKind::Propagated) ||
        static_cast<unsigned>(context) > static_cast<unsigned>(dep::DependencyContext::Target)) unsupported();
}
void validate_expression(const ExpressionPtr& e, ExpressionSupport support, bool condition) {
    const bool legacy = support != ExpressionSupport::Complete;
    if (!e) unsupported();
    switch(e->kind) {
    case K::True: (void)node<dep::TrueExpression>(e); return;
    case K::False: (void)node<dep::FalseExpression>(e); return;
    case K::Package:
        if (condition || node<dep::PackageRef>(e).name.empty()) unsupported(); return;
    case K::Capability:
        if (support == ExpressionSupport::PackageOnly || condition || node<dep::CapabilityRef>(e).name.empty()) unsupported(); return;
    case K::Feature:
        if (legacy || node<dep::FeatureRef>(e).name.empty()) unsupported(); return;
    case K::Platform:
        if (legacy || node<dep::PlatformRef>(e).name.empty()) unsupported(); return;
    case K::Architecture:
        if (legacy || node<dep::ArchitectureRef>(e).name.empty()) unsupported(); return;
    case K::ABI:
        if (legacy || node<dep::ABIRef>(e).name.empty()) unsupported(); return;
    case K::VersionComparison: {
        if (condition) unsupported();
        const auto& v = node<dep::VersionComparison>(e);
        if (!v.target || !v.constraint || (v.target->kind != K::Package && v.target->kind != K::Capability && v.target->kind != K::Feature)) unsupported();
        validate_expression(v.target, support); valid_constraint(*v.constraint); return;
    }
    case K::And:
        for (const auto& child : node<dep::AndExpression>(e).expressions) validate_expression(child, support, condition); return;
    case K::Or:
        for (const auto& child : node<dep::OrExpression>(e).expressions) validate_expression(child, support, condition); return;
    case K::Not: validate_expression(node<dep::NotExpression>(e).expression, support, condition); return;
    case K::Scoped: {
        if (legacy) unsupported();
        const auto& s = node<dep::ScopedExpression>(e); validate_scope(s.kind_role, s.context);
        validate_expression(s.body, support, condition); return;
    }
    case K::Conditional: {
        if (legacy || condition) unsupported();
        const auto& c = node<dep::ConditionalExpression>(e);
        validate_expression(c.condition, ExpressionSupport::Complete, true); validate_expression(c.body); return;
    }
    }
    unsupported();
}
}
