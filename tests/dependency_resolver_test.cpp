#include "../src/dependency/resolver.hpp"
#include "../src/dependency/constraint.hpp"
#include "../src/repository/repository_index.hpp"
#include "../src/install/install_plan.hpp"

#include <cassert>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using athena::package::Package;
using namespace athena::dependency;

namespace {

Dependency package_dependency(
    const std::string& name
)
{
    return Dependency{
        std::make_shared<const PackageRef>(name),
        DependencyKind::Runtime,
        DependencyContext::Target
    };
}

Dependency context_dependency(
    const std::string& name,
    DependencyContext context
)
{
    return Dependency{
        std::make_shared<const PackageRef>(name),
        DependencyKind::Runtime,
        context
    };
}

Dependency versioned_dependency(
    const std::string& name,
    VersionConstraint constraint
)
{
    auto package =
        std::make_shared<const PackageRef>(name);

    auto version_comparison =
        std::make_shared<const VersionComparison>(
            package,
            std::make_shared<const VersionConstraint>(
                std::move(constraint)
            )
        );

    return Dependency{
        version_comparison,
        DependencyKind::Runtime,
        DependencyContext::Target
    };
}

Dependency and_dependency(
    std::vector<ExpressionPtr> expressions
)
{
    return Dependency{
        std::make_shared<const AndExpression>(
            std::move(expressions)
        ),
        DependencyKind::Runtime,
        DependencyContext::Target
    };
}

Dependency or_dependency(
    std::vector<ExpressionPtr> expressions
)
{
    return Dependency{
        std::make_shared<const OrExpression>(
            std::move(expressions)
        ),
        DependencyKind::Runtime,
        DependencyContext::Target
    };
}

Dependency not_dependency(
    ExpressionPtr expression
)
{
    return Dependency{
        std::make_shared<const NotExpression>(
            std::move(expression)
        ),
        DependencyKind::Runtime,
        DependencyContext::Target
    };
}

ExpressionPtr package_expression(
    const std::string& name
)
{
    return std::make_shared<const PackageRef>(name);
}

ExpressionPtr platform_expression(
    const std::string& name
)
{
    return std::make_shared<const PlatformRef>(name);
}

ExpressionPtr architecture_expression(
    const std::string& name
)
{
    return std::make_shared<const ArchitectureRef>(name);
}

ExpressionPtr abi_expression(
    const std::string& name
)
{
    return std::make_shared<const ABIRef>(name);
}

Dependency environment_dependency(
    ExpressionPtr expression,
    DependencyContext context
)
{
    return Dependency{
        std::move(expression),
        DependencyKind::Runtime,
        context
    };
}

}


void test_capability_reference()
{
    athena::repository::RepositoryIndex repository;

    Package openssl{
        "openssl",
        "3.0",
        "OpenSSL",
        "",
        "",
        "",
        {}
    };

    openssl.provides.capabilities = {"ssl"};

    repository.add(openssl);

    Package application{
        "application",
        "1.0",
        "Application",
        "",
        "",
        "",
        {
            Dependency{
                std::make_shared<const CapabilityRef>("ssl"),
                DependencyKind::Runtime,
                DependencyContext::Target
            }
        }
    };

    const auto result =
        resolve(
            application,
            repository
        );

    assert(result.size() == 2);
    assert(result[0].name == "openssl");
    assert(result[1].name == "application");
}


void test_capability_provider_backtracking()
{
    athena::repository::RepositoryIndex repository;

    Package openssl{
        "openssl",
        "3.0",
        "OpenSSL",
        "",
        "",
        "autotools",
        {}
    };

    openssl.provides.capabilities = {"ssl"};

    Package libressl{
        "libressl",
        "3.9",
        "LibreSSL",
        "",
        "",
        "autotools",
        {}
    };

    libressl.provides.capabilities = {"ssl"};

    Package no_libressl{
        "no-libressl",
        "1.0",
        "Package that rejects LibreSSL",
        "",
        "",
        "autotools",
        {
            not_dependency(
                package_expression("libressl")
            )
        }
    };

    Package application{
        "provider-backtracking-app",
        "1.0",
        "Tests semantic provider backtracking",
        "",
        "",
        "autotools",
        {
            Dependency{
                std::make_shared<const CapabilityRef>("ssl"),
                DependencyKind::Runtime,
                DependencyContext::Target
            },
            package_dependency("no-libressl")
        }
    };

    repository.add(openssl);
    repository.add(libressl);
    repository.add(no_libressl);

    const auto result =
        athena::dependency::resolve(
            application,
            repository
        );

    /*
     * LibreSSL is initially preferred because version 3.9 is newer.
     * no-libressl then makes that choice invalid. The resolver must
     * globally backtrack and select OpenSSL 3.0 instead.
     */
    assert(result.size() == 3);
    assert(result[0].name == "openssl");
    assert(result[0].version == "3.0");
    assert(result[1].name == "no-libressl");
    assert(result[2].name == "provider-backtracking-app");
}

void test_feature_reference()
{
    athena::repository::RepositoryIndex repository;

    Package cmake{
        "cmake",
        "4.0",
        "CMake",
        "",
        "",
        "",
        {}
    };

    cmake.provides.features = {"modern-cmake"};

    repository.add(cmake);

    Package application{
        "application",
        "1.0",
        "Application",
        "",
        "",
        "",
        {
            Dependency{
                std::make_shared<const FeatureRef>("modern-cmake"),
                DependencyKind::Build,
                DependencyContext::Build
            }
        }
    };

    const auto result =
        resolve(
            application,
            repository
        );

    assert(result.size() == 2);
    assert(result[0].name == "cmake");
    assert(result[1].name == "application");
}

int main()
{
    test_capability_reference();
    test_feature_reference();
    test_capability_provider_backtracking();

    Package zlib_old{
        "zlib","1.0","Zlib","","","autotools",{}
    };

    Package zlib_mid{
        "zlib","1.8","Zlib","","","autotools",{}
    };

    Package zlib_new{
        "zlib","2.0","Zlib","","","autotools",{}
    };

    Package openssl{
        "openssl","3.0","OpenSSL","","","autotools",{}
    };

    Package curl{
        "curl","8.0","Curl","","","autotools",{}
    };

    Package libfoo{
        "libfoo","1.0","Libfoo","","","autotools",
        { package_dependency("zlib") }
    };

    Package app{
        "app","1.0","Test application","","","autotools",
        { package_dependency("libfoo") }
    };

    const std::vector<Package> available{
        zlib_old,
        zlib_mid,
        zlib_new,
        openssl,
        curl,
        libfoo
    };

    /*
     * High-level repository used by the public Repository-based resolver API.
     *
     * Repository owns package indexing while the resolver remains
     * responsible for selecting compatible versions.
     */
    athena::repository::Repository repository;

    for (const auto& package : available) {
        /*
         * The test data is already represented as Package objects, so we
         * populate the repository through its underlying index here.
         * Package-file loading is covered separately by repository_test.
         */
        repository.add(package);
    }

    /*
     * Basic dependency resolution through the high-level Repository API.
     */
    const auto result =
        athena::dependency::resolve(app, repository);

    assert(result.size() == 3);
    assert(result[0].name == "zlib");
    assert(result[0].version == "2.0");
    assert(result[1].name == "libfoo");
    assert(result[1].version == "1.0");
    assert(result[2].name == "app");
    assert(result[2].version == "1.0");

    /*
     * InstallPlan integration.
     *
     * Dependency resolution now exposes InstallPlan as the canonical
     * bridge to the installation layer. The plan must preserve exactly
     * the dependency-before-dependent order returned by resolve().
     */
    const auto plan =
        athena::dependency::resolve_plan(
            app,
            repository
        );

    assert(!plan.empty());
    assert(plan.size() == result.size());

    const auto& planned_packages = plan.packages();

    for (std::size_t i = 0; i < result.size(); ++i) {
        assert(
            planned_packages[i].name ==
            result[i].name
        );

        assert(
            planned_packages[i].version ==
            result[i].version
        );
    }

    /*
     * Version constraint.
     */
    Package constrained_app{
        "constrained-app","1.0","Constrained application","","","autotools",
        {
            versioned_dependency(
                "zlib",
                VersionConstraint::comparison(
                    ComparisonOperator::GreaterEqual,
                    "1.5"
                )
            )
        }
    };

    const auto constrained_result =
        athena::dependency::resolve(
            constrained_app,
            available
        );

    assert(constrained_result.size() == 2);
    assert(constrained_result[0].name == "zlib");
    assert(constrained_result[0].version == "2.0");
    assert(constrained_result[1].name == "constrained-app");
    assert(constrained_result[1].version == "1.0");

    /*
     * Version range.
     */
    Package ranged_app{
        "ranged-app","1.0","Ranged application","","","autotools",
        {
            versioned_dependency(
                "zlib",
                VersionConstraint::all({
                    VersionConstraint::comparison(
                        ComparisonOperator::GreaterEqual,
                        "1.5"
                    ),
                    VersionConstraint::comparison(
                        ComparisonOperator::Less,
                        "2.0"
                    )
                })
            )
        }
    };

    const auto ranged_result =
        athena::dependency::resolve(
            ranged_app,
            available
        );

    assert(ranged_result.size() == 2);
    assert(ranged_result[0].name == "zlib");
    assert(ranged_result[0].version == "1.8");
    assert(ranged_result[1].name == "ranged-app");
    assert(ranged_result[1].version == "1.0");

    /*
     * Impossible version constraint.
     */
    Package impossible_app{
        "impossible-app","1.0","Impossible application","","","autotools",
        {
            versioned_dependency(
                "zlib",
                VersionConstraint::comparison(
                    ComparisonOperator::GreaterEqual,
                    "3.0"
                )
            )
        }
    };

    bool incompatible_dependency_detected = false;

    try {
        athena::dependency::resolve(
            impossible_app,
            available
        );
    }
    catch (const std::runtime_error&) {
        incompatible_dependency_detected = true;
    }

    assert(incompatible_dependency_detected);

    /*
     * Missing dependency.
     */
    Package broken{
        "broken","1.0","Broken package","","","autotools",
        {
            package_dependency("missing")
        }
    };

    bool missing_dependency_detected = false;

    try {
        athena::dependency::resolve(
            broken,
            available
        );
    }
    catch (const std::runtime_error&) {
        missing_dependency_detected = true;
    }

    assert(missing_dependency_detected);

    /*
     * Dependency cycle.
     */
    Package cycle_a{
        "cycle-a","1.0","Cycle A","","","autotools",
        {
            package_dependency("cycle-b")
        }
    };

    Package cycle_b{
        "cycle-b","1.0","Cycle B","","","autotools",
        {
            package_dependency("cycle-a")
        }
    };

    const std::vector<Package> cyclic_packages{
        cycle_a,
        cycle_b
    };

    bool cycle_detected = false;

    try {
        athena::dependency::resolve(
            cycle_a,
            cyclic_packages
        );
    }
    catch (const std::runtime_error&) {
        cycle_detected = true;
    }

    assert(cycle_detected);

    /*
     * Logical AND.
     *
     * app-and depends on both zlib and openssl.
     */
    Package app_and{
        "app-and","1.0","AND application","","","autotools",
        {
            and_dependency({
                package_expression("zlib"),
                package_expression("openssl")
            })
        }
    };

    const auto and_result =
        athena::dependency::resolve(
            app_and,
            available
        );

    assert(and_result.size() == 3);
    assert(and_result[0].name == "zlib");
    assert(and_result[0].version == "2.0");
    assert(and_result[1].name == "openssl");
    assert(and_result[1].version == "3.0");
    assert(and_result[2].name == "app-and");

    /*
     * Logical OR.
     *
     * app-or depends on either zlib or openssl.
     * The first alternative is resolvable and therefore succeeds.
     */
    Package app_or{
        "app-or","1.0","OR application","","","autotools",
        {
            or_dependency({
                package_expression("zlib"),
                package_expression("openssl")
            })
        }
    };

    const auto or_result =
        athena::dependency::resolve(
            app_or,
            available
        );

    assert(or_result.size() == 2);
    assert(or_result[0].name == "zlib");
    assert(or_result[0].version == "2.0");
    assert(or_result[1].name == "app-or");

    /*
     * Logical OR with a failed first alternative.
     *
     * broken-lib exists, but its own dependency is missing.
     * The resolver must therefore try openssl.
     */
    Package broken_lib{
        "broken-lib","1.0","Broken library","","","autotools",
        {
            package_dependency("missing")
        }
    };

    Package fallback_app{
        "fallback-app","1.0","Fallback application","","","autotools",
        {
            or_dependency({
                package_expression("broken-lib"),
                package_expression("openssl")
            })
        }
    };

    const std::vector<Package> fallback_available{
        broken_lib,
        openssl
    };

    const auto fallback_result =
        athena::dependency::resolve(
            fallback_app,
            fallback_available
        );

    assert(fallback_result.size() == 2);
    assert(fallback_result[0].name == "openssl");
    assert(fallback_result[0].version == "3.0");
    assert(fallback_result[1].name == "fallback-app");

    /*
     * Transactional OR rollback.
     *
     * broken-top successfully resolves good-leaf first, but then fails
     * on the missing dependency. The successful part of the failed
     * alternative must be completely rolled back before openssl is tried.
     */
    Package good_leaf{
        "good-leaf","1.0","Good leaf","","","autotools",
        {}
    };

    Package broken_top{
        "broken-top","1.0","Broken top-level package","","","autotools",
        {
            package_dependency("good-leaf"),
            package_dependency("missing")
        }
    };

    Package rollback_app{
        "rollback-app","1.0","Rollback application","","","autotools",
        {
            or_dependency({
                package_expression("broken-top"),
                package_expression("openssl")
            })
        }
    };

    const std::vector<Package> rollback_available{
        broken_top,
        good_leaf,
        openssl
    };

    const auto rollback_result =
        athena::dependency::resolve(
            rollback_app,
            rollback_available
        );

    assert(rollback_result.size() == 2);
    assert(rollback_result[0].name == "openssl");
    assert(rollback_result[0].version == "3.0");
    assert(rollback_result[1].name == "rollback-app");

    /*
     * A failed OR alternative may introduce a negative NOT constraint
     * before failing. That constraint must be rolled back so that the
     * following alternative starts from the original resolution state.
     */
    Package not_then_missing{
        "not-then-missing","1.0",
        "Introduces NOT zlib and then fails","","","autotools",
        {
            not_dependency(
                package_expression("zlib")
            ),
            package_dependency("missing-package")
        }
    };

    Package zlib_alternative{
        "zlib","3.0","zlib alternative","","","autotools",
        {}
    };

    Package or_not_rollback_app{
        "or-not-rollback-app","1.0",
        "Tests NOT rollback in OR","","","autotools",
        {
            /*
             * The first alternative resolves not-then-missing. That
             * package introduces NOT zlib and then fails on its missing
             * dependency, so its negative constraint must be rolled back.
             */
            or_dependency({
                package_expression("not-then-missing"),
                package_expression("zlib")
            })
        }
    };

    const std::vector<Package> or_not_rollback_available{
        not_then_missing,
        zlib_alternative
    };

    const auto or_not_rollback_result =
        athena::dependency::resolve(
            or_not_rollback_app,
            or_not_rollback_available
        );

    /*
     * The first alternative fails because missing-package is unavailable.
     * Its NOT zlib constraint must be discarded, allowing the second
     * alternative to resolve zlib successfully.
     */
    assert(or_not_rollback_result.size() == 2);
    assert(or_not_rollback_result[0].name == "zlib");
    assert(or_not_rollback_result[0].version == "3.0");
    assert(or_not_rollback_result[1].name == "or-not-rollback-app");

    /*
     * NOT succeeds when the excluded package is not present in the
     * final dependency graph.
     */
    Package not_app{
        "not-app","1.0","NOT application","","","autotools",
        {
            not_dependency(
                package_expression("zlib")
            )
        }
    };

    const auto not_result =
        athena::dependency::resolve(
            not_app,
            available
        );

    assert(not_result.size() == 1);
    assert(not_result[0].name == "not-app");

    /*
     * NOT must also reject a package that is introduced indirectly
     * by another dependency.
     */
    Package zlib_user{
        "zlib-user","1.0","Uses zlib","","","autotools",
        {
            package_dependency("zlib")
        }
    };

    Package conflicting_not_app{
        "conflicting-not-app","1.0",
        "NOT zlib but zlib is required","","","autotools",
        {
            package_dependency("zlib-user"),
            not_dependency(
                package_expression("zlib")
            )
        }
    };

    bool not_conflict_detected = false;

    try {
        athena::dependency::resolve(
            conflicting_not_app,
            available
        );
    }
    catch (const std::runtime_error&) {
        not_conflict_detected = true;
    }

    assert(not_conflict_detected);

    /*
     * A version-qualified NOT excludes only versions satisfying its
     * constraint. Therefore zlib 1.8 is allowed by NOT zlib >= 2.0.
     */
    Package not_new_zlib{
        "not-new-zlib","1.0",
        "Rejects zlib >= 2.0","","","autotools",
        {
            /*
             * The package explicitly requires zlib while simultaneously
             * forbidding zlib versions >= 2.0. This makes the NOT
             * constraint observable: zlib 1.8 must succeed, while
             * zlib 2.0 must make resolution fail.
             */
            package_dependency("zlib"),
            not_dependency(
                std::make_shared<const VersionComparison>(
                    package_expression("zlib"),
                    std::make_shared<const VersionConstraint>(
                        VersionConstraint::comparison(
                            ComparisonOperator::GreaterEqual,
                            "2.0"
                        )
                    )
                )
            )
        }
    };

    std::vector<Package> old_zlib_available{
        Package{
            "zlib","1.8","zlib old","","","autotools",
            {}
        }
    };

    const auto old_zlib_result =
        athena::dependency::resolve(
            not_new_zlib,
            old_zlib_available
        );

    /*
     * zlib 1.8 satisfies the positive dependency and is not excluded
     * by NOT zlib >= 2.0, so both packages belong to the result.
     */
    assert(old_zlib_result.size() == 2);
    assert(old_zlib_result[0].name == "zlib");
    assert(old_zlib_result[0].version == "1.8");
    assert(old_zlib_result[1].name == "not-new-zlib");

    /*
     * The same NOT constraint must reject zlib 2.0 or newer.
     */
    std::vector<Package> new_zlib_available{
        Package{
            "zlib","2.0","zlib new","","","autotools",
            {}
        }
    };

    bool versioned_not_conflict_detected = false;

    try {
        athena::dependency::resolve(
            not_new_zlib,
            new_zlib_available
        );
    }
    catch (const std::runtime_error&) {
        versioned_not_conflict_detected = true;
    }

    assert(versioned_not_conflict_detected);

    /*
     * Global version conflict.
     *
     * strict-high selects the newest zlib version satisfying >= 2.0.
     * strict-low then requires zlib < 2.0. The resolver must detect
     * that the already selected zlib 2.0 cannot satisfy both
     * dependency constraints.
     */
    Package strict_high{
        "strict-high","1.0",
        "Requires zlib >= 2.0","","","autotools",
        {
            versioned_dependency(
                "zlib",
                VersionConstraint::comparison(
                    ComparisonOperator::GreaterEqual,
                    "2.0"
                )
            )
        }
    };

    Package strict_low{
        "strict-low","1.0",
        "Requires zlib < 2.0","","","autotools",
        {
            versioned_dependency(
                "zlib",
                VersionConstraint::comparison(
                    ComparisonOperator::Less,
                    "2.0"
                )
            )
        }
    };

    Package global_conflict_app{
        "global-conflict-app","1.0",
        "Conflicting global zlib constraints","","","autotools",
        {
            package_dependency("strict-high"),
            package_dependency("strict-low")
        }
    };

    const std::vector<Package> global_conflict_available{
        Package{
            "zlib","1.8","zlib old","","","autotools",
            {}
        },
        Package{
            "zlib","2.0","zlib new","","","autotools",
            {}
        },
        strict_high,
        strict_low
    };

    bool global_version_conflict_detected = false;

    try {
        athena::dependency::resolve(
            global_conflict_app,
            global_conflict_available
        );
    }
    catch (const std::runtime_error&) {
        global_version_conflict_detected = true;
    }

    assert(global_version_conflict_detected);

    /*
     * Global candidate backtracking.
     *
     * The resolver initially prefers zlib 2.0 because it is the newest
     * candidate. strict-high accepts it, but strict-low requires a
     * version below 2.0. The solver must therefore backtrack globally
     * and retry the earlier zlib decision with version 1.8.
     */
    Package global_backtracking_high{
        "global-backtracking-high","1.0",
        "Requires zlib >= 1.8","","","autotools",
        {
            versioned_dependency(
                "zlib",
                VersionConstraint::comparison(
                    ComparisonOperator::GreaterEqual,
                    "1.8"
                )
            )
        }
    };

    Package global_backtracking_low{
        "global-backtracking-low","1.0",
        "Requires zlib < 2.0","","","autotools",
        {
            versioned_dependency(
                "zlib",
                VersionConstraint::comparison(
                    ComparisonOperator::Less,
                    "2.0"
                )
            )
        }
    };

    Package global_backtracking_app{
        "global-backtracking-app","1.0",
        "Tests global candidate backtracking","","","autotools",
        {
            package_dependency("global-backtracking-high"),
            package_dependency("global-backtracking-low")
        }
    };

    const std::vector<Package> global_backtracking_available{
        Package{
            "zlib","1.8","zlib old","","","autotools",
            {}
        },
        Package{
            "zlib","2.0","zlib new","","","autotools",
            {}
        },
        global_backtracking_high,
        global_backtracking_low,
        global_backtracking_app
    };

    const auto global_backtracking_result =
        athena::dependency::resolve(
            global_backtracking_app,
            global_backtracking_available
        );

    bool zlib_old_selected = false;
    bool zlib_new_selected = false;

    for (const auto& package : global_backtracking_result) {
        if (package.name == "zlib" &&
            package.version == "1.8") {
            zlib_old_selected = true;
        }

        if (package.name == "zlib" &&
            package.version == "2.0") {
            zlib_new_selected = true;
        }
    }

    assert(zlib_old_selected);
    assert(!zlib_new_selected);

    /*
     * Local candidate backtracking.
     *
     * foo 2.0 is preferred because it is the newest candidate, but
     * its dependency is unavailable. The resolver must rollback the
     * failed candidate and retry foo 1.0.
     */
    Package foo_old{
        "foo","1.0",
        "Working foo candidate","","","autotools",
        {}
    };

    Package foo_new{
        "foo","2.0",
        "Broken foo candidate","","","autotools",
        {
            package_dependency("missing-package")
        }
    };

    Package local_backtracking_app{
        "local-backtracking-app","1.0",
        "Tests local candidate backtracking","","","autotools",
        {
            package_dependency("foo")
        }
    };

    const std::vector<Package> local_backtracking_available{
        foo_old,
        foo_new,
        local_backtracking_app
    };

    const auto local_backtracking_result =
        athena::dependency::resolve(
            local_backtracking_app,
            local_backtracking_available
        );

    bool foo_old_selected = false;
    bool foo_new_selected = false;

    for (const auto& package : local_backtracking_result) {

        if (package.name == "foo" &&
            package.version == "1.0") {

            foo_old_selected = true;
        }

        if (package.name == "foo" &&
            package.version == "2.0") {

            foo_new_selected = true;
        }
    }

    assert(foo_old_selected);
    assert(!foo_new_selected);

    /*
     * The root package is part of the final solution.
     * Therefore a NOT constraint introduced by one of its dependencies
     * must also be able to reject the root package.
     */
    Package root_not_app{
        "root-not-app","1.0",
        "Root excluded by its own NOT constraint",
        "","","autotools",
        {
            not_dependency(
                package_expression("root-not-app")
            )
        }
    };

    bool root_not_conflict_detected = false;

    try {
        athena::dependency::resolve(
            root_not_app,
            available
        );
    }
    catch (const std::runtime_error&) {
        root_not_conflict_detected = true;
    }

    assert(root_not_conflict_detected);

    /*
     * Dependency contexts.
     *
     * Host, Build and Target are semantic properties of the dependency
     * edge and must survive resolution into the InstallPlan.
     */
    Package host_tool{
        "host-tool","1.0","Host dependency","","","autotools",{}
    };

    Package build_tool{
        "build-tool","1.0","Build dependency","","","autotools",{}
    };

    Package target_library{
        "target-library","1.0","Target dependency","","","autotools",{}
    };

    Package contextual_app{
        "contextual-app","1.0",
        "Tests Host/Build/Target dependency contexts",
        "","","autotools",
        {
            context_dependency(
                "host-tool",
                DependencyContext::Host
            ),
            context_dependency(
                "build-tool",
                DependencyContext::Build
            ),
            context_dependency(
                "target-library",
                DependencyContext::Target
            )
        }
    };

    athena::repository::Repository context_repository;

    context_repository.add(host_tool);
    context_repository.add(build_tool);
    context_repository.add(target_library);
    context_repository.add(contextual_app);

    const auto contextual_plan =
        athena::dependency::resolve_plan(
            contextual_app,
            context_repository
        );

    assert(contextual_plan.size() == 4);
    assert(contextual_plan.contexts().size() == 4);

    const auto& contextual_packages =
        contextual_plan.packages();

    const auto& contextual_contexts =
        contextual_plan.contexts();

    bool host_context_ok = false;
    bool build_context_ok = false;
    bool target_context_ok = false;
    bool root_context_ok = false;

    for (std::size_t i = 0;
         i < contextual_packages.size();
         ++i) {

        const auto& package = contextual_packages[i];
        const auto& contexts = contextual_contexts[i];

        if (package.name == "host-tool") {
            assert(contexts.size() == 1);
            assert(contexts[0] == DependencyContext::Host);
            host_context_ok = true;
        }

        if (package.name == "build-tool") {
            assert(contexts.size() == 1);
            assert(contexts[0] == DependencyContext::Build);
            build_context_ok = true;
        }

        if (package.name == "target-library") {
            assert(contexts.size() == 1);
            assert(contexts[0] == DependencyContext::Target);
            target_context_ok = true;
        }

        if (package.name == "contextual-app") {
            assert(contexts.size() == 1);
            assert(contexts[0] == DependencyContext::Target);
            root_context_ok = true;
        }
    }

    assert(host_context_ok);
    assert(build_context_ok);
    assert(target_context_ok);
    assert(root_context_ok);

    /*
     * A single package can be required in more than one context.
     *
     * It must appear only once in the InstallPlan while retaining every
     * context in which it is needed.
     */
    Package shared_tool{
        "shared-tool","1.0",
        "Shared Build/Target dependency","","","autotools",{}
    };

    Package multi_context_app{
        "multi-context-app","1.0",
        "Tests multiple contexts for one package",
        "","","autotools",
        {
            context_dependency(
                "shared-tool",
                DependencyContext::Build
            ),
            context_dependency(
                "shared-tool",
                DependencyContext::Target
            )
        }
    };

    athena::repository::Repository multi_context_repository;

    multi_context_repository.add(shared_tool);
    multi_context_repository.add(multi_context_app);

    const auto multi_context_plan =
        athena::dependency::resolve_plan(
            multi_context_app,
            multi_context_repository
        );

    assert(multi_context_plan.size() == 2);
    assert(multi_context_plan.contexts().size() == 2);

    const auto& multi_context_packages =
        multi_context_plan.packages();

    const auto& multi_context_contexts =
        multi_context_plan.contexts();

    bool shared_tool_found = false;
    bool shared_build_found = false;
    bool shared_target_found = false;

    for (std::size_t i = 0;
         i < multi_context_packages.size();
         ++i) {

        const auto& package = multi_context_packages[i];
        const auto& contexts = multi_context_contexts[i];

        if (package.name == "shared-tool") {
            shared_tool_found = true;

            assert(contexts.size() == 2);

            for (const auto context : contexts) {
                if (context == DependencyContext::Build) {
                    shared_build_found = true;
                }

                if (context == DependencyContext::Target) {
                    shared_target_found = true;
                }
            }
        }

        if (package.name == "multi-context-app") {
            assert(contexts.size() == 1);
            assert(contexts[0] == DependencyContext::Target);
        }
    }

    assert(shared_tool_found);
    assert(shared_build_found);
    assert(shared_target_found);

    /*
     * Verify that Platform / Architecture / ABI selectors are evaluated
     * against the environment associated with DependencyContext.
     *
     * This deliberately uses different Host/Build/Target architectures:
     *
     *   Host   = linux / x86_64 / glibc
     *   Build  = linux / x86_64 / glibc
     *   Target = linux / aarch64 / glibc
     *
     * A selector must therefore never accidentally evaluate against the
     * machine running Athena when its context is Build or Target.
     */
    athena::dependency::Environments cross_environment{
        athena::dependency::Environment{
            "linux",
            "x86_64",
            "glibc"
        },
        athena::dependency::Environment{
            "linux",
            "x86_64",
            "glibc"
        },
        athena::dependency::Environment{
            "linux",
            "aarch64",
            "glibc"
        }
    };

    Package environment_test{
        "environment-test",
        "1.0",
        "Tests platform architecture and ABI selectors",
        "",
        "",
        "autotools",
        {
            environment_dependency(
                platform_expression("linux"),
                DependencyContext::Target
            ),
            environment_dependency(
                architecture_expression("x86_64"),
                DependencyContext::Host
            ),
            environment_dependency(
                architecture_expression("x86_64"),
                DependencyContext::Build
            ),
            environment_dependency(
                architecture_expression("aarch64"),
                DependencyContext::Target
            ),
            environment_dependency(
                abi_expression("glibc"),
                DependencyContext::Host
            ),
            environment_dependency(
                abi_expression("glibc"),
                DependencyContext::Build
            ),
            environment_dependency(
                abi_expression("glibc"),
                DependencyContext::Target
            )
        }
    };

    athena::repository::Repository environment_repository;
    environment_repository.add(environment_test);

    const auto environment_plan =
        athena::dependency::resolve_plan(
            environment_test,
            environment_repository.index(),
            cross_environment
        );

    assert(environment_plan.size() == 1);
    assert(
        environment_plan.packages()[0].name ==
        "environment-test"
    );

    /*
     * The target environment deliberately differs from Host/Build.
     * This dependency must fail if Target is incorrectly evaluated as
     * x86_64 instead of aarch64.
     */
    Package wrong_target_architecture{
        "wrong-target-architecture",
        "1.0",
        "Must reject an architecture absent from Target",
        "",
        "",
        "autotools",
        {
            environment_dependency(
                architecture_expression("x86_64"),
                DependencyContext::Target
            )
        }
    };

    athena::repository::Repository wrong_target_repository;
    wrong_target_repository.add(wrong_target_architecture);

    bool target_architecture_rejected = false;

    try {
        athena::dependency::resolve_plan(
            wrong_target_architecture,
            wrong_target_repository.index(),
            cross_environment
        );
    }
    catch (const std::runtime_error&) {
        target_architecture_rejected = true;
    }

    assert(target_architecture_rejected);

    /*
     * The Build environment is x86_64 while Target is aarch64.
     * This verifies that the same selector produces different results
     * depending on DependencyContext.
     */
    Package build_architecture_test{
        "build-architecture-test",
        "1.0",
        "Build architecture must use Build environment",
        "",
        "",
        "autotools",
        {
            environment_dependency(
                architecture_expression("x86_64"),
                DependencyContext::Build
            ),
            environment_dependency(
                architecture_expression("aarch64"),
                DependencyContext::Target
            )
        }
    };

    athena::repository::Repository build_architecture_repository;
    build_architecture_repository.add(build_architecture_test);

    const auto build_architecture_plan =
        athena::dependency::resolve_plan(
            build_architecture_test,
            build_architecture_repository.index(),
            cross_environment
        );

    assert(build_architecture_plan.size() == 1);

    /*
     * ABI is currently represented as a string selector. Verify that it
     * is evaluated independently for each environment.
     */
    Package abi_test{
        "abi-test",
        "1.0",
        "Tests ABI environment selector",
        "",
        "",
        "autotools",
        {
            environment_dependency(
                abi_expression("glibc"),
                DependencyContext::Target
            )
        }
    };

    athena::repository::Repository abi_repository;
    abi_repository.add(abi_test);

    const auto abi_plan =
        athena::dependency::resolve_plan(
            abi_test,
            abi_repository.index(),
            cross_environment
        );

    assert(abi_plan.size() == 1);

    Package wrong_abi{
        "wrong-abi",
        "1.0",
        "Must reject an ABI absent from Target",
        "",
        "",
        "autotools",
        {
            environment_dependency(
                abi_expression("musl"),
                DependencyContext::Target
            )
        }
    };

    athena::repository::Repository wrong_abi_repository;
    wrong_abi_repository.add(wrong_abi);

    bool abi_rejected = false;

    try {
        athena::dependency::resolve_plan(
            wrong_abi,
            wrong_abi_repository.index(),
            cross_environment
        );
    }
    catch (const std::runtime_error&) {
        abi_rejected = true;
    }

    assert(abi_rejected);

    std::cout << "Dependency resolver tests passed.\n";

    return 0;
}
