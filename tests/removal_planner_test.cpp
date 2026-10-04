#include "../src/install/removal_planner.hpp"

#include "../src/dependency/parser.hpp"

#include <iostream>
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
        "Removal planner test package",
        "test://source",
        "test-sha256",
        "test",
        std::move(dependencies)
    };
}

void expect_invalid(
    const athena::install::RemovalPlanner& planner,
    const std::vector<std::string>& package_names
)
{
    bool rejected = false;

    try {
        (void)planner.plan(package_names);
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    require(
        rejected,
        "Expected removal plan to be rejected"
    );
}

} // namespace

int main()
{
    using athena::dependency::parse_dependency;

    /*
     * A -> B means B cannot be removed while A remains installed.
     */
    const std::vector<athena::package::Package> environment{
        make_package("A", "1.0", {
            parse_dependency("B")
        }),
        make_package("B", "1.0"),
        make_package("C", "1.0")
    };

    const athena::install::RemovalPlanner planner(
        environment
    );

    /*
     * Removing B alone would leave A with an unsatisfied dependency.
     */
    expect_invalid(
        planner,
        {"B"}
    );

    /*
     * Removing A is safe because B remains installed.
     */
    {
        const auto plan = planner.plan({"A"});

        require(
            plan.size() == 1,
            "Removing A must produce one removal operation"
        );

        require(
            plan.contains("A"),
            "Removal plan must contain A"
        );
    }

    /*
     * A and B can be removed together. Dependency validation is
     * performed after the complete removal set has been applied to the
     * candidate environment, so A's dependency on B disappears with A.
     */
    {
        const auto plan = planner.plan({"A", "B"});

        require(
            plan.size() == 2,
            "Removing A and B must produce two removal operations"
        );

        require(
            plan.contains("A") &&
            plan.contains("B"),
            "Removal plan must contain A and B"
        );
    }

    /*
     * Duplicate requests are harmless and remain a single operation.
     */
    {
        const auto plan = planner.plan({"A", "A"});

        require(
            plan.size() == 1,
            "Duplicate removal requests must be collapsed"
        );

        require(
            plan.contains("A"),
            "Collapsed removal plan must contain A"
        );
    }

    /*
     * Removing an unknown package must fail before dependency
     * validation because there is no installed package to remove.
     */
    expect_invalid(
        planner,
        {"does-not-exist"}
    );

    /*
     * An empty removal request is valid as a no-op plan.
     */
    {
        const auto plan = planner.plan({});

        require(
            plan.empty(),
            "Empty removal request must produce an empty plan"
        );
    }

    /*
     * A package with a versioned dependency remains valid when the
     * required version is still installed.
     */
    {
        const std::vector<athena::package::Package> versioned_environment{
            make_package("A", "1.0", {
                parse_dependency("B >= 2.0")
            }),
            make_package("B", "2.1"),
            make_package("C", "1.0")
        };

        const athena::install::RemovalPlanner versioned_planner(
            versioned_environment
        );

        expect_invalid(
            versioned_planner,
            {"B"}
        );

        const auto plan = versioned_planner.plan({"C"});

        require(
            plan.contains("C"),
            "Unrelated package C must remain removable"
        );
    }

    std::cout
        << "removal_planner_test: all checks passed\n";

    return 0;
}
