#include "package_loader.hpp"

#include "../../include/athena/core/schema.hpp"

#include "../dependency/parser.hpp"

#include <toml++/toml.hpp>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace athena::package {

/*
 * Load a package definition from a TOML file.
 *
 * The package definition contains the metadata Athena needs
 * to process the package, such as its name, version, source
 * archive, checksum and build system.
 *
 * The TOML file is converted into a Package object so that
 * the rest of the package-management pipeline does not need
 * to know anything about the file format.
 */
Package load_from_file(const std::filesystem::path& path)
{
    try {

        /*
         * Parse the TOML package definition.
         *
         * toml++ performs the syntax parsing and gives us access
         * to the values stored in the document.
         */
        const auto table = toml::parse_file(path.string());

        // Legacy package definitions may omit schema_version. If present,
        // however, it must be an explicitly supported package schema.
        if (const auto* schema = table.get("schema_version")) {
            const auto version = schema->value<std::int64_t>();
            if (!version || *version < 1 || *version > std::numeric_limits<int>::max()) {
                throw PackageSchemaError("Invalid package schema_version");
            }
            try {
                athena::core::validate_schema_version(
                    athena::core::SchemaKind::PackageMetadata,
                    static_cast<int>(*version)
                );
            } catch (const std::exception& error) {
                throw PackageSchemaError(error.what());
            }
        }

        /*
         * Convert the TOML values into our internal Package model.
         *
         * build_system defaults to "autotools" when the field is
         * not present. This keeps older package definitions
         * compatible with the current implementation.
         */
        std::vector<athena::dependency::Dependency> dependencies;

        if (const auto* array = table["dependencies"].as_array()) {

            for (const auto& value : *array) {

                if (const auto dependency = value.value<std::string>()) {

                    dependencies.push_back(
                        athena::dependency::parse_dependency(
                            *dependency
                        )
                    );
                }
            }
        }

        /*
         * Load semantic capabilities and features provided by the
         * package. The entire [provides] table is optional so existing
         * package definitions remain fully backward compatible.
         */
        PackageProvides provides;

        if (const auto* provides_table =
                table["provides"].as_table()) {

            if (const auto* capabilities =
                    (*provides_table)["capabilities"].as_array()) {

                for (const auto& value : *capabilities) {

                    if (const auto capability =
                            value.value<std::string>()) {

                        provides.capabilities.push_back(
                            *capability
                        );
                    }
                }
            }

            if (const auto* features =
                    (*provides_table)["features"].as_array()) {

                for (const auto& value : *features) {

                    if (const auto feature =
                            value.value<std::string>()) {

                        provides.features.push_back(
                            *feature
                        );
                    }
                }
            }
        }

        return Package{
            table["name"].value_or(""),
            table["version"].value_or(""),
            table["description"].value_or(""),
            table["source"].value_or(""),
            table["sha256"].value_or(""),
            table["build_system"].value_or("autotools"),
            dependencies,
            provides
        };
    }
    catch (const toml::parse_error& error) {

        /*
         * Convert the TOML-specific exception into a generic
         * runtime_error with the package file path included.
         *
         * This allows higher-level modules, such as the installer,
         * to report a useful error without depending on toml++.
         */
        throw std::runtime_error(
            "Impossibile caricare il pacchetto '" +
            path.string() + "': " + error.what()
        );
    }
}

}
