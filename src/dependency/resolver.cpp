// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "role_activity.hpp"
#include "semantic_assignment.hpp"
#include "resolver.hpp"
#include "environment.hpp"
#include "../repository/resolver_snapshot_adapter.hpp"

#include "constraint.hpp"
#include "serializer.hpp"
#include "../core/resolution_order.hpp"
#include "../repository/repository_index.hpp"
#include "../version/version.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace athena::dependency {

Environment native_environment()
{
#if defined(__linux__)
    constexpr const char* platform = "linux";
#elif defined(__FreeBSD__)
    constexpr const char* platform = "freebsd";
#elif defined(__OpenBSD__)
    constexpr const char* platform = "openbsd";
#elif defined(__NetBSD__)
    constexpr const char* platform = "netbsd";
#else
    constexpr const char* platform = "unknown";
#endif

#if defined(__x86_64__) || defined(_M_X64)
    constexpr const char* architecture = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    constexpr const char* architecture = "aarch64";
#elif defined(__i386__) || defined(_M_IX86)
    constexpr const char* architecture = "x86";
#elif defined(__arm__) || defined(_M_ARM)
    constexpr const char* architecture = "arm";
#else
    constexpr const char* architecture = "unknown";
#endif

#if defined(__GLIBC__)
    constexpr const char* abi = "glibc";
#else
    constexpr const char* abi = "";
#endif

    return Environment{
        platform,
        architecture,
        abi
    };
}

Environments native_environments()
{
    const auto environment = native_environment();

    return Environments{
        environment,
        environment,
        environment
    };
}

namespace {

using Package = athena::repository::ResolverCandidate;

struct DependencyTarget {
    enum class Kind { Package, Capability, Feature };
    Kind kind;
    std::string name;
    VersionConstraint constraint;
};

struct NegativeDependency {
    DependencyTarget target;
    bool provider_origin = false;
};
struct DeferredNegative {
    ExpressionPtr expression;
    Package owner;
    DependencyContext context;
    bool conflict_expression = false;
    DependencyKind role = DependencyKind::Runtime;
};

/*
 * Explicitly records which package version has been selected during
 * dependency resolution.
 *
 * The resolver previously tracked only the package name through the
 * `resolved` set. Keeping the selected Package value available allows
 * later dependency constraints to validate the existing assignment
 * instead of silently accepting it.
 */
/*
 * The map key is the logical package slot (PackageId/name), so compatible
 * requirements converge on one installed package. The value is the exact
 * identity-qualified alternative selected for that slot; repository and
 * snapshot identity are never used to merge alternatives.
 */
struct PackageAssignment { Package package; };

/*
 * Snapshot of the mutable resolution state.
 *
 * A global candidate decision owns one of these snapshots so the solver
 * can return exactly to the point immediately before that decision was
 * made.
 */
struct ResolutionSnapshot {
    std::vector<Package> result;
    std::vector<RequestOrigin> origins;
    athena::core::ResolutionResult public_result;

    std::unordered_map<
        std::string,
        PackageAssignment
    > assignments;

    std::unordered_set<std::string> resolved;
    std::unordered_set<std::string> resolving;
    std::vector<NegativeDependency> negative_dependencies;
    std::vector<DeferredNegative> deferred_negatives;

    /*
     * Semantic contexts accumulated for each selected package.
     *
     * A package may be required simultaneously by different dependency
     * contexts, so the value is a set-like vector rather than a single
     * context.
     */
    std::unordered_map<
        std::string,
        std::vector<DependencyContext>
    > contexts;
    std::unordered_map<std::string, Package> semantic_assignments;
    std::unordered_map<std::string, bool> feature_states;
};

/*
 * A global search decision.
 *
 * Each decision represents one package for which multiple compatible
 * candidates exist. The solver starts with candidate_index == 0 and
 * advances it after a downstream conflict.
 */
struct ResolutionDecision {
    std::string name;
    std::vector<Package> candidates;
    std::size_t candidate_index;
    ResolutionSnapshot snapshot;
};

struct ResolutionState {
    std::vector<Package> result;
    std::vector<RequestOrigin> origins;
    athena::core::ResolutionResult public_result;

    /*
     * Candidate assignments are keyed by package name. This is the
     * foundation for global constraint validation and future candidate
     * backtracking.
     */
    std::unordered_map<
        std::string,
        PackageAssignment
    > assignments;

    std::unordered_set<std::string> resolved;
    std::unordered_set<std::string> resolving;
    std::vector<NegativeDependency> negative_dependencies;
    std::vector<DeferredNegative> deferred_negatives;

    /*
     * Semantic dependency contexts associated with selected packages.
     */
    std::unordered_map<
        std::string,
        std::vector<DependencyContext>
    > contexts;
    std::unordered_map<std::string, Package> semantic_assignments;
    std::unordered_map<std::string, bool> feature_states;

    /*
     * Host/Build/Target environments are fixed for the whole resolution.
     * They are therefore intentionally not part of ResolutionSnapshot:
     * backtracking changes the selected package graph, not the environment
     * against which that graph is evaluated.
     */
    Environments environments;
    DependencyResolutionContext policy;
    bool use_policy = false;
    const std::map<athena::core::RepositoryId, athena::core::RepositorySelectionRule>* repository_rules = nullptr;

    /*
     * Global candidate decisions, in dependency-resolution order.
     * Unlike the old candidate history, each decision owns the exact
     * state required to retry that branch.
     */
    std::vector<ResolutionDecision> decisions;
};

bool package_satisfies_target(const Package&, const DependencyTarget&);

bool identity_precedes(const Package& left, const Package& right)
{
    if (left.identity.has_value() != right.identity.has_value())
        return !left.identity.has_value();
    if (!left.identity) return false;
    const auto& a = *left.identity;
    const auto& b = *right.identity;
    if (a.repository != b.repository) return a.repository < b.repository;
    if (a.snapshot.metadata_digest != b.snapshot.metadata_digest)
        return a.snapshot.metadata_digest < b.snapshot.metadata_digest;
    if (a.snapshot.snapshot_id != b.snapshot.snapshot_id)
        return a.snapshot.snapshot_id < b.snapshot.snapshot_id;
    return a.provenance.metadata_reference < b.provenance.metadata_reference;
}

/*
 * Find the newest available package version satisfying
 * the specified version constraint.
 */
std::optional<Package> find_package(
    const std::string& name,
    const VersionConstraint& constraint,
    const athena::repository::RepositoryIndex& repository
)
{
    std::optional<Package> best_match;

    /*
     * RepositoryIndex provides all versions of the requested package.
     * The resolver remains responsible for applying the version
     * constraint and selecting the newest compatible candidate.
     */
    for (const auto& package : repository.find_candidates(name)) {

        if (!satisfies(constraint, package.version)) {
            continue;
        }

        if (!best_match ||
            athena::version::greater(
                package.version,
                best_match->version
            )) {

            best_match = package;
        }
    }

    return best_match;
}

/*
 * Return every available candidate satisfying the requested
 * constraint, ordered from newest to oldest.
 *
 * Keeping the complete candidate set is what allows the resolver
 * to backtrack when the newest candidate creates a conflict later
 * in the dependency graph.
 */
std::vector<Package> find_dependency_candidates(
    const DependencyTarget& target,
    const athena::repository::RepositoryIndex& repository,
    const ResolutionState* state = nullptr
)
{
    std::vector<Package> candidates;

    switch (target.kind) {

        case DependencyTarget::Kind::Package:
            candidates = repository.find_candidates(target.name);
            break;

        case DependencyTarget::Kind::Capability:
            candidates = repository.find_capability_candidates(target.name);
            break;

        case DependencyTarget::Kind::Feature:
            candidates = repository.find_feature_candidates(target.name);
            break;
    }

    candidates.erase(
        std::remove_if(candidates.begin(), candidates.end(), [&target, state](const Package& package) {
            if (state && state->repository_rules && target.kind != DependencyTarget::Kind::Package) {
                const auto assigned = state->assignments.find(package.name);
                if (assigned != state->assignments.end() && assigned->second.package.identity != package.identity)
                    return true;
            }
            return !package_satisfies_target(package, target);
        }), candidates.end());

    /*
     * Semantic dependencies may have several providers. Select the
     * newest provider first, while keeping package-name ordering
     * deterministic when versions are equal.
     */
    std::sort(
        candidates.begin(),
        candidates.end(),
        [&target, state](const Package& left, const Package& right) {
            if (state && state->repository_rules) {
                const auto& rules = *state->repository_rules;
                const auto precedes = [&](const Package& a, const Package& b) {
                    return athena::core::candidate_precedes(*a.public_candidate, *a.snapshot, rules.at(a.identity->repository),
                                                           *b.public_candidate, *b.snapshot, rules.at(b.identity->repository));
                };
                if (precedes(left, right)) return true;
                if (precedes(right, left)) return false;
                return left.name < right.name;
            }

            const auto provided_version = [&target](const Package& package) -> const std::string& {
                if (target.kind == DependencyTarget::Kind::Capability) {
                    const auto found = package.provides.capability_versions.find(target.name);
                    if (found != package.provides.capability_versions.end()) return found->second;
                }
                return package.version;
            };
            const auto& left_version = provided_version(left);
            const auto& right_version = provided_version(right);

            if (left_version != right_version) {
                return athena::version::greater(
                    left_version,
                    right_version
                );
            }

            if (left.name != right.name) return left.name < right.name;
            return identity_precedes(left, right);
        }
    );

    return candidates;
}

/*
 * Capture the mutable part of the resolution state.
 *
 * Decision history is deliberately excluded: a snapshot represents the
 * state before a decision, while the decision itself belongs to the
 * solver's search stack.
 */
ResolutionSnapshot snapshot_resolution_state(
    const ResolutionState& state
)
{
    return {
        state.result,
        state.origins,
        state.public_result,
        state.assignments,
        state.resolved,
        {}, // Restart propagation from its entry point after restoring this branch.
        state.negative_dependencies,
        state.deferred_negatives,
        state.contexts,
        state.semantic_assignments,
        state.feature_states
    };
}

/*
 * Restore the mutable state associated with a global decision.
 */
void restore_resolution_state(
    ResolutionState& state,
    const ResolutionSnapshot& snapshot
)
{
    state.result = snapshot.result;
    state.origins = snapshot.origins;
    state.public_result = snapshot.public_result;
    state.assignments = snapshot.assignments;
    state.resolved = snapshot.resolved;
    state.resolving = snapshot.resolving;
    state.negative_dependencies =
        snapshot.negative_dependencies;
    state.deferred_negatives = snapshot.deferred_negatives;

    state.contexts =
        snapshot.contexts;
    state.semantic_assignments = snapshot.semantic_assignments;
    state.feature_states = snapshot.feature_states;
}

bool backtrack_resolution(ResolutionState& state)
{
    while (!state.decisions.empty()) {
        auto& decision = state.decisions.back();
        restore_resolution_state(state, decision.snapshot);
        ++decision.candidate_index;
        if (decision.candidate_index < decision.candidates.size()) return true;
        state.decisions.pop_back();
    }
    return false;
}

/*
 * Extract a package name and version constraint from a simple
 * dependency expression.
 *
 * Supported:
 *
 *     PackageRef
 *     VersionComparison(PackageRef, VersionConstraint)
 *
 * A plain PackageRef means Any version.
 */

DependencyTarget extract_dependency_target(
    const ExpressionPtr& expression
)
{
    if (!expression) {
        throw std::runtime_error(
            "Espressione di dipendenza nulla"
        );
    }

    if (expression->kind == Expression::Kind::Package) {

        const auto* package =
            dynamic_cast<const PackageRef*>(
                expression.get()
            );

        if (package == nullptr) {
            throw std::runtime_error(
                "Espressione Package non valida"
            );
        }

        return {
            DependencyTarget::Kind::Package,
            package->name,
            VersionConstraint::any()
        };
    }

    if (expression->kind == Expression::Kind::Capability) {

        const auto* capability =
            dynamic_cast<const CapabilityRef*>(
                expression.get()
            );

        if (capability == nullptr) {
            throw std::runtime_error(
                "Espressione Capability non valida"
            );
        }

        return {
            DependencyTarget::Kind::Capability,
            capability->name,
            VersionConstraint::any()
        };
    }

    if (expression->kind == Expression::Kind::Feature) {

        const auto* feature =
            dynamic_cast<const FeatureRef*>(
                expression.get()
            );

        if (feature == nullptr) {
            throw std::runtime_error(
                "Espressione Feature non valida"
            );
        }

        return {
            DependencyTarget::Kind::Feature,
            feature->name,
            VersionConstraint::any()
        };
    }

    if (expression->kind ==
        Expression::Kind::VersionComparison) {

        const auto* comparison =
            dynamic_cast<const VersionComparison*>(
                expression.get()
            );

        if (comparison == nullptr ||
            !comparison->target ||
            !comparison->constraint) {

            throw std::runtime_error(
                "Espressione di confronto versione non valida"
            );
        }

        if (comparison->target->kind != Expression::Kind::Package &&
            comparison->target->kind != Expression::Kind::Capability &&
            comparison->target->kind != Expression::Kind::Feature) {

            throw std::runtime_error(
                "Il confronto di versione deve riferirsi "
                "a un pacchetto"
            );
        }

        if (const auto* package = dynamic_cast<const PackageRef*>(comparison->target.get()))
            return {DependencyTarget::Kind::Package, package->name, *comparison->constraint};
        if (const auto* capability = dynamic_cast<const CapabilityRef*>(comparison->target.get()))
            return {DependencyTarget::Kind::Capability, capability->name, *comparison->constraint};
        if (const auto* feature = dynamic_cast<const FeatureRef*>(comparison->target.get()))
            return {DependencyTarget::Kind::Feature, feature->name, *comparison->constraint};
        throw std::runtime_error("Riferimento versionato non valido");
    }

    throw std::runtime_error(
        "Tipo di espressione non semplice: "
        "non può essere convertita in un singolo target"
    );
}

const Package* find_assignment(
    const std::string& name,
    const ResolutionState& state
)
{
    const auto it =
        state.assignments.find(name);

    if (it == state.assignments.end()) {
        return nullptr;
    }

    return &it->second.package;
}

bool same_candidate(const Package& left, const Package& right)
{
    if (left.identity && right.identity) return *left.identity == *right.identity;
    if (left.identity || right.identity) return false;
    return left.name == right.name && left.version == right.version;
}

/*
 * Validate a package already selected by the resolver against a new
 * dependency constraint.
 *
 * A package name alone is not sufficient to determine compatibility:
 * different dependency expressions may impose different version
 * constraints on the same package.
 */
void validate_existing_assignment(
    const DependencyTarget& target,
    const Package& requiring_package,
    const ResolutionState& state
)
{
    /*
     * Capability and feature names are selectors, not concrete package
     * assignment keys. The selected provider is tracked by its package
     * name, so there is no direct assignment to validate here.
     */
    if (target.kind != DependencyTarget::Kind::Package) {
        return;
    }

    const auto* assigned =
        find_assignment(
            target.name,
            state
        );

    if (assigned == nullptr) {
        return;
    }

    if (!satisfies(
            target.constraint,
            assigned->version
        )) {

        throw std::runtime_error(
            "Conflitto di dipendenze: il pacchetto " +
            requiring_package.name +
            " richiede una versione incompatibile di " +
            target.name +
            ", ma è già stata assegnata la versione " +
            assigned->version
        );
    }
}

void record_dependency_context(
    ResolutionState& state,
    const Package& package,
    DependencyContext context
)
{
    auto& contexts =
        state.contexts[package.name];

    if (std::find(
            contexts.begin(),
            contexts.end(),
            context
        ) == contexts.end()) {

        contexts.push_back(context);
    }
}

bool package_satisfies_target(const Package& package, const DependencyTarget& target)
{
    bool provides = false;
    switch (target.kind) {
        case DependencyTarget::Kind::Package: provides = package.name == target.name; break;
        case DependencyTarget::Kind::Capability: provides = std::find(package.provides.capabilities.begin(), package.provides.capabilities.end(), target.name) != package.provides.capabilities.end(); break;
        case DependencyTarget::Kind::Feature: provides = std::find(package.provides.features.begin(), package.provides.features.end(), target.name) != package.provides.features.end(); break;
    }
    std::string_view version = package.version;
    if (target.kind == DependencyTarget::Kind::Capability) {
        const auto it = package.provides.capability_versions.find(target.name);
        if (it != package.provides.capability_versions.end()) version = it->second;
    }
    return provides && satisfies(target.constraint, std::string(version));
}

bool violates_negative_dependency(
    const Package& package,
    const std::vector<NegativeDependency>& constraints
)
{
    for (const auto& constraint : constraints) {

        if (package_satisfies_target(package, constraint.target)) {
            return true;
        }
    }

    return false;
}

std::string semantic_assignment_key(const DependencyTarget &target);

athena::core::DependencyRole public_role(DependencyKind r)
{
    using R = athena::core::DependencyRole;
    switch (r)
    {
    case DependencyKind::Runtime:
        return R::Runtime;
    case DependencyKind::Build:
        return R::Build;
    case DependencyKind::Test:
        return R::Test;
    case DependencyKind::Optional:
        return R::Optional;
    case DependencyKind::Development:
        return R::Development;
    case DependencyKind::Propagated:
        return R::Propagated;
    }
    throw std::logic_error("Unknown dependency role");
}
athena::core::DependencyTargetContext public_context(DependencyContext c)
{
    using C = athena::core::DependencyTargetContext;
    switch (c)
    {
    case DependencyContext::Host:
        return C::Host;
    case DependencyContext::Build:
        return C::Build;
    case DependencyContext::Target:
        return C::Target;
    }
    throw std::logic_error("Unknown dependency context");
}
athena::core::VersionConstraint public_constraint(const VersionConstraint &c)
{
    athena::core::VersionConstraint result;
    using K = athena::core::VersionConstraintKind;
    using O = athena::core::VersionComparisonOperator;
    switch (c.kind)
    {
    case VersionConstraint::Kind::Any:
        result.kind = K::Any;
        break;
    case VersionConstraint::Kind::Comparison:
        result.kind = K::Comparison;
        result.version = c.comparison_value.version;
        switch (c.comparison_value.op)
        {
        case ComparisonOperator::Equal:
            result.comparison = O::Equal;
            break;
        case ComparisonOperator::NotEqual:
            result.comparison = O::NotEqual;
            break;
        case ComparisonOperator::Less:
            result.comparison = O::Less;
            break;
        case ComparisonOperator::LessEqual:
            result.comparison = O::LessEqual;
            break;
        case ComparisonOperator::Greater:
            result.comparison = O::Greater;
            break;
        case ComparisonOperator::GreaterEqual:
            result.comparison = O::GreaterEqual;
            break;
        }
        break;
    case VersionConstraint::Kind::And:
        result.kind = K::And;
        break;
    case VersionConstraint::Kind::Or:
        result.kind = K::Or;
        break;
    case VersionConstraint::Kind::Not:
        result.kind = K::Not;
        result.operands.push_back(public_constraint(*c.constraint));
        break;
    }
    for (const auto &child : c.constraints)
        result.operands.push_back(public_constraint(child));
    return result;
}
athena::core::DependencySelector public_selector(const DependencyTarget &t)
{
    using K = athena::core::SelectorKind;
    K kind;
    switch (t.kind)
    {
    case DependencyTarget::Kind::Package:
        kind = K::Package;
        break;
    case DependencyTarget::Kind::Capability:
        kind = K::Capability;
        break;
    case DependencyTarget::Kind::Feature:
        kind = K::Feature;
        break;
    }
    return {kind, t.name, public_constraint(t.constraint)};
}
// Selections and their reasons are accepted-branch facts, not a post-search log.
void record_candidate(const Package &candidate, ResolutionState &state)
{
    if (!candidate.identity)
        return;
    auto &result = state.public_result;
    if (std::any_of(result.selections.begin(), result.selections.end(),
                    [&](const auto &s) { return s.identity() == *candidate.identity; }))
        return;
    result.selections.push_back({*candidate.public_candidate, *candidate.snapshot, {}});
    for (const auto &feature : candidate.provides.features)
        result.offered_features.push_back({feature, *candidate.identity});
}
void record_reason(const Package &candidate, athena::core::InstallReason reason,
                   ResolutionState &state)
{
    if (!candidate.identity)
        return;
    const auto selected =
        std::find_if(state.public_result.selections.begin(), state.public_result.selections.end(),
                     [&](const auto &s) { return s.identity() == *candidate.identity; });
    if (selected == state.public_result.selections.end())
        throw std::logic_error("Missing selected candidate fact");
    selected->reasons.push_back(std::move(reason));
}
void record_negative(const ExpressionPtr &prohibited, const Package &owner, DependencyKind role,
                     DependencyContext context, athena::core::NegativeConstraintSource source,
                     ResolutionState &state)
{
    if (owner.identity)
        state.public_result.negative_constraints.push_back({{serialize_expression(prohibited)},
                                                            *owner.identity,
                                                            public_role(role),
                                                            public_context(context),
                                                            std::move(source)});
}
void record_binding(const DependencyTarget &target, const Package &selected, ResolutionState &state)
{
    if (target.kind == DependencyTarget::Kind::Capability && selected.identity)
        state.public_result.capability_bindings.push_back(
            {public_selector(target), *selected.identity});
}
void record_edge(const DependencyTarget &target, const Package &owner, DependencyKind role,
                 DependencyContext context, const std::string &feature, ResolutionState &state)
{
    const auto &selected = target.kind == DependencyTarget::Kind::Package
                               ? state.assignments.at(target.name).package
                               : state.semantic_assignments.at(semantic_assignment_key(target));
    record_binding(target, selected, state);
    if (!owner.identity || !selected.identity)
        return;
    athena::core::InstallReason reason;
    reason.kind = feature.empty() ? athena::core::InstallReasonKind::DependencyOf
                                  : athena::core::InstallReasonKind::EnabledByFeature;
    reason.package = *owner.identity;
    if (!feature.empty())
    {
        reason.feature = feature;
        reason.profile = state.public_result.profile;
    }
    record_reason(selected, reason, state);
    state.public_result.edges.push_back({*owner.identity, public_selector(target),
                                         *selected.identity, public_role(role),
                                         public_context(context), reason});
}

void apply_package_conflicts(const Package &owner, ResolutionState &state,
                             bool provider_origin = false)
{
    for (const auto &conflict : owner.conflicts)
    {
        const auto &expression = conflict.expression;
        if (!expression)
            continue;
        record_negative(expression, owner, conflict.kind, conflict.context, {}, state);
        if (expression->kind == Expression::Kind::Package ||
            expression->kind == Expression::Kind::Capability ||
            expression->kind == Expression::Kind::Feature ||
            expression->kind == Expression::Kind::VersionComparison)
        {
            const auto target = extract_dependency_target(expression);
            for (const auto &selected : state.result)
            {
                if (package_satisfies_target(selected, target))
                    throw ResolutionError(ResolutionErrorCode::Conflict,
                                          "Conflitto: " + owner.name + " incompatibile con " +
                                              selected.name);
            }
            state.negative_dependencies.push_back({target, provider_origin});
        }
        else
        {
            // Reuse the dependency AST for compound and conditional conflicts.
            state.deferred_negatives.push_back(
                {expression, owner, conflict.context, true, conflict.kind});
        }
    }
}

std::string semantic_assignment_key(const DependencyTarget &target)
{
    return semantic_assignment_key(target.kind == DependencyTarget::Kind::Capability
        ? SemanticSelectorKind::Capability : SemanticSelectorKind::Feature, target.name);
}

const Environment& environment_for_context(
    const ResolutionState& state,
    DependencyContext context
)
{
    switch (context) {
        case DependencyContext::Host:
            return state.environments.host;

        case DependencyContext::Build:
            return state.environments.build;

        case DependencyContext::Target:
            return state.environments.target;
    }

    throw std::runtime_error(
        "Contesto di dipendenza non valido"
    );
}

bool evaluate_environment_selector(
    const ExpressionPtr& expression,
    DependencyContext context,
    const ResolutionState& state
)
{
    const auto& environment =
        environment_for_context(state, context);

    switch (expression->kind) {

        case Expression::Kind::Platform: {
            const auto* selector =
                dynamic_cast<const PlatformRef*>(
                    expression.get()
                );

            if (selector == nullptr) {
                throw std::runtime_error(
                    "Espressione Platform non valida"
                );
            }

            return environment.platform == selector->name;
        }

        case Expression::Kind::Architecture: {
            const auto* selector =
                dynamic_cast<const ArchitectureRef*>(
                    expression.get()
                );

            if (selector == nullptr) {
                throw std::runtime_error(
                    "Espressione Architecture non valida"
                );
            }

            return environment.architecture == selector->name;
        }

        case Expression::Kind::ABI: {
            const auto* selector =
                dynamic_cast<const ABIRef*>(
                    expression.get()
                );

            if (selector == nullptr) {
                throw std::runtime_error(
                    "Espressione ABI non valida"
                );
            }

            return environment.abi == selector->name;
        }

        default:
            throw std::runtime_error(
                "Selector ambientale non supportato"
            );
    }
}


bool evaluate_boolean_condition(const ExpressionPtr& expression, const Package& owner,
                                DependencyContext context, const ResolutionState& state,
                                DependencyKind role = DependencyKind::Runtime, bool scoped_role = false)
{
    if (!expression) throw std::runtime_error("Condizione booleana nulla");
    // Unscoped guards still control their bodies independently of role
    // activation. Within an explicit scope, inactive condition branches keep
    // the existing false result used by scoped boolean conditions.
    if (state.use_policy && scoped_role && !expression_role_active(expression, role, true, state.policy))
        return false;
    switch (expression->kind) {
        case Expression::Kind::True: return true;
        case Expression::Kind::False: return false;
        case Expression::Kind::Platform:
        case Expression::Kind::Architecture:
        case Expression::Kind::ABI: return evaluate_environment_selector(expression, context, state);
        case Expression::Kind::Feature: {
            const auto* feature = dynamic_cast<const FeatureRef*>(expression.get());
            if (!feature) throw std::runtime_error("Feature condizionale non valida");
            if (state.use_policy) {
                const auto configured = state.policy.feature_states.find(feature->name);
                if (configured != state.policy.feature_states.end()) return configured->second;
            }
            return std::find(owner.provides.features.begin(), owner.provides.features.end(), feature->name) != owner.provides.features.end();
        }
        case Expression::Kind::And: {
            const auto* e = dynamic_cast<const AndExpression*>(expression.get()); if (!e) throw std::runtime_error("AND condizionale non valido");
            for (const auto& child : e->expressions) if (!evaluate_boolean_condition(child, owner, context, state, role, scoped_role)) return false; return true;
        }
        case Expression::Kind::Or: {
            const auto* e = dynamic_cast<const OrExpression*>(expression.get()); if (!e) throw std::runtime_error("OR condizionale non valido");
            for (const auto& child : e->expressions) if (evaluate_boolean_condition(child, owner, context, state, role, scoped_role)) return true; return false;
        }
        case Expression::Kind::Not: {
            const auto* e = dynamic_cast<const NotExpression*>(expression.get()); if (!e || !e->expression) throw std::runtime_error("NOT condizionale non valido"); return !evaluate_boolean_condition(e->expression, owner, context, state, role, scoped_role);
        }
        case Expression::Kind::Scoped: {
            const auto* e = dynamic_cast<const ScopedExpression*>(expression.get()); if (!e || !e->body) throw std::runtime_error("Scope condizionale non valido");
            if (state.use_policy && !expression_role_active(e->body, e->kind_role, e->enabled, state.policy)) return false;
            return evaluate_boolean_condition(e->body, owner, e->context, state, e->kind_role, true);
        }
        default: throw std::runtime_error("La condizione supporta solo selector, feature e operatori booleani");
    }
}

std::optional<bool> expression_present_in_solution(const ExpressionPtr& expression,
    const std::vector<Package>& packages, const Package& owner, DependencyContext context,
    DependencyKind inherited_role, const ResolutionState& state, bool conflict_expression = false,
    bool scoped_role = false)
{
    if (!expression) throw std::runtime_error("Espressione negativa nulla");
    // Ordinary package conflicts remain unconditional, as before. Only an
    // explicit scope introduces role gating into a conflict subtree.
    if (state.use_policy && !expression_role_active(expression, inherited_role, true, state.policy,
                                                     !conflict_expression || scoped_role))
        return std::nullopt;
    switch (expression->kind) {
        case Expression::Kind::True: return true;
        case Expression::Kind::False: return false;
        case Expression::Kind::Package:
        case Expression::Kind::Capability:
        case Expression::Kind::Feature:
        case Expression::Kind::VersionComparison: {
            const auto target = extract_dependency_target(expression);
            return std::any_of(packages.begin(), packages.end(), [&](const Package& p){return package_satisfies_target(p,target);});
        }
        case Expression::Kind::Platform:
        case Expression::Kind::Architecture:
        case Expression::Kind::ABI: return evaluate_environment_selector(expression, context, state);
        case Expression::Kind::And: {
            const auto* e = dynamic_cast<const AndExpression*>(expression.get()); if (!e) throw std::runtime_error("AND negative non valido");
            for (const auto& child : e->expressions) { const auto value=expression_present_in_solution(child,packages,owner,context,inherited_role,state,conflict_expression,scoped_role); if (value && !*value) return false; }
            return true;
        }
        case Expression::Kind::Or: {
            const auto* e = dynamic_cast<const OrExpression*>(expression.get()); if (!e) throw std::runtime_error("OR negative non valido");
            for (const auto& child : e->expressions) { const auto value=expression_present_in_solution(child,packages,owner,context,inherited_role,state,conflict_expression,scoped_role); if (value && *value) return true; }
            return false;
        }
        case Expression::Kind::Not: {
            const auto* e = dynamic_cast<const NotExpression*>(expression.get()); if (!e || !e->expression) throw std::runtime_error("NOT negative non valido");
            const auto value=expression_present_in_solution(e->expression,packages,owner,context,inherited_role,state,conflict_expression,scoped_role); return !value.value_or(false);
        }
        case Expression::Kind::Scoped: {
            const auto* e = dynamic_cast<const ScopedExpression*>(expression.get()); if (!e || !e->body) throw std::runtime_error("Scope negative non valido");
            if (state.use_policy && !expression_role_active(e->body,e->kind_role,e->enabled,state.policy)) return std::nullopt;
            return expression_present_in_solution(e->body,packages,owner,e->context,e->kind_role,state,conflict_expression,true);
        }
        case Expression::Kind::Conditional: {
            const auto* e = dynamic_cast<const ConditionalExpression*>(expression.get()); if (!e || !e->condition || !e->body) throw std::runtime_error("Conditional negative non valido");
            if (!evaluate_boolean_condition(e->condition,owner,context,state,inherited_role)) return !conflict_expression;
            return expression_present_in_solution(e->body,packages,owner,context,inherited_role,state,conflict_expression,scoped_role);
        }
    }
    return false;
}

bool violates_deferred_negatives(const Package& root, const ResolutionState& state)
{
    if (state.deferred_negatives.empty()) return false;
    std::vector<Package> selected;
    selected.reserve(state.assignments.size()+1);
    for (const auto& [name, assignment] : state.assignments) { (void)name; selected.push_back(assignment.package); }
    selected.push_back(root);
    for (const auto& negative : state.deferred_negatives) {
        const auto value=expression_present_in_solution(negative.expression,selected,negative.owner,
            negative.context,negative.role,state,negative.conflict_expression);
        if (value && *value) return true;
    }
    return false;
}

void resolve_expression(
    const ExpressionPtr& expression,
    const Package& requiring_package,
    DependencyContext context,
    DependencyKind role,
    bool enabled,
    const athena::repository::RepositoryIndex& repository,
    ResolutionState& state,
    const std::string& feature = {}
);

void resolve_configured_feature_dependencies(
    const Package& package,
    const athena::repository::RepositoryIndex& repository,
    ResolutionState& state)
{
    if (!state.use_policy) return;
    for (const auto& [feature, enabled] : state.policy.feature_states) {
        if (!enabled) continue;
        const auto declared = package.feature_dependencies.find(feature);
        if (declared == package.feature_dependencies.end()) continue;
        state.feature_states[feature] = true;
        if (package.identity) state.public_result.activated_features.push_back({feature, state.public_result.profile, *package.identity});
        for (const auto& dependency : declared->second) {
            resolve_expression(dependency.expression, package, dependency.context,
                               dependency.kind, dependency.enabled, repository, state, feature);
        }
    }
}

void resolve_package_target(
    const DependencyTarget& target,
    const Package& requiring_package,
    DependencyContext context,
    const athena::repository::RepositoryIndex& repository,
    ResolutionState& state
)
{
    if (target.kind != DependencyTarget::Kind::Package) {
        const auto key = semantic_assignment_key(target);
        const auto selected = state.semantic_assignments.find(key);
        if (selected != state.semantic_assignments.end()) {
            const auto* provider = find_assignment(selected->second.name, state);
            if (!provider || !same_candidate(*provider, selected->second) || !package_satisfies_target(*provider, target))
                throw std::runtime_error("Il provider già selezionato non soddisfa il vincolo semantico " + target.name);
            record_dependency_context(state, *provider, context);
            resolve_package_target({DependencyTarget::Kind::Package, provider->name, state.repository_rules ? VersionConstraint::any() : target.constraint},
                                   requiring_package, context, repository, state);
            return;
        }
    }

    if (state.repository_rules && target.kind != DependencyTarget::Kind::Package) {
        const Package* reuse = nullptr;
        for (const auto& [name, assignment] : state.assignments) {
            (void)name;
            if (package_satisfies_target(assignment.package, target) &&
                (!reuse || assignment.package.name < reuse->name)) reuse = &assignment.package;
        }
        if (reuse) {
            const auto provider = *reuse;
            state.semantic_assignments[semantic_assignment_key(target)] = provider;
            resolve_package_target({DependencyTarget::Kind::Package, provider.name, VersionConstraint::any()},
                                   requiring_package, context, repository, state);
            return;
        }
    }

    /*
     * If this package already has an assignment, every new constraint
     * must be compatible with that exact version.
     */
    validate_existing_assignment(
        target,
        requiring_package,
        state
    );

    /*
     * A semantic dependency can reuse an already selected package.
     * Preserve every context in which that package is required.
     */
    if (target.kind == DependencyTarget::Kind::Package) {
        if (const auto* assigned =
                find_assignment(target.name, state)) {

            record_dependency_context(
                state,
                *assigned,
                context
            );
        }
    }

    /*
     * An assigned package can still be actively resolving its own
     * dependencies. Cycle detection therefore happens first.
     */
    if ((!state.repository_rules || target.kind == DependencyTarget::Kind::Package) && state.resolving.contains(target.name)) {

        throw std::runtime_error(
            "Ciclo di dipendenze rilevato: " +
            target.name
        );
    }

    if ((!state.repository_rules || target.kind == DependencyTarget::Kind::Package) && state.resolved.contains(target.name)) {
        return;
    }

    const auto* existing_assignment =
        (state.repository_rules && target.kind != DependencyTarget::Kind::Package)
            ? nullptr : find_assignment(target.name, state);

    if (existing_assignment != nullptr) {
        // Local AND/OR rollback replaces assignments. Own the immutable source
        // candidate while replaying its declarations rather than borrowing a
        // package from the map being restored.
        const auto propagating_package = *existing_assignment;

        /*
         * The package is assigned but not yet resolved. This happens
         * when global backtracking restores a snapshot taken while an
         * ancestor package was still propagating its dependencies.
         *
         * Re-enter that package so the dependency graph can be replayed
         * until the pending global decision is reached again.
         */
        state.resolving.insert(
            propagating_package.name
        );

        try {

            for (const auto& dependency :
                 propagating_package.dependencies) {

                resolve_expression(
                    dependency.expression,
                    propagating_package,
                    dependency.context,
                    dependency.kind,
                    dependency.enabled,
                    repository,
                    state
                );
            }

            resolve_configured_feature_dependencies(propagating_package, repository, state);

            state.resolving.erase(
                propagating_package.name
            );

            state.resolved.insert(
                propagating_package.name
            );

            state.result.push_back(
                propagating_package
            );
        }
        catch (...) {

            state.resolving.erase(
                propagating_package.name
            );

            throw;
        }

        return;
    }

    const auto candidates =
        find_dependency_candidates(
            target,
            repository,
            &state
        );

    if (candidates.empty()) {
        const auto code = target.kind == DependencyTarget::Kind::Capability
            ? ResolutionErrorCode::CapabilityUnsatisfied
            : target.kind == DependencyTarget::Kind::Feature
                ? ResolutionErrorCode::FeatureUnsatisfied
                : ResolutionErrorCode::NoProvider;
        throw ResolutionError(code,
            "Dipendenza non trovata o nessuna versione "
            "compatibile disponibile: " +
            target.name +
            " (richiesta da " +
            requiring_package.name +
            ")"
        );
    }

    /*
     * A package with multiple compatible versions creates a global
     * decision point. The snapshot represents the exact state before
     * this package was selected.
     *
     * For a single candidate there is no search branch to maintain,
     * so we can resolve it directly.
     */
    std::size_t decision_index =
        std::numeric_limits<std::size_t>::max();

    /*
     * During global backtracking the most recent decision can already
     * belong to this package. In that case the solver has restored its
     * snapshot and advanced candidate_index before restarting
     * propagation. Reuse that decision instead of creating a duplicate.
     */
    const std::string decision_name = target.kind == DependencyTarget::Kind::Package
        ? target.name : semantic_assignment_key(target);
    if (!state.decisions.empty() &&
        state.decisions.back().name == decision_name &&
        state.decisions.back().candidates.size() == candidates.size()) {

        decision_index =
            state.decisions.size() - 1;
    }
    else if (candidates.size() > 1) {

        state.decisions.push_back({
            decision_name,
            candidates,
            0,
            snapshot_resolution_state(state)
        });

        decision_index =
            state.decisions.size() - 1;
    }

    /*
     * Select the candidate represented by this decision, or the only
     * candidate when no decision point was necessary.
     */
    const auto& dependency_package =
        candidates[
            decision_index ==
                    std::numeric_limits<std::size_t>::max()
                ? 0
                : state.decisions[decision_index].candidate_index
        ];

    if (violates_negative_dependency(
            dependency_package,
            state.negative_dependencies
        )) {
        const auto provider_conflict = std::any_of(state.negative_dependencies.begin(), state.negative_dependencies.end(),
            [&dependency_package](const NegativeDependency& negative) {
                return negative.provider_origin && package_satisfies_target(dependency_package, negative.target);
            });
        throw ResolutionError(provider_conflict ? ResolutionErrorCode::ProviderConflict : ResolutionErrorCode::Conflict,
            "Dipendenza vietata dal vincolo NOT: " +
            dependency_package.name +
            " (" +
            dependency_package.version +
            ")"
        );
    }

    /*
     * Record the concrete assignment before propagating dependencies.
     * This allows sibling constraints to detect conflicts immediately.
     */
    state.assignments[dependency_package.name] = {
        dependency_package
        };

    record_candidate(dependency_package, state);
    apply_package_conflicts(dependency_package, state, target.kind != DependencyTarget::Kind::Package);

    if (target.kind != DependencyTarget::Kind::Package)
        state.semantic_assignments[semantic_assignment_key(target)] = dependency_package;

    record_dependency_context(
        state,
        dependency_package,
        context
    );

    state.resolving.insert(
        dependency_package.name
    );

    try {

        for (const auto& dependency :
             dependency_package.dependencies) {

            resolve_expression(
                dependency.expression,
                dependency_package,
                dependency.context,
                dependency.kind,
                dependency.enabled,
                repository,
                state
            );
        }

        resolve_configured_feature_dependencies(dependency_package, repository, state);

        state.resolving.erase(
            dependency_package.name
        );

        state.resolved.insert(
            dependency_package.name
        );

        state.result.push_back(
            dependency_package
        );
    }
    catch (...) {

        state.resolving.erase(
            dependency_package.name
        );

        /*
         * A local failure must not leave a half-applied assignment in
         * the current state. The global solver will use the decision
         * snapshot to retry this branch when appropriate.
         */
        if (decision_index !=
            std::numeric_limits<std::size_t>::max()) {

            const auto snapshot =
                state.decisions[decision_index].snapshot;

            restore_resolution_state(
                state,
                snapshot
            );
        }

        throw;
    }
}

void resolve_expression(
    const ExpressionPtr& expression,
    const Package& requiring_package,
    DependencyContext context,
    DependencyKind role,
    bool enabled,
    const athena::repository::RepositoryIndex& repository,
    ResolutionState& state,
    const std::string& feature
)
{
    if (!expression) {
        throw std::runtime_error(
            "Espressione di dipendenza nulla"
        );
    }

    if (state.use_policy &&
        !expression_role_active(expression, role, enabled, state.policy)) return;

    switch (expression->kind) {

        case Expression::Kind::Platform:
        case Expression::Kind::Architecture:
        case Expression::Kind::ABI: {

            if (!evaluate_environment_selector(
                    expression,
                    context,
                    state)) {

                throw std::runtime_error(
                    "Vincolo ambientale non soddisfatto per " +
                    requiring_package.name
                );
            }

            return;
        }

        case Expression::Kind::Package:
        case Expression::Kind::Capability:
        case Expression::Kind::Feature:
        case Expression::Kind::VersionComparison: {

            const auto target =
                extract_dependency_target(
                    expression
                );

            resolve_package_target(
                target,
                requiring_package,
                context,
                repository,
                state
            );
            record_edge(target, requiring_package, role, context, feature, state);

            return;
        }

        case Expression::Kind::And: {

            const auto* and_expression =
                dynamic_cast<const AndExpression*>(
                    expression.get()
                );

            if (and_expression == nullptr) {
                throw std::runtime_error(
                    "Espressione AND non valida"
                );
            }

            const auto before = snapshot_resolution_state(state);
            const auto decisions_before = state.decisions;
            try {
                for (const auto& child : and_expression->expressions)
                    resolve_expression(child, requiring_package, context, role, enabled, repository, state, feature);
            } catch (...) {
                restore_resolution_state(state, before);
                state.decisions = decisions_before;
                throw;
            }

            return;
        }

        case Expression::Kind::Or: {

            const auto* or_expression =
                dynamic_cast<const OrExpression*>(
                    expression.get()
                );

            if (or_expression == nullptr) {
                throw std::runtime_error(
                    "Espressione OR non valida"
                );
            }

            std::string last_error;
            std::optional<ResolutionErrorCode> last_error_code;

            for (const auto& child :
                 or_expression->expressions) {

                if (state.use_policy && !expression_role_active(child, role, enabled, state.policy))
                    continue;

                /*
                 * Each OR alternative is attempted transactionally.
                 *
                 * If the alternative fails, all state changes produced
                 * by that attempt are rolled back before trying the
                 * next alternative.
                 */
                const auto before = snapshot_resolution_state(state);
                const auto decisions_before = state.decisions;

                try {

                    resolve_expression(
                        child,
                        requiring_package,
                        context,
                        role,
                        enabled,
                        repository,
                        state,
                        feature
                    );

                    return;
                }
                catch (const std::runtime_error& error) {

                    /*
                     * Roll back every change made by the failed
                     * alternative, including packages already resolved,
                     * resolution state, and negative NOT constraints.
                     */
                    restore_resolution_state(state, before);
                    state.decisions = decisions_before;

                    last_error = error.what();
                    if (const auto* structured = dynamic_cast<const ResolutionError*>(&error))
                        last_error_code = structured->code();
                }
            }

            if (last_error_code)
                throw ResolutionError(*last_error_code,
                    "Nessuna alternativa soddisfacibile per la dipendenza richiesta da " +
                    requiring_package.name + (last_error.empty() ? "" : ": " + last_error));
            throw std::runtime_error(
                "Nessuna alternativa soddisfacibile per "
                "la dipendenza richiesta da " +
                requiring_package.name +
                (last_error.empty()
                    ? ""
                    : ": " + last_error)
            );
        }

        case Expression::Kind::Not: {

            const auto* not_expression =
                dynamic_cast<const NotExpression*>(
                    expression.get()
                );

            if (not_expression == nullptr ||
                !not_expression->expression) {

                throw std::runtime_error(
                    "Espressione NOT non valida"
                );
            }

            record_negative(not_expression->expression, requiring_package, role, context,
                {feature.empty() ? athena::core::NegativeConstraintSourceKind::PackageDependency : athena::core::NegativeConstraintSourceKind::FeatureDependency, feature}, state);

            const auto selector_only = [&](const auto& self, const ExpressionPtr& node) -> bool {
                if (!node) return false;
                switch (node->kind) {
                    case Expression::Kind::True:
                    case Expression::Kind::False:
                    case Expression::Kind::Platform:
                    case Expression::Kind::Architecture:
                    case Expression::Kind::ABI: return true;
                    case Expression::Kind::And: {
                        const auto* e = dynamic_cast<const AndExpression*>(node.get()); if (!e) return false;
                        return std::all_of(e->expressions.begin(), e->expressions.end(), [&](const auto& c){return self(self,c);});
                    }
                    case Expression::Kind::Or: {
                        const auto* e = dynamic_cast<const OrExpression*>(node.get()); if (!e) return false;
                        return std::all_of(e->expressions.begin(), e->expressions.end(), [&](const auto& c){return self(self,c);});
                    }
                    case Expression::Kind::Not: {
                        const auto* e = dynamic_cast<const NotExpression*>(node.get()); return e && self(self,e->expression);
                    }
                    default: return false;
                }
            };
            if (selector_only(selector_only, not_expression->expression)) {
                if (evaluate_boolean_condition(not_expression->expression, requiring_package, context, state))
                    throw std::runtime_error("Negazione di selector non soddisfatta");
                return;
            }

            const auto inner_kind = not_expression->expression->kind;
            if (inner_kind == Expression::Kind::And || inner_kind == Expression::Kind::Or ||
                inner_kind == Expression::Kind::Not || inner_kind == Expression::Kind::Conditional ||
                inner_kind == Expression::Kind::Scoped) {
                state.deferred_negatives.push_back({not_expression->expression, requiring_package, context, false, role});
                return;
            }
            const auto target = extract_dependency_target(not_expression->expression);

        // NOT accepts package, capability and feature references.

            for (const auto& package : state.result) {

                if (package_satisfies_target(package, target)) {

                    throw ResolutionError(ResolutionErrorCode::Conflict,
                        "Vincolo NOT violato dal pacchetto già "
                        "risolto: " +
                        package.name +
                        " (" +
                        package.version +
                        ")"
                    );
                }
            }

            state.negative_dependencies.push_back({target});

            return;
        }

        case Expression::Kind::Scoped: {
            const auto* scoped = dynamic_cast<const ScopedExpression*>(expression.get());
            if (!scoped || !scoped->body) throw std::runtime_error("Espressione scoped non valida");
            resolve_expression(scoped->body, requiring_package, scoped->context, scoped->kind_role,
                               scoped->enabled, repository, state, feature);
            return;
        }

        case Expression::Kind::Conditional: {
            const auto* conditional = dynamic_cast<const ConditionalExpression*>(expression.get());
            if (!conditional || !conditional->condition || !conditional->body)
                throw std::runtime_error("Espressione conditional non valida");
            if (evaluate_boolean_condition(conditional->condition, requiring_package, context, state, role))
                resolve_expression(conditional->body, requiring_package, context, role, enabled, repository, state, feature);
            return;
        }

        case Expression::Kind::True:
            return;

        case Expression::Kind::False:
            throw std::runtime_error("L'espressione false non può essere soddisfatta");

        default:
            throw std::runtime_error(
                "Tipo di espressione di dipendenza non "
                "supportato dal resolver"
            );
    }
}

void resolve_recursive(
    const Package& package,
    const athena::repository::RepositoryIndex& repository,
    ResolutionState& state
)
{
    if (state.resolved.contains(package.name)) {
        return;
    }

    if (state.resolving.contains(package.name)) {

        throw std::runtime_error(
            "Ciclo di dipendenze rilevato: " +
            package.name
        );
    }

    state.resolving.insert(package.name);
    apply_package_conflicts(package, state);

    /*
     * The root package represents the package being produced/installed,
     * therefore its own semantic context is Target.
     */
    record_dependency_context(
        state,
        package,
        DependencyContext::Target
    );

    try {

        for (const auto& dependency : package.dependencies) {

            resolve_expression(
                dependency.expression,
                package,
                dependency.context,
                dependency.kind,
                dependency.enabled,
                repository,
                state
            );
        }

        resolve_configured_feature_dependencies(package, repository, state);

        /*
         * The package has been completely propagated. It is no longer
         * part of the active recursion chain and can be committed as
         * resolved.
         */
        state.resolving.erase(package.name);
        state.resolved.insert(package.name);

        state.result.push_back(package);
    }
    catch (...) {

        /*
         * An exception abandons this recursive branch. The package must
         * therefore be removed from the active recursion chain before
         * the global solver restores a decision snapshot and retries.
         */
        state.resolving.erase(package.name);

        throw;
    }
}

struct CandidateResolution {
    std::vector<Package> packages;
    std::unordered_map<std::string, std::vector<DependencyContext>> contexts;
};

CandidateResolution resolve_candidate_graph(
    const Package& root,
    const athena::repository::RepositoryIndex& repository,
    const Environments& environments,
    const DependencyResolutionContext* policy)
{
    ResolutionState state;
    state.environments = environments;
    if (policy) {
        state.policy = *policy;
        state.use_policy = true;
        for (const auto& [feature, enabled] : policy->feature_states) {
            if (enabled && !root.feature_dependencies.contains(feature))
                throw ResolutionError(ResolutionErrorCode::FeatureUnsatisfied,
                    "Feature configurata ma non dichiarata dal package radice: " + feature);
            state.feature_states[feature] = enabled;
        }
    }

    for (;;) {
        try {
            resolve_recursive(root, repository, state);
            if (violates_negative_dependency(root, state.negative_dependencies))
                throw ResolutionError(ResolutionErrorCode::Conflict,
                    "Pacchetto radice vietato dal vincolo NOT: " + root.name);
            if (violates_deferred_negatives(root, state))
                throw ResolutionError(ResolutionErrorCode::Conflict,
                    "Espressione NOT composta violata dal grafo selezionato");
            return {state.result, state.contexts};
        } catch (const std::runtime_error& error) {
            if (!backtrack_resolution(state)) {
                if (const auto* structured = dynamic_cast<const ResolutionError*>(&error)) throw *structured;
                throw ResolutionError(ResolutionErrorCode::ResolutionFailure, error.what());
            }
        }
    }
}

VersionConstraint request_constraint(const athena::core::VersionConstraint& c)
{
    using K = athena::core::VersionConstraintKind;
    auto invalid = []() -> VersionConstraint {
        throw ResolutionError(ResolutionErrorCode::InvalidSelector, "Invalid root version constraint");
    };
    switch (c.kind) {
    case K::Any:
        if (!c.version.empty() || !c.operands.empty()) return invalid();
        return VersionConstraint::any();
    case K::Comparison: {
        if (c.version.empty() || !c.operands.empty()) return invalid();
        ComparisonOperator op;
        using O = athena::core::VersionComparisonOperator;
        switch (c.comparison) {
        case O::Equal: op = ComparisonOperator::Equal; break;
        case O::NotEqual: op = ComparisonOperator::NotEqual; break;
        case O::Less: op = ComparisonOperator::Less; break;
        case O::LessEqual: op = ComparisonOperator::LessEqual; break;
        case O::Greater: op = ComparisonOperator::Greater; break;
        case O::GreaterEqual: op = ComparisonOperator::GreaterEqual; break;
        default: return invalid();
        }
        return VersionConstraint::comparison(op, c.version);
    }
    case K::And: case K::Or: {
        if (!c.version.empty() || c.operands.empty()) return invalid();
        std::vector<VersionConstraint> children;
        for (const auto& child : c.operands) children.push_back(request_constraint(child));
        return c.kind == K::And ? VersionConstraint::all(std::move(children)) : VersionConstraint::any_of(std::move(children));
    }
    case K::Not:
        if (!c.version.empty() || c.operands.size() != 1) return invalid();
        return VersionConstraint::not_(request_constraint(c.operands.front()));
    }
    return invalid();
}

DependencyTarget request_target(const athena::core::InstallTarget& target)
{
    const auto& selector = target.selector;
    if (selector.name.empty() || selector.name.find_first_of(" \t\r\n") != std::string::npos)
        throw ResolutionError(ResolutionErrorCode::InvalidSelector, "Empty or invalid root selector");
    DependencyTarget::Kind kind;
    using K = athena::core::SelectorKind;
    switch (selector.kind) {
    case K::Package: kind = DependencyTarget::Kind::Package; break;
    case K::Capability: kind = DependencyTarget::Kind::Capability; break;
    case K::Feature: kind = DependencyTarget::Kind::Feature; break;
    default: throw ResolutionError(ResolutionErrorCode::InvalidSelector, "Unknown root selector kind");
    }
    return {kind, selector.name, request_constraint(selector.constraint)};
}

DependencyKind request_role(athena::core::DependencyRole role)
{
    using R = athena::core::DependencyRole;
    switch (role) {
    case R::Runtime: return DependencyKind::Runtime;
    case R::Build: return DependencyKind::Build;
    case R::Test: return DependencyKind::Test;
    case R::Optional: return DependencyKind::Optional;
    case R::Development: return DependencyKind::Development;
    case R::Propagated: return DependencyKind::Propagated;
    }
    throw ResolutionError(ResolutionErrorCode::InvalidRequest, "Unknown active dependency role");
}

athena::install::InstallPlan legacy_plan(const CandidateResolution& solution)
{
    athena::install::InstallPlan plan;
    for (const auto& candidate : solution.packages) {
        const auto found = solution.contexts.find(candidate.name);
        if (found == solution.contexts.end() || found->second.empty()) {
            plan.add(candidate, DependencyContext::Target);
            continue;
        }
        for (const auto context : found->second) plan.add(candidate, context);
    }
    return plan;
}

}

RequestResolution resolve_request(const athena::core::InstallRequest& request)
{
    if (request.targets.empty() && request.system_policies.empty())
        throw ResolutionError(ResolutionErrorCode::InvalidRequest, "Request has no requirements");
    // B.2 validation runs before eligibility filtering, including rejected inputs.
    const auto adapted = athena::repository::adapt_snapshots(request.snapshots);
    for (const auto& [id, rule] : request.repositories) {
        try {
            if (id.empty()) throw std::invalid_argument("Empty repository ID");
            athena::core::validate_repository_rule(rule);
        } catch (const std::exception& e) {
            throw ResolutionError(ResolutionErrorCode::InvalidRepositoryRule, e.what());
        }
    }
    for (const auto& snapshot : request.snapshots)
        if (!request.repositories.contains(snapshot.repository_id()))
            throw ResolutionError(ResolutionErrorCode::InvalidRepositoryRule, "Snapshot has no repository rule");
    athena::repository::RepositoryIndex repository;
    for (const auto& candidate : adapted.resolver_candidates())
        if (athena::core::candidate_matches_target(*candidate.public_candidate, request.target))
            repository.add_candidate(candidate);

    ResolutionState state;
    state.environments = athena::repository::adapt_environments(request.target, request.environments);
    state.repository_rules = &request.repositories;
    state.public_result.target = request.target;
    state.public_result.environments = request.environments;
    state.public_result.profile = request.profile;
    for(const auto& [feature, enabled] : request.feature_states)
        state.public_result.configured_features.push_back({feature, request.profile, enabled});
    state.use_policy = true;
    for (const auto role : request.active_roles) state.policy.active_kinds.push_back(request_role(role));
    if (state.policy.active_kinds.empty()) state.policy.active_kinds = {DependencyKind::Runtime};
    state.policy.optional_enabled = request.optional_enabled &&
        std::find(state.policy.active_kinds.begin(), state.policy.active_kinds.end(), DependencyKind::Optional) != state.policy.active_kinds.end();
    state.policy.propagated_enabled = request.propagated_enabled;
    state.public_result.active_roles.clear();
    for (const auto role : state.policy.active_kinds)
        state.public_result.active_roles.push_back(static_cast<athena::core::DependencyRole>(role));
    state.public_result.optional_enabled = state.policy.optional_enabled;
    state.public_result.propagated_enabled = state.policy.propagated_enabled;
    for (const auto& [id, rule] : request.repositories)
        state.public_result.version_schemes.emplace(id, rule.version_scheme);
    state.policy.feature_states = request.feature_states;
    for (const auto& [feature, enabled] : request.feature_states) {
        if (feature.empty()) throw ResolutionError(ResolutionErrorCode::InvalidRequest, "Empty configured feature");
        state.feature_states[feature] = enabled;
    }
    struct Requirement {
        std::optional<athena::core::SystemPolicyId> policy;
        athena::core::InstallTarget original;
        DependencyTarget target;
    };
    std::vector<Requirement> requirements;
    auto add = [&](const athena::core::InstallTarget& original, std::optional<athena::core::SystemPolicyId> policy) {
        auto target = request_target(original);
        auto any = target;
        any.constraint = VersionConstraint::any();
        if (find_dependency_candidates(any, repository, &state).empty()) {
            auto code = target.kind == DependencyTarget::Kind::Package ? ResolutionErrorCode::UnknownPackageRoot :
                target.kind == DependencyTarget::Kind::Capability ? ResolutionErrorCode::NoCapabilityProvider : ResolutionErrorCode::NoFeatureProvider;
            if (policy) code = ResolutionErrorCode::UnsatisfiedSystemPolicy;
            throw ResolutionError(code, "No candidate for request selector: " + target.name);
        }
        if (find_dependency_candidates(target, repository, &state).empty())
            throw ResolutionError(policy ? ResolutionErrorCode::UnsatisfiedSystemPolicy : ResolutionErrorCode::IncompatibleRootVersion,
                                  "No compatible version for request selector: " + target.name);
        requirements.push_back({std::move(policy), original, std::move(target)});
    };
    for (const auto& target : request.targets) add(target, std::nullopt);
    for (const auto& policy : request.system_policies) {
        if (policy.policy.empty() || policy.required.empty())
            throw ResolutionError(ResolutionErrorCode::InvalidRequest, "Empty system policy requirement");
        for (const auto& target : policy.required) add(target, policy.policy);
    }
    const auto owner = Package::legacy(athena::package::Package{});
    std::optional<athena::core::SystemPolicyId> current_policy;
    for (;;) {
        try {
            // Restored snapshots retain satisfied prefixes; replaying every
            // requirement validates them and resumes incomplete propagation.
            for (std::size_t i = 0; i < requirements.size(); ++i) {
                const auto& requirement = requirements[i];
                current_policy = requirement.policy;
                resolve_package_target(requirement.target, owner, DependencyContext::Target, repository, state);
                const auto& selected = requirement.target.kind == DependencyTarget::Kind::Package
                    ? state.assignments.at(requirement.target.name).package
                    : state.semantic_assignments.at(semantic_assignment_key(requirement.target));
                athena::core::InstallReason reason;
                reason.kind = requirement.policy ? athena::core::InstallReasonKind::RequiredBySystemPolicy : athena::core::InstallReasonKind::ExplicitRequest;
                if (requirement.policy) reason.policy = requirement.policy;
                else reason.explicit_target = requirement.original;
                record_reason(selected, std::move(reason), state);
                if (requirement.original.selector.kind == athena::core::SelectorKind::Capability)
                    state.public_result.capability_bindings.push_back({requirement.original.selector, *selected.identity});
                if(requirement.policy) state.public_result.system_policy_selections.push_back({*requirement.policy, requirement.original.selector, *selected.identity});
                if (i < state.origins.size()) state.origins[i] = {requirement.policy, requirement.original, selected};
                else state.origins.push_back({requirement.policy, requirement.original, selected});
            }
            for (const auto& candidate : state.result)
                if (violates_deferred_negatives(candidate, state))
                    throw ResolutionError(ResolutionErrorCode::Conflict, "Selected graph violates composed NOT");
            auto& result = state.public_result;
            athena::core::detail::canonicalize(result);
            return {state.result, state.origins, request.profile, std::move(result)};
        } catch (const std::runtime_error& error) {
            if (!backtrack_resolution(state)) {
                if(!current_policy) if(const auto* precise=dynamic_cast<const ResolutionError*>(&error)) throw *precise;
                throw ResolutionError(current_policy ? ResolutionErrorCode::UnsatisfiedSystemPolicy : ResolutionErrorCode::IncompatibleRoots,
                                      error.what());
            }
        }
    }
}

std::vector<athena::package::Package> resolve(
    const athena::package::Package& root,
    const athena::repository::Repository& repository
)
{
    /*
     * Repository is the public abstraction used by callers. The resolver
     * deliberately delegates to its index instead of duplicating package
     * lookup or repository storage logic.
     */
    return resolve(
        root,
        repository.index()
    );
}

athena::install::InstallPlan resolve_plan(
    const athena::package::Package& root,
    const athena::repository::RepositoryIndex& repository,
    const Environments& environments
)
{
    return legacy_plan(resolve_candidate_graph(
        athena::repository::ResolverCandidate::legacy(root), repository, environments, nullptr));
}

athena::install::InstallPlan resolve_plan(
    const athena::package::Package& root,
    const athena::repository::RepositoryIndex& repository,
    const Environments& environments,
    const DependencyResolutionContext& context
)
{
    return legacy_plan(resolve_candidate_graph(
        athena::repository::ResolverCandidate::legacy(root), repository, environments, &context));
}

std::vector<athena::repository::ResolverCandidate> resolve_candidates(
    const athena::package::Package& root,
    const athena::repository::RepositoryIndex& repository,
    const Environments& environments,
    const DependencyResolutionContext& context)
{
    return resolve_candidate_graph(athena::repository::ResolverCandidate::legacy(root),
                                   repository, environments, &context).packages;
}

athena::install::InstallPlan resolve_plan(
    const athena::package::Package& root,
    const athena::repository::RepositoryIndex& repository
)
{
    return resolve_plan(
        root,
        repository,
        native_environments()
    );
}

athena::install::InstallPlan resolve_plan(
    const athena::package::Package& root,
    const athena::repository::Repository& repository
)
{
    /*
     * Repository owns package loading and indexing; the resolver only
     * consumes the index when constructing the installation plan.
     */
    return resolve_plan(
        root,
        repository.index()
    );
}

/*
 * Compatibility wrapper.
 *
 * Keep the existing vector-based API source-compatible while making
 * InstallPlan the canonical result of dependency resolution.
 */
std::vector<athena::package::Package> resolve(
    const athena::package::Package& root,
    const athena::repository::RepositoryIndex& repository,
    const Environments& environments
)
{
    const auto plan = resolve_plan(
        root,
        repository,
        environments
    );

    return plan.packages();
}

std::vector<athena::package::Package> resolve(
    const athena::package::Package& root,
    const athena::repository::RepositoryIndex& repository
)
{
    const auto plan = resolve_plan(
        root,
        repository
    );

    return plan.packages();
}

/*
 * Legacy overload.
 *
 * Keep the vector-based API source-compatible while making the
 * RepositoryIndex-based resolver the canonical implementation.
 */
std::vector<athena::package::Package> resolve(
    const athena::package::Package& root,
    const std::vector<athena::package::Package>& available
)
{
    athena::repository::RepositoryIndex repository;

    for (const auto& package : available) {
        repository.add(package);
    }

    return resolve(
        root,
        repository
    );
}

}

namespace athena::core {
ResolutionResult resolve_request(const InstallRequest& request)
{
    try {
        return athena::dependency::resolve_request(request).result;
    } catch (const athena::repository::ResolverSnapshotAdapterError& error) {
        using E = athena::repository::ResolverSnapshotAdapterErrorCode;
        ResolutionErrorCode code;
        switch (error.code()) {
        case E::InvalidSnapshot: code = ResolutionErrorCode::InvalidSnapshot; break;
        case E::InvalidCandidate: code = ResolutionErrorCode::InvalidCandidate; break;
        case E::InvalidMetadata: code = ResolutionErrorCode::InvalidDependencyMetadata; break;
        case E::SnapshotMismatch: code = ResolutionErrorCode::CandidateSnapshotMismatch; break;
        }
        throw ResolutionError(code, error.what());
    }
}
}
