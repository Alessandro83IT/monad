#include "resolver.hpp"
#include "environment.hpp"

#include "constraint.hpp"
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

namespace {

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


using Package = athena::package::Package;

struct NegativeDependency {
    std::string name;
    VersionConstraint constraint;
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
struct PackageAssignment {
    Package package;
};

/*
 * Snapshot of the mutable resolution state.
 *
 * A global candidate decision owns one of these snapshots so the solver
 * can return exactly to the point immediately before that decision was
 * made.
 */
struct ResolutionSnapshot {
    std::vector<Package> result;

    std::unordered_map<
        std::string,
        PackageAssignment
    > assignments;

    std::unordered_set<std::string> resolved;
    std::unordered_set<std::string> resolving;
    std::vector<NegativeDependency> negative_dependencies;

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

    /*
     * Semantic dependency contexts associated with selected packages.
     */
    std::unordered_map<
        std::string,
        std::vector<DependencyContext>
    > contexts;

    /*
     * Host/Build/Target environments are fixed for the whole resolution.
     * They are therefore intentionally not part of ResolutionSnapshot:
     * backtracking changes the selected package graph, not the environment
     * against which that graph is evaluated.
     */
    Environments environments;

    /*
     * Global candidate decisions, in dependency-resolution order.
     * Unlike the old candidate history, each decision owns the exact
     * state required to retry that branch.
     */
    std::vector<ResolutionDecision> decisions;
};

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
    for (const auto& package : repository.find(name)) {

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
struct DependencyTarget {
    enum class Kind {
        Package,
        Capability,
        Feature
    };

    Kind kind;
    std::string name;
    VersionConstraint constraint;
};

std::vector<Package> find_dependency_candidates(
    const DependencyTarget& target,
    const athena::repository::RepositoryIndex& repository
)
{
    std::vector<Package> candidates;

    switch (target.kind) {

        case DependencyTarget::Kind::Package:
            candidates = repository.find(target.name);
            break;

        case DependencyTarget::Kind::Capability:
            candidates = repository.find_capability(target.name);
            break;

        case DependencyTarget::Kind::Feature:
            candidates = repository.find_feature(target.name);
            break;
    }

    /*
     * Version constraints currently apply to concrete package
     * references. Capability and feature references select a provider
     * package and therefore do not impose a version constraint yet.
     */
    if (target.kind == DependencyTarget::Kind::Package) {

        candidates.erase(
            std::remove_if(
                candidates.begin(),
                candidates.end(),
                [&target](const Package& package) {
                    return !satisfies(
                        target.constraint,
                        package.version
                    );
                }
            ),
            candidates.end()
        );
    }

    /*
     * Semantic dependencies may have several providers. Select the
     * newest provider first, while keeping package-name ordering
     * deterministic when versions are equal.
     */
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const Package& left, const Package& right) {

            if (left.version != right.version) {
                return athena::version::greater(
                    left.version,
                    right.version
                );
            }

            return left.name < right.name;
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
        state.assignments,
        state.resolved,
        {},
        state.negative_dependencies,
        state.contexts
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
    state.assignments = snapshot.assignments;
    state.resolved = snapshot.resolved;
    state.resolving = snapshot.resolving;
    state.negative_dependencies =
        snapshot.negative_dependencies;

    state.contexts =
        snapshot.contexts;
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

        if (comparison->target->kind !=
            Expression::Kind::Package) {

            throw std::runtime_error(
                "Il confronto di versione deve riferirsi "
                "a un pacchetto"
            );
        }

        const auto* package =
            dynamic_cast<const PackageRef*>(
                comparison->target.get()
            );

        if (package == nullptr) {
            throw std::runtime_error(
                "Riferimento al pacchetto non valido"
            );
        }

        return {
            DependencyTarget::Kind::Package,
            package->name,
            *comparison->constraint
        };
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

bool violates_negative_dependency(
    const Package& package,
    const std::vector<NegativeDependency>& constraints
)
{
    for (const auto& constraint : constraints) {

        if (package.name != constraint.name) {
            continue;
        }

        if (satisfies(constraint.constraint, package.version)) {
            return true;
        }
    }

    return false;
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

void resolve_expression(
    const ExpressionPtr& expression,
    const Package& requiring_package,
    DependencyContext context,
    const athena::repository::RepositoryIndex& repository,
    ResolutionState& state
);

void resolve_package_target(
    const DependencyTarget& target,
    const Package& requiring_package,
    DependencyContext context,
    const athena::repository::RepositoryIndex& repository,
    ResolutionState& state
)
{
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
    if (state.resolving.contains(target.name)) {

        throw std::runtime_error(
            "Ciclo di dipendenze rilevato: " +
            target.name
        );
    }

    if (state.resolved.contains(target.name)) {
        return;
    }

    const auto* existing_assignment =
        find_assignment(
            target.name,
            state
        );

    if (existing_assignment != nullptr) {

        /*
         * The package is assigned but not yet resolved. This happens
         * when global backtracking restores a snapshot taken while an
         * ancestor package was still propagating its dependencies.
         *
         * Re-enter that package so the dependency graph can be replayed
         * until the pending global decision is reached again.
         */
        state.resolving.insert(
            existing_assignment->name
        );

        try {

            for (const auto& dependency :
                 existing_assignment->dependencies) {

                resolve_expression(
                    dependency.expression,
                    *existing_assignment,
                    dependency.context,
                    repository,
                    state
                );
            }

            state.resolving.erase(
                existing_assignment->name
            );

            state.resolved.insert(
                existing_assignment->name
            );

            state.result.push_back(
                *existing_assignment
            );
        }
        catch (...) {

            state.resolving.erase(
                existing_assignment->name
            );

            throw;
        }

        return;
    }

    const auto candidates =
        find_dependency_candidates(
            target,
            repository
        );

    if (candidates.empty()) {

        throw std::runtime_error(
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
    if (!state.decisions.empty() &&
        state.decisions.back().name == target.name &&
        state.decisions.back().candidates.size() == candidates.size()) {

        decision_index =
            state.decisions.size() - 1;
    }
    else if (candidates.size() > 1) {

        state.decisions.push_back({
            target.name,
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

        if (decision_index !=
            std::numeric_limits<std::size_t>::max()) {

            state.decisions.pop_back();
        }

        throw std::runtime_error(
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
                repository,
                state
            );
        }

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
    const athena::repository::RepositoryIndex& repository,
    ResolutionState& state
)
{
    if (!expression) {
        throw std::runtime_error(
            "Espressione di dipendenza nulla"
        );
    }

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

            for (const auto& child :
                 and_expression->expressions) {

                resolve_expression(
                    child,
                    requiring_package,
                    context,
                    repository,
                    state
                );
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

            for (const auto& child :
                 or_expression->expressions) {

                /*
                 * Each OR alternative is attempted transactionally.
                 *
                 * If the alternative fails, all state changes produced
                 * by that attempt are rolled back before trying the
                 * next alternative.
                 */
                const auto result_size = state.result.size();
                const auto assignments_before = state.assignments;
                const auto resolved_before = state.resolved;
                const auto resolving_before = state.resolving;
                const auto negative_dependencies_size =
                    state.negative_dependencies.size();

                try {

                    resolve_expression(
                        child,
                        requiring_package,
                        context,
                        repository,
                        state
                    );

                    return;
                }
                catch (const std::runtime_error& error) {

                    /*
                     * Roll back every change made by the failed
                     * alternative, including packages already resolved,
                     * resolution state, and negative NOT constraints.
                     */
                    state.result.resize(result_size);
                    state.assignments = assignments_before;
                    state.resolved = resolved_before;
                    state.resolving = resolving_before;
                    state.negative_dependencies.resize(
                        negative_dependencies_size
                    );

                    last_error = error.what();
                }
            }

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

            const auto target =
                extract_dependency_target(
                    not_expression->expression
                );

            if (target.kind !=
                DependencyTarget::Kind::Package) {

                throw std::runtime_error(
                    "Il vincolo NOT supporta attualmente "
                    "solo riferimenti a pacchetti"
                );
            }

            for (const auto& package : state.result) {

                if (package.name == target.name &&
                    satisfies(
                        target.constraint,
                        package.version
                    )) {

                    throw std::runtime_error(
                        "Vincolo NOT violato dal pacchetto già "
                        "risolto: " +
                        package.name +
                        " (" +
                        package.version +
                        ")"
                    );
                }
            }

            state.negative_dependencies.push_back({
                target.name,
                target.constraint
            });

            return;
        }

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
                repository,
                state
            );
        }

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
    ResolutionState state;
    state.environments = environments;

    /*
     * The solver repeatedly propagates the dependency graph from the
     * current state. A failure does not restart the search from scratch:
     * the most recent decision restores its exact pre-selection snapshot
     * and advances to the next candidate.
     */
    for (;;) {

        try {

            resolve_recursive(
                root,
                repository,
                state
            );

            /*
             * The root package is part of the final solution just like
             * every resolved dependency. A NOT constraint introduced
             * while resolving the graph must therefore be checked before
             * accepting the solution.
             */
            if (violates_negative_dependency(
                    root,
                    state.negative_dependencies
                )) {

                throw std::runtime_error(
                    "Pacchetto radice vietato dal vincolo NOT: " +
                    root.name +
                    " (" +
                    root.version +
                    ")"
                );
            }

            /*
             * ResolutionState already stores packages in dependency-
             * before-dependent order, so InstallPlan can preserve that
             * ordering directly.
             */
            athena::install::InstallPlan plan;

            for (const auto& package : state.result) {

                const auto context_it =
                    state.contexts.find(package.name);

                if (context_it == state.contexts.end() ||
                    context_it->second.empty()) {

                    /*
                     * This should only be possible for legacy/internal
                     * resolution paths. Keep Target as the historical
                     * default instead of creating an unannotated plan.
                     */
                    plan.add(
                        package,
                        DependencyContext::Target
                    );

                    continue;
                }

                for (const auto context :
                     context_it->second) {

                    plan.add(
                        package,
                        context
                    );
                }
            }

            return plan;
        }
        catch (const std::runtime_error&) {

            /*
             * No decision means that the dependency graph has no
             * remaining alternative branch.
             */
            if (state.decisions.empty()) {
                throw;
            }

            /*
             * The latest decision is the deepest unresolved search
             * branch. Restore exactly the state that existed before its
             * candidate was selected.
             */
            auto& decision =
                state.decisions.back();

            restore_resolution_state(
                state,
                decision.snapshot
            );

            /*
             * Move to the next candidate for this decision.
             */
            ++decision.candidate_index;

            /*
             * All candidates for this branch have been exhausted.
             * Remove the decision and continue backtracking towards the
             * previous branch.
             */
            if (decision.candidate_index >=
                decision.candidates.size()) {

                state.decisions.pop_back();
            }
        }
    }
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
