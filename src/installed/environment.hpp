// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "../generation/generation.hpp"
#include "../dependency/expression.hpp"
#include <stdexcept>

namespace athena::installed {
class ValidationError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
// Pure offline validation. Does not read repositories, payloads, or legacy stores.
// Legacy memberships must first be explicitly captured from available metadata.
void validate_environment(const generation::Generation&,
                          const std::vector<SemanticState>& planned_members = {});
// Checks AST support structurally, including inactive/short-circuited branches.
// Throws for unknown types/kinds/scopes, never returns false for "unsupported".
void validate_expression(const dependency::ExpressionPtr&, bool legacy = false, bool condition = false);
// Read-only compatibility capture; no repository I/O and no invented old data.
void capture_legacy_semantics(generation::Generation&);
// Materialize inherited profile configuration before removing its original
// declaring member, and on successful new memberships before persistence.
void inherit_profile_features(generation::Generation&, const generation::Generation* previous = nullptr);
// Returns preserved memberships and resulting policies after validating the
// complete target with planned semantic values. Never fabricates an ArtifactId.
generation::Generation prepare_install(const generation::Generation&, const core::InstallPlan&);
// Parser-only overload for D8's existing legacy document bound.
void capture_legacy_semantics(generation::Generation&, std::size_t max_document_bytes);

}
