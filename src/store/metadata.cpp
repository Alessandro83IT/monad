#include "metadata.hpp"

#include <fstream>
#include <stdexcept>
#include <toml++/toml.hpp>

namespace athena::store {

/*
 * Write the metadata of an installed package.
 *
 * Metadata is kept inside a dedicated .athena directory so that
 * package files and Athena-specific information remain clearly
 * separated.
 */
void write_metadata(
    const std::filesystem::path& store_directory,
    const Metadata& metadata
)
{
    /*
     * Create the directory used for Athena-specific metadata.
     *
     * Example:
     *
     *     /var/lib/athena/store/hello-2.12/.athena/
     */
    const std::filesystem::path metadata_directory =
        store_directory / ".athena";

    std::filesystem::create_directories(metadata_directory);

    /*
     * Metadata is stored in TOML so that it remains human-readable
     * and can easily be inspected or processed by other tools.
     */
    const std::filesystem::path metadata_file =
        metadata_directory / "metadata.toml";

    std::ofstream output(metadata_file);

    if (!output) {
        throw std::runtime_error(
            "Impossibile creare il file dei metadati: " +
            metadata_file.string()
        );
    }

    /*
     * Write the package metadata.
     *
     * This information describes how the installed package was
     * obtained and built.
     */
    /*
     * Build the TOML document through toml++ so dependency strings
     * are escaped correctly instead of being interpolated manually.
     */
    toml::table table{
        {"name", metadata.name},
        {"version", metadata.version},
        {"source", metadata.source},
        {"sha256", metadata.sha256},
        {"build_system", metadata.build_system}
    };

    toml::array dependencies;

    for (const auto& dependency : metadata.dependencies) {
        dependencies.push_back(dependency);
    }

    table.insert("dependencies", std::move(dependencies));

    /*
     * Persist semantic capabilities and features in an optional
     * [provides] table. Empty fields are omitted so the serialized
     * format remains compact and older-style metadata stays simple.
     */
    if (!metadata.provides.capabilities.empty() ||
        !metadata.provides.features.empty()) {

        toml::table provides;

        if (!metadata.provides.capabilities.empty()) {
            toml::array capabilities;

            for (const auto& capability :
                 metadata.provides.capabilities) {
                capabilities.push_back(capability);
            }

            provides.insert(
                "capabilities",
                std::move(capabilities)
            );
        }

        if (!metadata.provides.features.empty()) {
            toml::array features;

            for (const auto& feature :
                 metadata.provides.features) {
                features.push_back(feature);
            }

            provides.insert(
                "features",
                std::move(features)
            );
        }

        table.insert(
            "provides",
            std::move(provides)
        );
    }

    output << table;
}

/*
 * Read package metadata from an installed store entry.
 *
 * The metadata file is parsed using toml++ and converted back into
 * Athena's internal Metadata representation.
 */
Metadata read_metadata(
    const std::filesystem::path& store_directory
)
{
    /*
     * Locate the metadata file associated with the store entry.
     */
    const std::filesystem::path metadata_file =
        store_directory /
        ".athena" /
        "metadata.toml";

    try {

        /*
         * Parse the TOML metadata file.
         */
        const auto table =
            toml::parse_file(metadata_file.string());

        /*
         * Read dependencies as an optional array.
         *
         * Older store entries may not contain this field, so the
         * absence of the array intentionally results in an empty
         * dependency list for backward compatibility.
         */
        std::vector<std::string> dependencies;

        if (const auto* dependency_array =
                table["dependencies"].as_array()) {

            for (const auto& dependency : *dependency_array) {
                if (const auto value =
                        dependency.value<std::string>()) {
                    dependencies.push_back(*value);
                }
                else {
                    throw std::runtime_error(
                        "Il campo 'dependencies' contiene un "
                        "valore non stringa nei metadati: " +
                        metadata_file.string()
                    );
                }
            }
        }

        /*
         * Read semantic capabilities and features from the optional
         * [provides] table. Missing fields remain empty for backward
         * compatibility with existing store metadata.
         */
        athena::package::PackageProvides provides;

        if (const auto* provides_table =
                table["provides"].as_table()) {

            if (const auto* capabilities =
                    (*provides_table)["capabilities"].as_array()) {

                for (const auto& capability : *capabilities) {
                    if (const auto value =
                            capability.value<std::string>()) {
                        provides.capabilities.push_back(*value);
                    }
                    else {
                        throw std::runtime_error(
                            "Il campo 'provides.capabilities' contiene "
                            "un valore non stringa nei metadati: " +
                            metadata_file.string()
                        );
                    }
                }
            }

            if (const auto* features =
                    (*provides_table)["features"].as_array()) {

                for (const auto& feature : *features) {
                    if (const auto value =
                            feature.value<std::string>()) {
                        provides.features.push_back(*value);
                    }
                    else {
                        throw std::runtime_error(
                            "Il campo 'provides.features' contiene "
                            "un valore non stringa nei metadati: " +
                            metadata_file.string()
                        );
                    }
                }
            }
        }

        /*
         * Convert the TOML values into a Metadata object.
         */
        return Metadata{
            table["name"].value_or(""),
            table["version"].value_or(""),
            table["source"].value_or(""),
            table["sha256"].value_or(""),
            table["build_system"].value_or(""),
            std::move(dependencies),
            std::move(provides)
        };
    }
    catch (const toml::parse_error& error) {

        /*
         * Hide the TOML-specific exception from higher-level modules
         * and provide a useful Athena-specific error message.
         */
        throw std::runtime_error(
            "Impossibile leggere i metadati del pacchetto '" +
            store_directory.string() + "': " +
            error.what()
        );
    }
}

}
