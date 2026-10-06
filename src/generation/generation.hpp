#pragma once

#include "../realization/record.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace athena::store { class ObjectStore; }

namespace athena::generation {
enum class GenerationCompatibility { Historical, Modern };

/*
 * A legacy generation entry identifies one package and the immutable store
 * directory containing its files.
 *
 * The generation stores references to immutable data; it never owns
 * or copies the package files themselves.
 */
struct GenerationEntry {

    // Logical package name represented by this store entry.
    std::string package;

    // Immutable store directory containing the package files.
    std::string store_path;
    std::optional<installed::SemanticState> semantics;

    /*
     * Generation entries are value objects. Equality therefore means
     * that both the logical package and its immutable store reference
     * are identical.
     */
    bool operator==(
        const GenerationEntry&
    ) const = default;
};

/*
 * A Generation represents one complete logical state of the
 * activated Athena environment.
 *
 * Once persisted, a generation is immutable.
 */
struct Generation {

    // Monotonically increasing generation identifier.
    std::uint64_t id;

    // Ordered legacy packages composing this generation.
    std::vector<GenerationEntry> entries;

    // Explicitly distinct from legacy entries; resolved separately from legacy manifests.
    std::vector<realization::RealizationRecord> realizations{};
    // Environment requirements outlive individual package memberships.
    std::vector<installed::PolicyRequirement> policies{};
    // Reader provenance, not inferred from optional member metadata. New
    // values/writes are modern; only pre-M2.4 input receives historical mode.
    GenerationCompatibility compatibility = GenerationCompatibility::Modern;

    void validate() const;
};

// Pure state boundary. Legacy order is preserved; realizations are canonicalized.
Generation make_generation(std::uint64_t id, std::vector<GenerationEntry> legacy,
    std::vector<realization::RealizationRecord> realizations = {},
    std::vector<installed::PolicyRequirement> policies = {});
std::string serialize(const Generation& generation);
Generation deserialize_generation(const std::string& document);

/*
 * Manage the persistent set of Athena generations.
 *
 * The manager stores generation metadata below:
 *
 *     <state>/generations/<id>/generation.toml
 *
 * and keeps the identifier of the active generation in:
 *
 *     <state>/generations/current
 */
class GenerationManager {

public:

    struct GenerationDirectoryTag {};

    explicit GenerationManager(
        std::filesystem::path state_directory
    );
    GenerationManager(
        std::filesystem::path generation_directory,
        GenerationDirectoryTag
    );

    /*
     * Create Generation 0 if no generations exist yet.
     *
     * If generations already exist, the existing current generation
     * is returned without modifying persistent state.
     */
    Generation initialize();

    /*
     * Create a new immutable generation containing the supplied
     * store references. The new generation is not activated.
     */
    Generation create(
        const std::vector<GenerationEntry>& entries,
        const std::vector<realization::RealizationRecord>& realizations = {},
        const std::vector<installed::PolicyRequirement>& policies = {}
    );

    /*
     * Read an existing generation.
     */
    Generation get(
        std::uint64_t id
    ) const;

    /*
     * Discard a generation that is no longer needed.
     *
     * This operation is intended for generations created as part of
     * a failed transaction. The currently active generation can
     * never be discarded.
     */
    void discard(
        std::uint64_t id
    );

    /*
     * Return every persisted generation in ascending ID order.
     *
     * The garbage collector uses all generations as roots so that
     * historical rollback targets remain available.
     */
    std::vector<Generation> list() const;

    /*
     * Return the currently selected generation.
     */
    Generation current() const;

    /*
     * Switch the active filesystem state to an existing generation.
     *
     * The persistent current-generation pointer is updated only
     * after the filesystem transition succeeds.
     */
    void switch_to(
        std::uint64_t id,
        const std::filesystem::path& target_root
    );

    // Explicit authoritative store context; existing legacy callers need none.
    void switch_to(std::uint64_t id, const std::filesystem::path& target_root,
                   const store::ObjectStore& object_store);

private:
    void switch_impl(std::uint64_t id, const std::filesystem::path& target_root,
                     const store::ObjectStore* object_store);

    std::filesystem::path state_directory_;
    std::filesystem::path generations_directory_;

    void ensure_directories() const;

    void write_generation(
        const Generation& generation
    ) const;

    void write_current(
        std::uint64_t id
    ) const;

    std::uint64_t next_id() const;
};

}
