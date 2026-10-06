// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "expression.hpp"
#include <algorithm>
namespace athena::dependency {
// Check effective roles at leaves, after traversing structural nodes and
// applying scopes. An inactive inherited role must not hide an active nested
// scope. Active structural nodes retain their existing boolean semantics;
// explicit disabling is sticky for the entire enclosing subtree.
inline bool expression_role_active(const ExpressionPtr& expression, DependencyKind role,
                            bool enabled, const DependencyResolutionContext& policy,
                            bool gate_inherited_role = true)
{
    if (!expression || !enabled) return false;
    switch (expression->kind) {
        case Expression::Kind::Scoped: {
            const auto* scoped = dynamic_cast<const ScopedExpression*>(expression.get());
            return scoped && expression_role_active(scoped->body, scoped->kind_role,
                                                     scoped->enabled, policy);
        }
        case Expression::Kind::And: {
            const auto* e = dynamic_cast<const AndExpression*>(expression.get());
            return e && ((!gate_inherited_role || policy.activates(role))
                || std::any_of(e->expressions.begin(), e->expressions.end(),
                [&](const auto& child) { return expression_role_active(child, role, enabled, policy, gate_inherited_role); }));
        }
        case Expression::Kind::Or: {
            const auto* e = dynamic_cast<const OrExpression*>(expression.get());
            return e && ((!gate_inherited_role || policy.activates(role))
                || std::any_of(e->expressions.begin(), e->expressions.end(),
                [&](const auto& child) { return expression_role_active(child, role, enabled, policy, gate_inherited_role); }));
        }
        case Expression::Kind::Not: {
            const auto* e = dynamic_cast<const NotExpression*>(expression.get());
            return e && ((!gate_inherited_role || policy.activates(role))
                || expression_role_active(e->expression, role, enabled, policy, gate_inherited_role));
        }
        case Expression::Kind::Conditional: {
            const auto* e = dynamic_cast<const ConditionalExpression*>(expression.get());
            return e && ((!gate_inherited_role || policy.activates(role))
                || expression_role_active(e->body, role, enabled, policy, gate_inherited_role));
        }
        default: return !gate_inherited_role || policy.activates(role);
    }
}

}
