// SPDX-License-Identifier: GPL-3.0-or-later
// This TU sees only Athena's public includes and the original system toml++.
#include <athena/core/repository.hpp>
#include <toml++/toml.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
static_assert(TOML_LIB_MAJOR == 3 && TOML_LIB_MINOR == 4 && TOML_LIB_PATCH == 0);
#if defined(ATHENA_TOMLPLUSPLUS_HPP) || defined(ATHENA_TOML_LIB_MAJOR)
#error Internal fork leaked into public consumer
#endif
bool isolated_toml_upstream_first();
bool isolated_toml_athena_first();
int main() {
    try {
        const std::string bytes = "schema_version=1\npackage_id='pkg'\nversion='1'\n"
            "dependencies=[]\ncapabilities=[]\nfeatures=[]\nconflicts=[]\nplatforms=[]\narchitectures=[]\nabis=[]\n";
        const auto before = toml::parse(bytes);
        auto package = athena::core::deserialize_package_metadata(bytes);
        const auto encoded = athena::core::serialize(package);
        const auto after = toml::parse(encoded);
        if (before["package_id"].value<std::string>() != after["package_id"].value<std::string>() ||
            !isolated_toml_upstream_first() || !isolated_toml_athena_first())
            throw std::runtime_error("upstream/fork coexistence failed");
        // Calling the Athena library must not replace the upstream grammar.
        if (toml::parse("external.extension = [1, 2, 3]")["external"]["extension"].as_array()->size() != 3)
            throw std::runtime_error("upstream behavior changed");
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
