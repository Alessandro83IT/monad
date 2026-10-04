#pragma once

#include "../package/package.hpp"
#include "../repository/repository.hpp"
#include "../repository/repository_index.hpp"
#include "../install/install_plan.hpp"
#include "environment.hpp"

#include <vector>

namespace athena::dependency {

/*
 * Resolve a package and produce an InstallPlan.
 *
 * InstallPlan is the boundary between dependency resolution and the
 * installation layer: the resolver determines the complete package
 * solution and preserves dependency-before-dependent ordering, while
 * the installation layer decides how that plan is executed.
 */
athena::install::InstallPlan resolve_plan(
    const athena::package::Package& root,
    const athena::repository::RepositoryIndex& repository
);

/*
 * Resolve a package using explicit Host/Build/Target environments.
 *
 * Platform, architecture and ABI dependency selectors are evaluated
 * against the environment associated with their DependencyContext.
 */
athena::install::InstallPlan resolve_plan(
    const athena::package::Package& root,
    const athena::repository::RepositoryIndex& repository,
    const Environments& environments
);

/*
 * Resolve a package using a high-level Repository and produce an
 * InstallPlan.
 */
athena::install::InstallPlan resolve_plan(
    const athena::package::Package& root,
    const athena::repository::Repository& repository
);

/*
 * Resolve the dependencies of a package.
 *
 * The resolver receives the package to resolve and a collection
 * of available package definitions.
 *
 * The returned vector contains the packages required by the root
 * package, including the root package itself.
 */
/*
 * Resolve dependencies using a repository index.
 *
 * The repository is responsible for providing all available versions
 * of a package; the resolver is responsible for selecting a compatible
 * solution.
 */
std::vector<athena::package::Package> resolve(
    const athena::package::Package& root,
    const athena::repository::RepositoryIndex& repository
);

/*
 * Resolve dependencies using explicit Host/Build/Target environments.
 */
std::vector<athena::package::Package> resolve(
    const athena::package::Package& root,
    const athena::repository::RepositoryIndex& repository,
    const Environments& environments
);

/*
 * Resolve dependencies using a high-level Repository.
 *
 * Repository owns package loading and repository indexing; the resolver
 * only consumes its index when selecting compatible package versions.
 */
std::vector<athena::package::Package> resolve(
    const athena::package::Package& root,
    const athena::repository::Repository& repository
);


/*
 * Compatibility overload for callers that still provide a raw package
 * collection. The implementation converts the collection into a
 * RepositoryIndex and delegates to the repository-based resolver.
 */
std::vector<athena::package::Package> resolve(
    const athena::package::Package& root,
    const std::vector<athena::package::Package>& available
);

}
