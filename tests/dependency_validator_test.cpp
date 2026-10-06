#include "../src/dependency/parser.hpp"
#include "../src/dependency/validator.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(
    bool condition,
    const std::string& message
)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

athena::package::Package make_package(
    const std::string& name,
    const std::string& version,
    std::vector<athena::dependency::Dependency> dependencies = {}
)
{
    return athena::package::Package{
        name,
        version,
        "Dependency validator test package",
        "test://source",
        "test-sha256",
        "test",
        std::move(dependencies)
    };
}

void expect_valid(
    const std::vector<athena::package::Package>& packages
)
{
    const athena::dependency::InstalledEnvironment environment(
        packages
    );

    const athena::dependency::DependencyValidator validator(
        environment
    );

    validator.validate();
}

void expect_invalid(
    const std::vector<athena::package::Package>& packages
)
{
    const athena::dependency::InstalledEnvironment environment(
        packages
    );

    const athena::dependency::DependencyValidator validator(
        environment
    );

    bool rejected = false;

    try {
        validator.validate();
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    require(
        rejected,
        "Expected dependency validator to reject environment"
    );
}

}

int main()
{
    using athena::dependency::parse_dependency;

    /*
     * A basic dependency is valid when the required package exists.
     */
    expect_valid({
        make_package("A", "1.0", {
            parse_dependency("B")
        }),
        make_package("B", "1.0")
    });

    /*
     * Removing B from an A -> B environment must be rejected.
     */
    expect_invalid({
        make_package("A", "1.0", {
            parse_dependency("B")
        })
    });

    /*
     * Version constraints are evaluated against the installed
     * package version rather than merely checking package presence.
     */
    expect_valid({
        make_package("A", "1.0", {
            parse_dependency("B >= 2.0")
        }),
        make_package("B", "2.1")
    });

    expect_invalid({
        make_package("A", "1.0", {
            parse_dependency("B >= 2.0")
        }),
        make_package("B", "1.9")
    });

    /*
     * OR is satisfied when at least one branch exists.
     */
    expect_valid({
        make_package("A", "1.0", {
            parse_dependency("B OR C")
        }),
        make_package("C", "1.0")
    });

    expect_invalid({
        make_package("A", "1.0", {
            parse_dependency("B OR C")
        })
    });

    /*
     * AND requires every branch to be satisfied.
     */
    expect_valid({
        make_package("A", "1.0", {
            parse_dependency("B AND C")
        }),
        make_package("B", "1.0"),
        make_package("C", "1.0")
    });

    expect_invalid({
        make_package("A", "1.0", {
            parse_dependency("B AND C")
        }),
        make_package("B", "1.0")
    });

    /*
     * NOT is evaluated against the candidate environment.
     */
    expect_valid({
        make_package("A", "1.0", {
            parse_dependency("NOT B")
        })
    });

    expect_invalid({
        make_package("A", "1.0", {
            parse_dependency("NOT B")
        }),
        make_package("B", "1.0")
    });

    /*
     * An empty environment is valid because there are no packages
     * whose dependencies could be violated.
     */
    expect_valid({});

    /*
     * Capability expressions exist in the semantic model, but the
     * current InstalledEnvironment has no capability database.
     * Unsupported requirements therefore fail closed.
     */
    {
        const athena::dependency::InstalledEnvironment environment({
            make_package("A", "1.0")
        });

        const athena::dependency::DependencyValidator validator(
            environment
        );

        const auto capability =
            std::make_shared<
                const athena::dependency::CapabilityRef
            >("compression");

        bool rejected = false;
        try { (void)validator.satisfies(std::make_shared<const athena::dependency::NotExpression>(capability)); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "Unsupported capability under NOT must fail closed");
    }

    std::cout
        << "dependency_validator_test: all checks passed\n";

    return 0;
}
