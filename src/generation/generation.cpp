#include "generation.hpp"

#include "../activation/activation.hpp"

#include <algorithm>
#include <fstream>
#include <unordered_set>
#include <stdexcept>

namespace athena::generation {

GenerationManager::GenerationManager(
    std::filesystem::path state_directory
)
    : state_directory_(std::move(state_directory)),
      generations_directory_(
          state_directory_ / "generations"
      )
{
}

GenerationManager::GenerationManager(
    std::filesystem::path generation_directory,
    GenerationDirectoryTag
)
    : state_directory_(generation_directory.parent_path()),
      generations_directory_(std::move(generation_directory))
{
}

/*
 * Ensure that the persistent generation directory exists.
 */
void GenerationManager::ensure_directories() const
{
    std::filesystem::create_directories(
        generations_directory_
    );
}

/*
 * Create Generation 0 on first initialization.
 *
 * The operation is idempotent: an already initialized manager
 * simply returns the existing current generation.
 */
Generation GenerationManager::initialize()
{
    ensure_directories();

    const auto current_file =
        generations_directory_ / "current";

    if (std::filesystem::exists(current_file)) {
        return current();
    }

    const Generation initial{
        0,
        {}
    };

    write_generation(initial);
    write_current(initial.id);

    return initial;
}

/*
 * Determine the next monotonically increasing generation ID.
 */
std::uint64_t GenerationManager::next_id() const
{
    ensure_directories();

    std::uint64_t next = 0;

    for (
        const auto& entry :
        std::filesystem::directory_iterator(
            generations_directory_
        )
    ) {
        if (!entry.is_directory()) {
            continue;
        }

        try {
            const auto id =
                std::stoull(
                    entry.path().filename().string()
                );

            if (id >= next) {
                next = id + 1;
            }
        }
        catch (const std::invalid_argument&) {
            /*
             * Ignore non-generation directories.
             */
        }
        catch (const std::out_of_range&) {
            throw std::runtime_error(
                "Identificatore di generation fuori intervallo: " +
                entry.path().string()
            );
        }
    }

    return next;
}

/*
 * Persist an immutable generation.
 */
void GenerationManager::write_generation(
    const Generation& generation
) const
{
    const auto document = serialize(generation);

    const auto directory =
        generations_directory_ /
        std::to_string(generation.id);

    /*
     * Never overwrite an existing generation. This is the central
     * immutability guarantee of the generation layer.
     */
    if (std::filesystem::exists(directory)) {
        throw std::runtime_error(
            "La generation esiste già: " +
            std::to_string(generation.id)
        );
    }

    std::filesystem::create_directories(directory);

    const auto file =
        directory / "generation.toml";

    std::ofstream output(file);

    if (!output) {
        throw std::runtime_error(
            "Impossibile creare la generation: " +
            file.string()
        );
    }

    output << document;
    output.close();
    if (!output) throw std::runtime_error("Failed to persist generation");
}

/*
 * Persist the pointer to the current generation.
 */
void GenerationManager::write_current(
    std::uint64_t id
) const
{
    const auto file =
        generations_directory_ / "current";

    std::ofstream output(file);

    if (!output) {
        throw std::runtime_error(
            "Impossibile aggiornare la generation corrente: " +
            file.string()
        );
    }

    output << id << '\n';
}

/*
 * Load an immutable generation from disk.
 */
Generation GenerationManager::get(
    std::uint64_t id
) const
{
    const auto file =
        generations_directory_ /
        std::to_string(id) /
        "generation.toml";

    if (!std::filesystem::exists(file)) {
        throw std::runtime_error(
            "Generation non trovata: " +
            std::to_string(id)
        );
    }

    std::ifstream input(file);
    if (!input) throw std::runtime_error("Failed to read generation: " + file.string());
    std::string document((std::istreambuf_iterator<char>(input)), {});
    if (input.bad()) throw std::runtime_error("Failed to read generation: " + file.string());
    auto generation = deserialize_generation(document);
    if (generation.id != id) throw std::runtime_error("Generation identifier mismatch");
    return generation;
}

/*
 * Discard a generation that is no longer part of a successful
 * transaction.
 *
 * Generations are immutable once committed, but a newly created
 * generation may still belong to an in-progress transaction. The
 * transaction owner may explicitly discard that generation if
 * activation fails.
 *
 * The current generation is protected because deleting it would
 * invalidate the persistent environment state.
 */
void GenerationManager::discard(
    std::uint64_t id
)
{
    const auto current_generation =
        current();

    if (current_generation.id == id) {
        throw std::runtime_error(
            "Impossibile eliminare la generation corrente: " +
            std::to_string(id)
        );
    }

    const auto directory =
        generations_directory_ /
        std::to_string(id);

    if (!std::filesystem::exists(directory)) {
        throw std::runtime_error(
            "Generation non trovata: " +
            std::to_string(id)
        );
    }

    /*
     * The generation metadata is removed only after the current
     * pointer has been verified. Store entries are deliberately not
     * touched here: they are immutable objects and are managed by
     * the garbage collector.
     */
    std::filesystem::remove_all(directory);
}

/*
 * Load every persisted generation.
 *
 * Directory names that are not numeric generation identifiers are
 * ignored, matching the behavior already used by next_id(). The
 * returned vector is sorted by generation ID to keep callers
 * deterministic.
 */
std::vector<Generation> GenerationManager::list() const
{
    ensure_directories();

    std::vector<Generation> generations;

    for (
        const auto& entry :
        std::filesystem::directory_iterator(
            generations_directory_
        )
    ) {
        if (!entry.is_directory()) {
            continue;
        }

        try {
            const auto id =
                std::stoull(
                    entry.path().filename().string()
                );

            generations.push_back(
                get(id)
            );
        }
        catch (const std::invalid_argument&) {
            /*
             * Ignore auxiliary directories such as non-numeric
             * filesystem entries.
             */
        }
        catch (const std::out_of_range&) {
            throw std::runtime_error(
                "Identificatore di generation fuori intervallo: " +
                entry.path().string()
            );
        }
    }

    std::sort(
        generations.begin(),
        generations.end(),
        [](const Generation& left, const Generation& right) {
            return left.id < right.id;
        }
    );

    return generations;
}

/*
 * Create a new immutable generation.
 */
Generation GenerationManager::create(
    const std::vector<GenerationEntry>& entries,
    const std::vector<realization::RealizationRecord>& realizations
)
{
    ensure_directories();

    const auto generation = make_generation(next_id(), entries, realizations);

    write_generation(generation);

    return generation;
}

/*
 * Return the generation selected by the persistent current pointer.
 */
Generation GenerationManager::current() const
{
    const auto file =
        generations_directory_ / "current";

    if (!std::filesystem::exists(file)) {
        throw std::runtime_error(
            "Nessuna generation inizializzata."
        );
    }

    std::ifstream input(file);

    if (!input) {
        throw std::runtime_error(
            "Impossibile leggere la generation corrente: " +
            file.string()
        );
    }

    std::uint64_t id = 0;

    input >> id;

    if (input.fail()) {
        throw std::runtime_error(
            "Identificatore della generation corrente non valido: " +
            file.string()
        );
    }

    return get(id);
}

/*
 * Switch the persistent current-generation pointer.
 *
 * The target generation must already exist. This prevents current
 * from ever referring to an invalid generation.
 */
/*
 * Switch the filesystem to an existing generation.
 *
 * A generation is represented by immutable store directories. The
 * target filesystem is updated by activating entries that belong to
 * the new generation and deactivating entries that belong only to
 * the previous generation.
 *
 * The persistent current pointer is changed only after all required
 * filesystem operations have succeeded.
 */
void GenerationManager::switch_to(
    std::uint64_t id,
    const std::filesystem::path& target_root
)
{
    const Generation target =
        get(id);

    const Generation previous =
        current();

    if (!target.realizations.empty() || !previous.realizations.empty()) {
        throw std::runtime_error("Realization-backed generation activation is not implemented");
    }

    if (previous.id == target.id) {
        return;
    }

    /*
     * Compare generations using immutable store paths. The package
     * name is metadata identifying the entry; the store path is the
     * actual filesystem object that must be activated.
     */
    std::unordered_set<std::string> previous_entries;

    for (const auto& entry : previous.entries) {
        previous_entries.insert(entry.store_path);
    }

    std::unordered_set<std::string> target_entries;

    for (const auto& entry : target.entries) {
        target_entries.insert(entry.store_path);
    }

    std::vector<std::string> removed;
    std::vector<std::string> added;

    for (const auto& entry : previous.entries) {
        if (!target_entries.contains(entry.store_path)) {
            removed.push_back(entry.store_path);
        }
    }

    for (const auto& entry : target.entries) {
        if (!previous_entries.contains(entry.store_path)) {
            added.push_back(entry.store_path);
        }
    }

    /*
     * Remove entries that do not belong to the target generation.
     * Keep track of them so they can be restored if activation of a
     * new entry fails later in the transaction.
     */
    std::vector<std::string> successfully_removed;

    try {

        for (const auto& entry : removed) {

            athena::activation::deactivate(
                entry,
                target_root
            );

            successfully_removed.push_back(entry);
        }

        /*
         * Activation itself is transactional. If one of the new
         * entries fails, activation rolls back the links it created.
         */
        for (const auto& entry : added) {
            athena::activation::activate(
                entry,
                target_root
            );
        }

        /*
         * The filesystem now represents the target generation.
         * Only now is the persistent generation pointer changed.
         */
        write_current(target.id);
    }
    catch (...) {

        /*
         * Restore entries removed before the failure. Activation is
         * itself transactional, so a failed restoration does not
         * leave partially created links from that restoration attempt.
         */
        for (
            auto it = successfully_removed.rbegin();
            it != successfully_removed.rend();
            ++it
        ) {
            try {
                athena::activation::activate(
                    *it,
                    target_root
                );
            }
            catch (...) {
                /*
                 * Preserve the original switch failure. A future
                 * recovery mechanism can inspect the filesystem if
                 * restoration itself encounters an independent error.
                 */
            }
        }

        throw;
    }
}

}
