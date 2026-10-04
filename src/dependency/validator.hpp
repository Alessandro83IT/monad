#pragma once

#include "installed_environment.hpp"

#include <memory>

namespace athena::dependency {

/*
 * Validates dependency expressions against the packages that are
 * already present in an environment.
 *
 * This is intentionally separate from DependencyResolver:
 *
 *   Resolver  -> chooses packages from a repository.
 *   Validator -> checks packages already present.
 *
 * Removal uses the validator because it must reject an environment
 * that would become inconsistent after a package is removed.
 */
class DependencyValidator {
public:
    explicit DependencyValidator(
        const InstalledEnvironment& environment
    );

    bool satisfies(
        const ExpressionPtr& expression
    ) const;

    void validate() const;

private:
    const InstalledEnvironment& environment_;

    bool satisfies_expression(
        const ExpressionPtr& expression
    ) const;
};

} // namespace athena::dependency
