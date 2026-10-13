// SPDX-License-Identifier: GPL-3.0-or-later
#include "initial_activation.hpp"
#include "../artifact/identity.hpp"
#include "../generation/document.hpp"
#include "../generation/publication.hpp"
#include "../store/transaction_pins.hpp"
#include <fcntl.h>
#include <system_error>
namespace athena::activation {
namespace d = core::detail;
namespace a = store::administrative;
InitialActivationError::InitialActivationError(InitialStage s, InitialCause c, ProjectionOutcome o,
                                               std::exception_ptr e)
    : std::runtime_error("Initial activation requires inspection; residue preserved"), stage(s),
      cause(c), outcome(o), underlying(e) {}
namespace {
[[noreturn]] void fail(InitialStage s, InitialCause c) {
    throw InitialActivationError(s, c, ProjectionOutcome::NotMutated);
}
bool exists(int fd, const char *name) {
    struct stat st{};
    if (::fstatat(fd, name, &st, AT_SYMLINK_NOFOLLOW) == 0)
        return true;
    if (errno == ENOENT)
        return false;
    throw std::system_error(errno, std::generic_category(), "Initial namespace inspection");
}
void authority(const ProjectionBinding &b, const ProjectionState &s) {
    b.validate();
    s.revalidate();
    if (b.generation_authority != s.generations())
        fail(InitialStage::Classification, InitialCause::Conflict);
}
OperationalGeneration observed(const ProjectionBinding &b, std::uint64_t id) {
    try {
        auto doc = generation::read_generation_document(b.generation_authority, id);
        return {id, doc.digest, doc.bytes, {}};
    } catch (...) {
        throw InitialActivationError(InitialStage::GenerationRead, InitialCause::PrimitiveFailure,
                                     ProjectionOutcome::NotMutated, std::current_exception());
    }
}
std::string historical_current(int parent) {
    auto fd = d::open_at(parent, "current", O_RDONLY | O_NONBLOCK);
    const auto before = d::stat_fd(fd.get());
    const auto root = d::stat_fd(parent);
    if (!S_ISREG(before.st_mode) || before.st_uid != ::geteuid() || before.st_nlink != 1 ||
        (before.st_mode & 07022) || before.st_size < 0 || before.st_size > max_current_size ||
        before.st_dev != root.st_dev)
        fail(InitialStage::CurrentObservation, InitialCause::Conflict);
    a::same_mount(parent, fd.get());
    a::unchanged(parent, "current", fd.get());
    std::string bytes;
    char buffer[4096];
    for (;;) {
        auto n = ::read(fd.get(), buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            throw std::system_error(errno, std::generic_category(), "Read initial current");
        if (!n)
            break;
        if (bytes.size() + n > max_current_size)
            fail(InitialStage::CurrentObservation, InitialCause::Conflict);
        bytes.append(buffer, n);
    }
    const auto after = d::stat_fd(fd.get());
    a::unchanged(parent, "current", fd.get());
    if (before.st_size != after.st_size || bytes.size() != before.st_size ||
        before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
        before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
        before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
        fail(InitialStage::CurrentObservation, InitialCause::Conflict);
    return bytes;
}
bool empty(const generation::Generation &g) { return g.entries.empty() && g.realizations.empty(); }
bool store_binding(const ProjectionBinding &b) {
    try {
        store::StoreGcLock gc(b.store_authority); // caller already owns State
        return store::AuthorityContext(b.store_authority, b.generation_authority)
            .inspect_binding(gc);
    } catch (...) {
        throw InitialActivationError(InitialStage::StoreBinding, InitialCause::StoreBindingConflict,
                                     ProjectionOutcome::NotMutated, std::current_exception());
    }
}
void gc_compatible(const ProjectionBinding &b) {
    std::vector<std::uint64_t> ids;
    try {
        // All retained roots, not just the selected one, must remain readable by C.
        ids = generation::publication::existing_ids_for_gc(b.generation_authority);
        auto root = d::open_directory(b.generation_authority);
        a::private_directory(root.get());
        for (auto id : ids) {
            auto dir = d::open_at(root.get(), std::to_string(id), O_RDONLY | O_DIRECTORY);
            a::same_mount(root.get(), dir.get());
            auto file = d::open_at(dir.get(), "generation.toml", O_RDONLY);
            a::same_mount(dir.get(), file.get());
        }
    } catch (...) {
        throw InitialActivationError(InitialStage::GenerationRead,
                                     InitialCause::LegacyMetadataUnsupported,
                                     ProjectionOutcome::NotMutated, std::current_exception());
    }
    for (auto id : ids) {
        auto doc = observed(b, id); // preserve document/IO errors as GenerationRead
        for (const auto &entry :
             generation::validate_generation_document(doc.document, id).generation.entries) {
            const auto prefix = b.store_authority + '/';
            if (!entry.store_path.starts_with(prefix))
                fail(InitialStage::GenerationRead, InitialCause::LegacyLocatorUnsupported);
            const auto relative = entry.store_path.substr(prefix.size());
            try {
                validate_relative_path(relative);
            } catch (...) {
                throw InitialActivationError(
                    InitialStage::GenerationRead, InitialCause::LegacyLocatorUnsupported,
                    ProjectionOutcome::NotMutated, std::current_exception());
            }
            const auto first = relative.substr(0, relative.find('/'));
            if (first == "objects" || first == ".athena-gc-trash" || first == ".athena-locks")
                fail(InitialStage::GenerationRead, InitialCause::LegacyLocatorUnsupported);
        }
    }
}
void verify_zero_preparation(int g, const std::string &name) {
    try {
        auto dir = d::open_at(g, name, O_RDONLY | O_DIRECTORY);
        a::private_directory(dir.get());
        a::same_mount(g, dir.get());
        if (a::entries(dir.get()) != std::vector<std::string>{"generation.toml"} ||
            a::read_file(dir.get(), "generation.toml") !=
                generation::serialize(generation::make_generation(0, {})))
            fail(InitialStage::Classification, InitialCause::Conflict);
        a::unchanged(g, name, dir.get());
    } catch (const InitialActivationError &) {
        throw;
    } catch (...) {
        throw InitialActivationError(InitialStage::Classification, InitialCause::Conflict,
                                     ProjectionOutcome::NotMutated, std::current_exception());
    }
}
ProjectionContext context(const ProjectionBinding &b, const store::ObjectStore &o,
                          const ProjectionState &s, bool bound, ProjectionIo io) {
    try {
        return bound ? ProjectionContext::open(b, o, std::move(io))
                     : ProjectionContext::create_binding(b, o, s, std::move(io));
    } catch (...) {
        throw InitialActivationError(InitialStage::Binding, InitialCause::BindingRecoveryRequired,
                                     ProjectionOutcome::OutcomeUncertain, std::current_exception());
    }
}
} // namespace
namespace {
InitialObservation inspect(const ProjectionBinding &b, const store::ObjectStore &o,
                           const ProjectionState &s, ProjectionIo io,
                           const std::optional<ActivationId> &authorized = {}) {
    try {
        authority(b, s);
        if (o.root().native() != b.store_authority)
            fail(InitialStage::Classification, InitialCause::Conflict);
        bool c_bound = store_binding(b);
        auto g = d::open_directory(b.generation_authority),
             r = d::open_directory(b.projection_root);
        bool left = exists(g.get(), ".athena-projection-authority.toml"),
             right = exists(r.get(), ".athena-activation");
        if (left != right)
            fail(InitialStage::Binding, InitialCause::BindingRecoveryRequired);
        for (const auto &name : a::entries(g.get()))
            if (name.starts_with(".publish-") || name.starts_with(".athena-tmp-"))
                fail(InitialStage::Binding, InitialCause::BindingRecoveryRequired);
        std::optional<CurrentSelection> current;
        if (left) {
            try {
                auto p = ProjectionContext::open(b, o, io);
                try {
                    current = inspect_selection(p, s);
                } catch (...) {
                    throw InitialActivationError(
                        InitialStage::CurrentObservation, InitialCause::PrimitiveFailure,
                        ProjectionOutcome::OutcomeUncertain, std::current_exception());
                }
            } catch (const InitialActivationError &) {
                throw;
            } catch (...) {
                throw InitialActivationError(
                    InitialStage::Binding, InitialCause::BindingRecoveryRequired,
                    ProjectionOutcome::OutcomeUncertain, std::current_exception());
            }
        } else if (exists(g.get(), "current")) {
            try {
                current = parse_current_selection(historical_current(g.get()));
            } catch (const InitialActivationError &) {
                throw;
            } catch (...) {
                throw InitialActivationError(
                    InitialStage::CurrentObservation, InitialCause::PrimitiveFailure,
                    ProjectionOutcome::OutcomeUncertain, std::current_exception());
            }
        }
        if (current) {
            auto doc = observed(b, selected_generation(*current));
            auto membership =
                generation::validate_generation_document(doc.document, doc.id).generation;
            if (std::holds_alternative<CurrentV1>(*current)) {
                if (!left)
                    fail(InitialStage::Binding, InitialCause::BindingRecoveryRequired);
                return {InitialScenario::Selected, true, current};
            }
            return {empty(membership) ? InitialScenario::LegacyEmpty
                                      : InitialScenario::LegacyNonempty,
                    left, current};
        }
        bool preparation = false, selection_preparation = false;
        if (c_bound)
            fail(InitialStage::Classification, InitialCause::Conflict);
        for (const auto &name : a::entries(g.get())) {
            if (name == ".athena-locks" || name == ".athena-projection-authority.toml")
                continue;
            if (name == "0" || name == ".initial-generation") {
                if (name == ".initial-generation" && exists(g.get(), "0"))
                    fail(InitialStage::Classification, InitialCause::Conflict);
                verify_zero_preparation(g.get(), name);
                preparation = true;
                continue;
            }
            if (name.starts_with(".athena-current-") && left && exists(g.get(), "0")) {
                // This observation grants no selection authority. An explicit retry
                // must reconstruct the identical absent-base request and D4 digest.
                auto bytes = a::read_file(g.get(), name);
                auto candidate = parse_current_selection(bytes);
                auto modern = std::get_if<CurrentV1>(&candidate);
                if (!modern || modern->generation != 0 ||
                    name != ".athena-current-" + modern->activation_id.value())
                    fail(InitialStage::Classification, InitialCause::Conflict);
                auto p = ProjectionContext::open(b, o, io);
                auto zero = observed(b, 0);
                OperationalPlanInput request{modern->activation_id, b, AbsentBase{}, {}, zero, {}};
                auto canonical = build_operational_plan(request);
                auto published = inspect_activation_plan(p, s, modern->activation_id);
                if (modern->plan_digest != canonical.digest ||
                    published.digest != canonical.digest || bytes != serialize(*modern) ||
                    (authorized && *authorized != modern->activation_id))
                    fail(InitialStage::Classification, InitialCause::Conflict);
                selection_preparation = true;
                continue;
            }
            fail(InitialStage::Classification, InitialCause::Conflict);
        }
        for (const auto &name : a::entries(r.get()))
            if (!left || name != ".athena-activation")
                fail(InitialStage::Classification, InitialCause::Conflict);
        if (selection_preparation && !authorized)
            return {InitialScenario::RecoveryRequired, left, {}};
        return {preparation || left ? InitialScenario::VerifiedPreparation : InitialScenario::Fresh,
                left,
                {}};
    } catch (const InitialActivationError &) {
        throw;
    } catch (...) {
        throw InitialActivationError(InitialStage::Classification, InitialCause::PrimitiveFailure,
                                     ProjectionOutcome::OutcomeUncertain, std::current_exception());
    }
}
} // namespace
InitialObservation inspect_initial(const ProjectionBinding &b, const store::ObjectStore &o,
                                   const ProjectionState &s, ProjectionIo io) {
    return inspect(b, o, s, std::move(io));
}
ProjectionOutcome ensure_initial_generation(const ProjectionState &s, const d::Syscalls &ops) {
    bool touched = false;
    try {
        s.revalidate();
        auto root = d::open_directory(s.generations());
        a::private_directory(root.get());
        d::require_durable_directory(root.get(), ops);
        const auto bytes = generation::serialize(generation::make_generation(0, {}));
        bool published = exists(root.get(), "0"),
             prepared = exists(root.get(), ".initial-generation");
        if (published && prepared)
            fail(InitialStage::GenerationZero, InitialCause::Conflict);
        if (!published && !prepared) {
            // Preserve even incomplete residue. Never invoke generic publication
            // cleanup after a write/barrier/rename with uncertain outcome.
            touched = true;
            if ((ops.mkdir ? ops.mkdir : ::mkdirat)(root.get(), ".initial-generation", 0700) < 0)
                throw std::system_error(errno, std::generic_category(), "Prepare generation zero");
            auto dir = d::open_at(root.get(), ".initial-generation", O_RDONLY | O_DIRECTORY);
            auto file = d::open_at(dir.get(), "generation.toml", O_WRONLY | O_CREAT | O_EXCL);
            d::complete_write(file.get(), bytes, ops);
        }
        const std::string name = published ? "0" : ".initial-generation";
        auto dir = d::open_at(root.get(), name, O_RDONLY | O_DIRECTORY);
        a::private_directory(dir.get());
        a::same_mount(root.get(), dir.get());
        if (a::entries(dir.get()) != std::vector<std::string>{"generation.toml"} ||
            a::read_file(dir.get(), "generation.toml") != bytes)
            fail(InitialStage::GenerationZero, InitialCause::Conflict);
        auto file = d::open_at(dir.get(), "generation.toml", O_RDONLY);
        a::private_file(file.get());
        a::unchanged(root.get(), name, dir.get());
        a::unchanged(dir.get(), "generation.toml", file.get());
        touched = true;
        d::file_barrier(file.get(), ops);
        d::directory_barrier(dir.get(), published, ops);
        s.revalidate();
        a::unchanged(root.get(), name, dir.get());
        if (!published &&
            d::publish_noreplace(root.get(), name, "0", ops) != d::Publication::Published)
            fail(InitialStage::GenerationZero, InitialCause::Conflict);
        d::directory_barrier(root.get(), true, ops);
        a::unchanged(root.get(), "0", dir.get());
        a::unchanged(dir.get(), "generation.toml", file.get());
        if (a::read_file(dir.get(), "generation.toml") != bytes)
            fail(InitialStage::GenerationZero, InitialCause::Conflict);
        return ProjectionOutcome::MutatedDurably;
    } catch (InitialActivationError &e) {
        if (touched)
            e.outcome = ProjectionOutcome::OutcomeUncertain;
        throw;
    } catch (...) {
        throw InitialActivationError(InitialStage::GenerationZero, InitialCause::PrimitiveFailure,
                                     touched ? ProjectionOutcome::OutcomeUncertain
                                             : ProjectionOutcome::NotMutated,
                                     std::current_exception());
    }
}
OperationalActivationResult bootstrap_initial(const ProjectionBinding &b,
                                              const store::ObjectStore &o, const ProjectionState &s,
                                              const ActivationId &id, ProjectionIo io) {
    auto initial = inspect(b, o, s, io, id);
    if (initial.scenario == InitialScenario::LegacyNonempty)
        fail(InitialStage::Migration, InitialCause::ExplicitMigrationRequired);
    if (initial.scenario == InitialScenario::Selected)
        fail(InitialStage::Recovery, InitialCause::Conflict);
    gc_compatible(b);
    bool touched = false;
    try {
        if (initial.scenario == InitialScenario::Fresh ||
            initial.scenario == InitialScenario::VerifiedPreparation) {
            touched = true;
            ensure_initial_generation(s, io.durable);
        }
        if (!initial.bound)
            touched = true;
        auto p = context(b, o, s, initial.bound, std::move(io));
        auto target = observed(b, initial.current ? selected_generation(*initial.current) : 0);
        OperationalPlanInput request{id, b, AbsentBase{}, {}, target, {}};
        if (initial.current) {
            request.base = SelectedBase{*initial.current, target.digest, EmptyOwnership{}};
            request.base_generation = target;
        }
        return activate_operational(p, s, request);
    } catch (const OperationalActivationError &) {
        throw;
    } catch (InitialActivationError &e) {
        if (touched)
            e.outcome = ProjectionOutcome::OutcomeUncertain;
        throw;
    } catch (...) {
        throw InitialActivationError(InitialStage::GenerationRead, InitialCause::PrimitiveFailure,
                                     ProjectionOutcome::OutcomeUncertain, std::current_exception());
    }
}
OperationalActivationResult migrate_initial(const ProjectionBinding &b, const store::ObjectStore &o,
                                            const ProjectionState &s, const ActivationId &id,
                                            const AdoptionId &adoption, ProjectionIo io) {
    auto initial = inspect_initial(b, o, s, io);
    if (initial.scenario != InitialScenario::LegacyNonempty)
        fail(InitialStage::Migration, InitialCause::Conflict);
    gc_compatible(b);
    // Establish only the existing C protocol's explicit authority binding, under
    // State -> StoreGc, before D3; never instantiate/register TransactionPins.
    // Once bound, State prevents this authority's GC crossing the D3/D6 interval.
    try {
        store::StoreGcLock gc(b.store_authority);
        store::AuthorityContext(b.store_authority, b.generation_authority)
            .validate_binding(true, io.durable, d::ReplacementFailure::Preserve);
    } catch (...) {
        throw InitialActivationError(InitialStage::StoreBinding, InitialCause::StoreBindingConflict,
                                     ProjectionOutcome::OutcomeUncertain, std::current_exception());
    }
    try {
        auto p = context(b, o, s, initial.bound, std::move(io));
        auto observation = observed(b, selected_generation(*initial.current));
        auto doc = generation::validate_generation_document(observation.document, observation.id);
        std::vector<LegacySource> sources;
        for (const auto &entry : doc.generation.entries)
            sources.push_back(seal_legacy(p, s, doc.generation.id, entry.store_path).source);
        adopt_legacy_projection(p, s, adoption, sources);
        // Record is observation only; D6/D7 independently validate its authority
        // and hard-link witnesses. No scan is interpreted as ownership.
        auto root = d::open_directory(b.projection_root);
        auto admin = d::open_at(root.get(), ".athena-activation", O_RDONLY | O_DIRECTORY);
        auto adoptions = d::open_at(admin.get(), "adoptions", O_RDONLY | O_DIRECTORY);
        auto container = d::open_at(adoptions.get(), adoption.value(), O_RDONLY | O_DIRECTORY);
        auto record = parse_adoption_record(a::read_file(container.get(), "adoption.toml"));
        if (record.id != adoption || record.binding != b ||
            CurrentSelection(record.current) != *initial.current ||
            record.generation_digest != doc.digest)
            fail(InitialStage::Migration, InitialCause::Conflict);
        OperationalGeneration target{doc.generation.id, doc.digest, doc.bytes, {}};
        for (std::size_t n = 0; n < record.sources.size(); ++n) {
            OperationalSourceProjection projection{record.sources[n], {}, {}};
            for (const auto &entry : record.entries)
                if (entry.source == n)
                    projection.links.push_back(entry.path);
            // Artifact empty directories are explicit projection members. Parent
            // directories of legacy links are derived by D8.1.
            if (const auto *artifact_source = std::get_if<ArtifactSource>(&projection.source)) {
                store::StoreGcLock gc(b.store_authority);
                store::AuthorityContext(b.store_authority, b.generation_authority)
                    .validate_binding(false);
                o.verify(artifact_source->artifact_id);
                auto payload = d::open_directory(o.object_path(artifact_source->artifact_id));
                auto tree = artifact::scan_bounded(payload.get());
                if (artifact::derive_artifact_id(tree) != artifact_source->artifact_id)
                    fail(InitialStage::Migration, InitialCause::Conflict);
                for (const auto &e : tree.entries)
                    if (std::holds_alternative<artifact::Directory>(e.value))
                        projection.directories.push_back(e.path);
            }
            target.sources.push_back(std::move(projection));
        }
        OperationalPlanInput request{
            id,     b,      SelectedBase{*initial.current, doc.digest, AdoptionOwnership{adoption}},
            target, target, {}};
        for (const auto &entry : record.entries)
            request.old_witnesses.push_back({entry.path, entry.witness});
        return activate_operational(p, s, request);
    } catch (const OperationalActivationError &) {
        throw;
    } catch (InitialActivationError &e) {
        e.outcome = ProjectionOutcome::OutcomeUncertain;
        throw;
    } catch (...) {
        throw InitialActivationError(InitialStage::Migration, InitialCause::PrimitiveFailure,
                                     ProjectionOutcome::OutcomeUncertain, std::current_exception());
    }
}
ReconciliationResult recover_initial(const ProjectionBinding &b, const store::ObjectStore &o,
                                     const ProjectionState &s, ProjectionIo io) {
    try {
        authority(b, s);
        const bool c_bound = store_binding(b);
        gc_compatible(b);
        auto p = context(b, o, s, true, std::move(io));
        auto current = inspect_selection(p, s);
        const auto *selected = current ? std::get_if<CurrentV1>(&*current) : nullptr;
        if (!selected)
            fail(InitialStage::Recovery, InitialCause::Conflict);
        auto generation = observed(b, selected->generation);
        if (!c_bound &&
            !empty(generation::validate_generation_document(generation.document, generation.id)
                       .generation))
            throw InitialActivationError(InitialStage::StoreBinding,
                                         InitialCause::StoreBindingConflict,
                                         ProjectionOutcome::OutcomeUncertain);
        PlanIdentity identity{selected->activation_id, selected->plan_digest};
        recover_selection(p, s, identity);
        return reconcile_selection(p, s, identity);
    } catch (InitialActivationError &e) {
        // A failed recovery observation cannot certify the previous selection's
        // durability. No namespace mutation in this attempt is not a rollback proof.
        if (e.cause != InitialCause::Conflict)
            e.outcome = ProjectionOutcome::OutcomeUncertain;
        throw;
    } catch (...) {
        throw InitialActivationError(InitialStage::Recovery, InitialCause::PrimitiveFailure,
                                     ProjectionOutcome::OutcomeUncertain, std::current_exception());
    }
}
} // namespace athena::activation
