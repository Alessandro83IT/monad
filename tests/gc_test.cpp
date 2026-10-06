#include "../src/generation/generation.hpp"
#include "../src/store/gc.hpp"
#include "../src/store/object_store.hpp"
#include "../src/store/manifest.hpp"
#include <athena/core/repository.hpp>
#include "../src/store/metadata.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

static athena::realization::RealizationRecord realization_record(
    std::string name,
    athena::core::ArtifactId id
)
{
    using namespace athena::core;

    RepositorySnapshotRef snapshot{
        RepositoryId("repo"),
        RepositoryRevision("rev"),
        SchemaVersion(1),
        ContentDigest("sha256:" + std::string(64, 'a')),
        SnapshotId("sha256:" + std::string(64, 'b'))
    };

    return {
        {
            PackageId(std::move(name)),
            PackageVersion("1"),
            snapshot.repository_id,
            snapshot,
            RepositoryProvenance{
                snapshot.repository_id,
                snapshot.revision,
                {},
                snapshot
            }
        },
        std::move(id),
        {},
        ArtifactOrigin::AmbientSourceBuild
    };
}

int main()
{
    const auto test_root =
        std::filesystem::path(
            "/tmp/athena-gc-test"
        );

    std::filesystem::remove_all(
        test_root
    );

    const auto state_directory =
        test_root / "state";

    const auto store_directory =
        test_root / "store";

    std::filesystem::create_directories(
        store_directory
    );

    /*
     * Create three valid Athena store entries:
     *
     *     kept       -> referenced by a generation
     *     historical -> referenced only by an old generation
     *     orphan     -> referenced by no generation
     */
    const auto kept =
        store_directory / "package-kept-1.0";

    const auto historical =
        store_directory / "package-historical-1.0";

    const auto orphan =
        store_directory / "package-orphan-1.0";

    for (const auto& store : {
        kept,
        historical,
        orphan
    }) {
        std::filesystem::create_directories(
            store / ".athena"
        );

        athena::store::write_metadata(
            store,
            {
                "test-package",
                "1.0",
                "test-source",
                "test-sha256",
                "test"
            }
        );
    }

    /*
     * Also create an unrelated directory. The GC must never remove
     * objects that are not recognized as Athena store entries.
     */
    const auto unrelated =
        store_directory / "unrelated";

    std::filesystem::create_directories(
        unrelated
    );

    /*
     * Generation 0 references the historical store.
     */
    athena::generation::GenerationManager manager(
        state_directory
    );

    manager.initialize();

    manager.create(
        {
            {
                "package-historical",
                historical.string()
            }
        }
    );

    /*
     * Generation 2 references the current store.
     *
     * The historical store must survive because generation 1 still
     * makes it reachable for rollback.
     */
    manager.create(
        {
            {
                "package-kept",
                kept.string()
            }
        }
    );

    athena::store::GarbageCollector gc(
        store_directory,
        state_directory
    );

    const auto removed =
        gc.collect();

    /*
     * The orphan is unreachable and must be collected.
     */
    assert(
        !std::filesystem::exists(orphan)
    );

    assert(
        std::find(
            removed.begin(),
            removed.end(),
            orphan
        ) != removed.end()
    );

    /*
     * Both current and historical stores remain available.
     */
    assert(
        std::filesystem::exists(kept)
    );

    assert(
        std::filesystem::exists(historical)
    );

    /*
     * Unrelated directories are outside the GC ownership boundary.
     */
    assert(
        std::filesystem::exists(unrelated)
    );

    /*
     * A second collection is idempotent: there is nothing else to
     * remove.
     */
    const auto removed_again =
        gc.collect();

    assert(
        removed_again.empty()
    );

    /*
     * A symlink in the store root is not an Athena store entry and
     * must never be followed or removed by the collector.
     */
    const auto external_directory =
        test_root / "external";

    std::filesystem::create_directories(
        external_directory
    );

    const auto external_file =
        external_directory / "important";

    {
        std::ofstream file(external_file);
        assert(file);
        file << "must survive\n";
    }

    const auto store_symlink =
        store_directory / "external-link";

    std::filesystem::create_directory_symlink(
        external_directory,
        store_symlink
    );

    const auto removed_with_symlink =
        gc.collect();

    assert(
        removed_with_symlink.empty()
    );

    assert(
        std::filesystem::is_symlink(
            store_symlink
        )
    );

    assert(
        std::filesystem::exists(
            external_file
        )
    );

    // A legacy-only store can still be accessed through a symlink locator.
    const auto compat_real = test_root / "compat-real";
    const auto compat_link = test_root / "compat-link";
    const auto compat_state = test_root / "compat-state";
    std::filesystem::create_directory(compat_real);
    std::filesystem::create_directory_symlink(compat_real, compat_link);
    athena::generation::GenerationManager compat_manager(compat_state);
    compat_manager.initialize();
    compat_manager.create({{"keep", (compat_link / "keep").string()}});
    auto compat_entry = [&](const char* name) {
        const auto path = compat_real / name;
        std::filesystem::create_directories(path / ".athena");
        athena::store::write_metadata(path, {"test", "1", "source", "digest", "test"});
    };
    compat_entry("keep"); compat_entry("orphan");
    athena::store::GarbageCollector compat_gc(compat_link, compat_state);
    assert(compat_gc.collect() == std::vector<std::filesystem::path>{compat_link / "orphan"});
    assert(std::filesystem::exists(compat_real / "keep"));
    assert(compat_gc.collect().empty());
    // An incomplete, real namespace is absent; a namespace symlink is
    // suspicious even when dangling and must stop both deletion domains.
    std::filesystem::create_directories(compat_real / "objects/v1");
    assert(compat_gc.collect().empty());
    compat_entry("orphan");
    std::filesystem::create_directory_symlink(test_root / "missing", compat_real / "objects/v1/sha256");
    bool unsafe_namespace_rejected = false;
    try { compat_gc.collect(); } catch (const athena::store::ObjectError&) { unsafe_namespace_rejected = true; }
    assert(unsafe_namespace_rejected && std::filesystem::exists(compat_real / "orphan"));
    const auto strict_state = test_root / "strict-namespace-state";
    athena::generation::GenerationManager strict_manager(strict_state);
    strict_manager.initialize();
    athena::store::GarbageCollector strict_gc(compat_real, strict_state);
    unsafe_namespace_rejected = false;
    try { strict_gc.collect(); } catch (const athena::store::ObjectError&) { unsafe_namespace_rejected = true; }
    assert(unsafe_namespace_rejected && std::filesystem::exists(compat_real / "orphan"));
    std::filesystem::remove(compat_real / "objects/v1/sha256");
    std::filesystem::create_directory(compat_real / "objects/v1/sha256");
    // Even a real namespace cannot enable ObjectStore through a linked root.
    unsafe_namespace_rejected = false;
    try { compat_gc.collect(); } catch (const athena::store::ObjectError&) { unsafe_namespace_rejected = true; }
    assert(unsafe_namespace_rejected && std::filesystem::exists(compat_real / "orphan"));

    // GC must not normalize away a linked component before strict validation.
    athena::store::GarbageCollector ambiguous_gc(compat_link / "../compat-real", strict_state);
    unsafe_namespace_rejected = false;
    try { ambiguous_gc.collect(); } catch (const athena::store::ObjectError&) { unsafe_namespace_rejected = true; }
    assert(unsafe_namespace_rejected && std::filesystem::exists(compat_real / "orphan"));

    // Retained legacy generations can still activate their payload after GC.
    for (const auto& path : {historical, kept}) {
        std::filesystem::create_directories(path / "bin");
        std::ofstream(path / "bin/tool") << path.filename().string();
        athena::store::write_manifest(path, {{"bin/tool"}});
    }
    const auto target = test_root / "target";
    std::filesystem::create_directory(target);
    manager.switch_to(2, target);
    manager.switch_to(1, target);
    assert(manager.current().id == 1);
    assert(std::filesystem::canonical(target / "bin/tool") == historical / "bin/tool");
    manager.switch_to(2, target);

    /*
     * Content-addressed reachability is independent from legacy store
     * reachability. Every persisted generation is a root.
     */
    athena::store::ObjectStore objects(
        std::filesystem::absolute(store_directory)
    );

    const athena::core::ArtifactId historical_artifact(
        "sha256:" + std::string(64, 'c')
    );

    const athena::core::ArtifactId current_artifact(
        "sha256:" + std::string(64, 'd')
    );

    const athena::core::ArtifactId orphan_artifact(
        "sha256:" + std::string(64, 'e')
    );

    /*
     * These directories model already-published authoritative objects.
     * This test exercises GC reachability and ownership, not scanner or
     * publication semantics.
     */
    std::filesystem::create_directories(
        objects.object_path(historical_artifact)
    );

    std::filesystem::create_directories(
        objects.object_path(current_artifact)
    );

    std::filesystem::create_directories(
        objects.object_path(orphan_artifact)
    );

    const auto historical_realization =
        manager.create(
            {},
            {
                realization_record(
                    "historical-object",
                    historical_artifact
                )
            }
        );

    const auto current_realization =
        manager.create(
            {},
            {
                realization_record(
                    "current-object",
                    current_artifact
                )
            }
        );

    const auto shared = manager.create({}, {realization_record("shared", current_artifact)});
    // Select current state without invoking the privileged artifact scanner.
    std::ofstream(state_directory / "generations/current") << current_realization.id << '\n';
    assert(manager.current().id == current_realization.id);
    const auto ns = objects.object_path(orphan_artifact).parent_path();
    const auto link = ns / std::string(64, 'f');
    std::filesystem::create_directory_symlink(external_directory, link);
    std::filesystem::create_directory(ns / ".publish-keep");
    std::filesystem::create_directory(ns / "unrelated");
    std::filesystem::create_directory(ns / std::string(64, 'A'));
    std::ofstream(ns / std::string(64, 'a')) << "unrelated file";
    std::filesystem::create_directories(objects.object_path(orphan_artifact) / "nested");
    std::filesystem::create_directory_symlink(external_directory, objects.object_path(orphan_artifact) / "nested/link");
    (void)shared;

    const auto removed_content_addressed =
        gc.collect();

    assert(
        std::filesystem::exists(
            objects.object_path(historical_artifact)
        )
    );

    assert(
        std::filesystem::exists(
            objects.object_path(current_artifact)
        )
    );

    assert(
        !std::filesystem::exists(
            objects.object_path(orphan_artifact)
        )
    );

    assert(
        std::find(
            removed_content_addressed.begin(),
            removed_content_addressed.end(),
            objects.object_path(orphan_artifact)
        ) != removed_content_addressed.end()
    );

    /*
     * A second pass remains idempotent after content-addressed
     * collection as well.
     */
    assert(
        gc.collect().empty()
    );

    assert(std::filesystem::is_symlink(link) && std::filesystem::exists(external_file));
    for (const auto& name : {std::string(".publish-keep"), std::string("unrelated"), std::string(64, 'A'), std::string(64, 'a')})
        assert(std::filesystem::exists(ns / name));
    // Historical membership remains loadable and its authoritative payload exists.
    const auto retained = manager.get(historical_realization.id);
    assert(retained.realizations.front().artifact_id == historical_artifact);
    assert(std::filesystem::exists(objects.object_path(retained.realizations.front().artifact_id)));

    // Semantic corruption (TypeError derives from invalid_argument) and syntax
    // corruption must both stop GC before either ownership domain is deleted.
    const auto metadata = state_directory / "generations" / std::to_string(historical_realization.id) / "generation.toml";
    const auto valid_document = athena::generation::serialize(retained);
    std::filesystem::create_directory(objects.object_path(orphan_artifact));
    std::filesystem::create_directories(orphan / ".athena");
    athena::store::write_metadata(orphan, {"test", "1", "source", "digest", "test"});
    auto semantic_corruption = valid_document;
    semantic_corruption.replace(semantic_corruption.find(historical_artifact.value()), historical_artifact.value().size(), "invalid");
    for (const auto& corrupt : {semantic_corruption, std::string("invalid = [")}) {
        std::ofstream(metadata) << corrupt;
        bool rejected = false;
        try { gc.collect(); } catch (const std::exception&) { rejected = true; }
        assert(rejected);
        assert(std::filesystem::exists(orphan));
        assert(std::filesystem::exists(objects.object_path(orphan_artifact)));
    }
    std::ofstream(metadata) << valid_document;
    assert(gc.collect().size() == 2);
    assert(gc.collect().empty());

    /*
     * A generation referencing a path outside the configured store
     * root must be rejected rather than allowing the GC to treat that
     * path as an Athena-owned store entry.
     */
    const auto invalid_generation =
        manager.create(
            {
                {
                    "external-package",
                    external_directory.string()
                }
            }
        );

    (void)invalid_generation;

    bool invalid_path_rejected = false;

    try {
        gc.collect();
    }
    catch (const std::runtime_error&) {
        invalid_path_rejected = true;
    }

    assert(
        invalid_path_rejected
    );

    std::filesystem::remove_all(
        test_root
    );

    std::cout
        << "Garbage collector tests passed."
        << '\n';

    return 0;
}
