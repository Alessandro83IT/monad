#pragma once

#include <string>
#include <utility>

namespace athena::dependency {

/*
 * Describes the execution/build target environment against which
 * semantic platform constraints are evaluated.
 *
 * These values are deliberately represented as strings. Athena does
 * not hard-code a finite platform, architecture or ABI enumeration:
 * repositories and future language tooling may introduce their own
 * identifiers.
 */
struct Environment {
    std::string platform;
    std::string architecture;
    std::string abi;

    Environment() = default;

    Environment(
        std::string platform_name,
        std::string architecture_name,
        std::string abi_name
    )
        : platform(std::move(platform_name)),
          architecture(std::move(architecture_name)),
          abi(std::move(abi_name))
    {
    }
};

/*
 * The three environments relevant to dependency resolution.
 *
 * Host   = machine on which Athena/build tools execute.
 * Build  = environment in which a package is built.
 * Target = environment for which the package is produced.
 */
struct Environments {
    Environment host;
    Environment build;
    Environment target;
};

// Native ambient execution context, shared by legacy and CLI request adapters.
Environments native_environments();

} // namespace athena::dependency
