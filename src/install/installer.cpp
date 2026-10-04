#include "installer.hpp"

#include "../package/package.hpp"
#include "../package/package_loader.hpp"
#include "../download/downloader.hpp"
#include "../verify/verifier.hpp"
#include "../paths/paths.hpp"
#include "../archive/archive.hpp"
#include "../build/builder.hpp"
#include "../store/store.hpp"
#include "../generation/generation.hpp"
#include "generation_builder.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>


namespace athena::install {

/*
 * Execute the build and store pipeline for one already-loaded Package.
 *
 * Dependency resolution and activation are deliberately outside this
 * function. This function only turns a package definition into an
 * immutable store entry.
 *
 * Keeping activation out of this step is essential for transactional
 * InstallPlan execution: all packages must reach the store before a
 * new generation is activated.
 */
std::filesystem::path install_one_package(
    const athena::package::Package& package
)
{
    std::cout
        << "Installazione di: " << package.name
        << " " << package.version << '\n';

    const std::filesystem::path destination =
        athena::paths::downloads() /
        ("athena-" + package.name + "-" + package.version + ".tar.gz");

    std::cout
        << "Download: " << package.source << '\n';

    const auto downloaded =
        athena::download::download_file(
            package.source,
            destination
        );

    std::cout
        << "Download completato: "
        << downloaded << '\n';

    std::cout << "Verifica SHA-256...\n";

    const bool valid =
        athena::verify::verify_sha256(
            downloaded,
            package.sha256
        );

    if (!valid) {
        throw std::runtime_error(
            "SHA-256 non corrisponde per: " +
            downloaded.string()
        );
    }

    std::cout << "SHA-256 verificato correttamente.\n";

    const std::filesystem::path source_directory =
        athena::paths::sources();

    std::cout
        << "Estrazione in: "
        << source_directory << '\n';

    const auto extracted =
        athena::archive::extract(
            downloaded,
            source_directory
        );

    std::cout
        << "Estrazione completata: "
        << extracted << '\n';

    /*
     * Create a dedicated build directory for this package.
     *
     * Build artifacts must remain separate from the source tree.
     * This is especially important for out-of-source build systems
     * such as CMake.
     */
    const std::filesystem::path build_directory =
        athena::paths::build() /
        (package.name + "-" + package.version);

    /*
     * The staging directory represents the temporary filesystem tree
     * that will be transferred into the immutable package store.
     */
    const std::filesystem::path staging =
        athena::paths::build() /
        (package.name + "-" + package.version + "-install");

    std::cout
        << "Directory di build: "
        << build_directory << '\n';

    std::cout
        << "Compilazione...\n";

    athena::build::build(
        extracted,
        build_directory,
        package.build_system
    );

    std::cout
        << "Compilazione completata.\n";

    std::cout
        << "Installazione nello staging: "
        << staging << '\n';

    athena::build::install(
        extracted,
        build_directory,
        staging,
        package.build_system
    );

    std::cout
        << "Installazione nello staging completata.\n";

    /*
     * Store the package together with its complete metadata,
     * including serialized dependency expressions. The store entry
     * therefore contains enough information for future operations
     * such as dependency-aware package removal.
     */
    const auto installed =
        athena::store::install(
            staging,
            package
        );

    std::cout
        << "Installazione nello store completata: "
        << installed << '\n';

    /*
     * Return the immutable store directory instead of activating it.
     *
     * The caller uses this reference to construct the new Generation.
     */
    return installed;
}

/*
 * Build the complete generation that should become active after an
 * InstallPlan has been executed.
 *
 * The current generation represents the complete active environment.
 * Packages appearing in the new plan replace entries with the same
 * logical package name; packages not mentioned by the plan remain
 * active. This makes an InstallPlan an incremental state transition
 * rather than an instruction to discard the existing environment.
 */


void install_package(
    const std::filesystem::path& package_file,
    const std::filesystem::path& target_root
)
{
    /*
     * Preserve the existing single-package API by constructing a
     * one-package InstallPlan and delegating to the same transactional
     * execution path used for multi-package plans.
     */
    const athena::package::Package package =
        athena::package::load_from_file(package_file);

    InstallPlan plan;
    plan.add(package);

    install_plan(
        plan,
        target_root
    );
}

void install_plan(
    const InstallPlan& plan,
    const std::filesystem::path& target_root
)
{
    /*
     * Ensure that the complete Athena filesystem hierarchy exists
     * before any download, extraction, build or store operation starts.
     *
     * This keeps the installer self-contained and also allows isolated
     * tests to redirect Athena's paths into a temporary filesystem root.
     */
    athena::paths::initialize();

    /*
     * An empty plan represents no requested state transition.
     * Avoid creating a redundant generation in that case.
     */
    if (plan.empty()) {
        return;
    }

    /*
     * The generation manager owns persistent generation metadata and
     * the current-generation pointer. Initialize it before reading
     * the current state so a fresh Athena installation starts from
     * Generation 0.
     */
    athena::generation::GenerationManager generations(
        athena::paths::root()
    );

    generations.initialize();

    /*
     * Install every package into the immutable store first.
     *
     * No package is activated at this stage. Therefore a failure
     * during download, verification, extraction, build or storage
     * leaves the currently active generation untouched.
     */
    std::vector<
        std::pair<
            athena::package::Package,
            std::filesystem::path
        >
    > installed;

    installed.reserve(plan.size());

    for (const auto& package : plan.packages()) {
        installed.emplace_back(
            package,
            install_one_package(package)
        );
    }

    /*
     * Construct the complete target generation by applying the
     * installation plan to the current active environment.
     */
    const auto current =
        generations.current();

    const auto target_entries =
        build_target_entries(
            current,
            installed
        );

    /*
     * Persist the new generation before switching to it. Generation
     * metadata is immutable and can therefore safely become a rollback
     * target even after a later operation fails.
     */
    const auto target =
        generations.create(
            target_entries
        );

    std::cout
        << "Nuova generation: "
        << target.id << '\n';

    /*
     * Activation and the persistent current-generation pointer are
     * handled by GenerationManager as one state transition.
     */
    std::cout
        << "Attivazione della generation "
        << target.id
        << "...\n";

    generations.switch_to(
        target.id,
        target_root
    );

    std::cout
        << "Generation "
        << target.id
        << " attivata.\n";
}

}
