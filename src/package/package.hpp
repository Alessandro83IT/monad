#pragma once

#include "../dependency/expression.hpp"

#include <string>
#include <map>
#include <vector>

namespace athena::package {

/*
 * Describes the semantic interfaces exposed by a package.
 *
 * Capabilities identify functionality provided by the package, while
 * features identify optional or ecosystem-level features that the
 * package exposes. Dependency resolution will consume these fields
 * in a later phase.
 */
struct PackageProvides {

    // Capabilities exposed by the package, for example "ssl" or "tls".
    std::vector<std::string> capabilities{};

    // Features exposed by the package, for example "openssl".
    std::vector<std::string> features{};
    // Optional capability-specific versions. Missing entries use the package version.
    std::map<std::string, std::string> capability_versions{};
};

/*
 * Represents the definition of a package.
 *
 * A Package is created by the package loader from a package.toml
 * file and is then passed through the installation pipeline.
 *
 * The structure currently contains the basic information required
 * to download, verify and build a package.
 */
struct Package {

    // Package name, for example: "hello".
    std::string name;

    // Package version, for example: "2.12".
    std::string version;

    // Human-readable package description.
    std::string description;

    // URL of the source archive.
    std::string source;

    // Expected SHA-256 checksum of the source archive.
    std::string sha256;

    /*
     * Build system used to compile the package.
     *
     * Examples:
     *   "autotools"
     *   "cmake"
     *   "meson"
     *
     * The actual build system implementation is handled by
     * the build module.
     */
    std::string build_system;

    /*
     * Dependencies required by this package.
     *
     * Dependencies are represented using Athena's universal
     * dependency expression model, allowing package names,
     * capabilities and version constraints to be represented
     * independently of a specific package ecosystem.
     */
    std::vector<athena::dependency::Dependency> dependencies;

    /*
     * Semantic capabilities and features provided by this package.
     *
     * The field is intentionally placed after dependencies so existing
     * aggregate initializers remain source-compatible.
     */
    PackageProvides provides{};

    // Negative dependency expressions checked as soon as this package is selected.
    std::vector<athena::dependency::Dependency> conflicts;

    // Configurable features are distinct from provided features. Enabling a
    // key activates only the dependencies declared in its value.
    std::map<std::string, std::vector<athena::dependency::Dependency>> feature_dependencies;

    // Raw fields from an Arch repository desc record. Kept opaque here so
    // importing Arch binaries does not reinterpret them as an Athena recipe.
    std::map<std::string, std::vector<std::string>> arch_metadata{};

    // Repository target restrictions retained by the resolver-facing
    // metadata adapter. Empty lists mean unrestricted, matching PackageMetadata.
    std::vector<std::string> platforms;
    std::vector<std::string> architectures;
    std::vector<std::string> abis;
};

/*
 * Print the basic information contained in a Package.
 *
 * This function is mainly useful for CLI output and debugging.
 */
void print_info(const Package& package);

}
