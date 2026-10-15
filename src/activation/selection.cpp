// SPDX-License-Identifier: GPL-3.0-or-later
#include "selection.hpp"
#include "legacy.hpp"
#include "../artifact/identity.hpp"
#include "../store/transaction_pins.hpp"
#include "../store/object_store_detail.hpp"
#include "../installed/environment.hpp"
#include <fcntl.h>
#include <linux/fs.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <system_error>

namespace athena::activation {
namespace d = core::detail;
namespace {
[[noreturn]] void reject(ExecutionCause c, std::string_view text, int error = 0,
                        ProjectionOutcome o = ProjectionOutcome::NotMutated) {
    throw ExecutionError(c, o, error, text);
}
void parents(std::set<std::string>& directories, std::string path) {
    while (true) { auto n = path.rfind('/'); if (n == path.npos) break;
        path.resize(n); directories.insert(path); }
}
struct SourceProjection { std::string locator; std::set<std::string> links, directories; };
struct Membership { std::map<std::string, std::string> links; std::set<std::string> directories; };
CurrentV1 target(const InspectedActivationPlan& doc) {
    return {doc.intent.target_generation, doc.intent.activation_id, doc.digest};
}
bool base_matches(const ActivationBase& base, const std::optional<CurrentSelection>& actual) {
    if (auto selected = std::get_if<SelectedBase>(&base)) return actual && *actual == selected->current;
    return !actual;
}
}
ExecutionError::ExecutionError(ExecutionCause c, ProjectionOutcome o, int e, std::string_view message)
    : std::runtime_error(std::string(message)), cause(c), outcome(o), error(e) {}
generation::ValidatedGenerationDocument ExecutionAccess::generation(const ProjectionContext& p, std::uint64_t id) {
    const auto name = std::to_string(id);
    open_how how{}; how.flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV;
    const int raw = p.io_.open(p.generation_.get(), name.c_str(), how);
    if (raw < 0) reject(ExecutionCause::UnsafeMetadata, "Constrained generation directory open", errno);
    auto fd = d::Fd::adopt(raw, p.io_.durable);
    const auto s = p.identity(fd.get()), root = p.identity(p.generation_.get());
    if (!S_ISDIR(s.stx_mode) || s.stx_uid != ::geteuid() || (s.stx_mode & 07022) ||
        s.stx_mnt_id != root.stx_mnt_id || s.stx_dev_major != root.stx_dev_major || s.stx_dev_minor != root.stx_dev_minor)
        reject(ExecutionCause::UnsafeMetadata, "Unsafe generation directory metadata/mount");
    p.unchanged(p.generation_.get(), name, fd.get());
    const auto file_instance = p.identity(fd.get(), "generation.toml", false);
    auto doc = generation::validate_generation_document(read(p, fd.get(), "generation.toml", max_document_size, true), id);
    p.barrier(fd.get(), true); p.barrier(p.generation_.get(), true);
    if (!plan_detail::OperationEvidence::same_observation(file_instance, p.identity(fd.get(), "generation.toml", false)))
        reject(ExecutionCause::UnsafeMetadata, "Generation file instance changed across barriers");
    p.unchanged(p.generation_.get(), name, fd.get()); return doc;
}
std::string ExecutionAccess::read(const ProjectionContext& p, int parent, std::string_view name,
                                  std::size_t limit, bool sync) {
    const std::string leaf(name);
    open_how how{}; how.flags = O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV;
    const int raw = p.io_.open(parent, leaf.c_str(), how);
    if (raw < 0) reject(ExecutionCause::Io, "Constrained selection record open", errno);
    auto fd = d::Fd::adopt(raw, p.io_.durable);
    const auto before = p.identity(fd.get()), ds = p.identity(parent);
    if (!S_ISREG(before.stx_mode) || before.stx_uid != ::geteuid() || before.stx_nlink != 1 ||
        (before.stx_mode & 07022) || before.stx_size > limit ||
        before.stx_mnt_id != ds.stx_mnt_id || before.stx_dev_major != ds.stx_dev_major || before.stx_dev_minor != ds.stx_dev_minor)
        reject(ExecutionCause::UnsafeMetadata, "Unsafe selection record metadata/mount");
    auto check = [&] {
        if (!plan_detail::OperationEvidence::same_observation(before, p.identity(fd.get())) ||
            !plan_detail::OperationEvidence::same_observation(before, p.identity(parent, name, false)))
            reject(ExecutionCause::UnsafeMetadata, "Selection record substituted");
    };
    check(); std::string bytes; char buffer[4096];
    for (;;) {
        auto n = p.io_.pread(fd.get(), buffer, sizeof(buffer), bytes.size());
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) reject(ExecutionCause::Io, "Selection record read", errno);
        if (static_cast<std::size_t>(n) > sizeof(buffer) || static_cast<std::size_t>(n) > limit - bytes.size())
            reject(ExecutionCause::UnsafeMetadata, "Selection record read bound");
        if (!n) break;
        bytes.append(buffer, n);
    }
    check(); if (bytes.size() != before.stx_size) reject(ExecutionCause::UnsafeMetadata, "Selection record EOF mismatch");
    if (sync) {
        int rc; do { rc = p.io_.fsync(fd.get()); } while (rc < 0 && errno == EINTR);
        if (rc < 0) reject(ExecutionCause::Durability, "Selected current file barrier", errno, ProjectionOutcome::OutcomeUncertain);
        check();
    }
    return bytes;
}
std::optional<CurrentSelection> ExecutionAccess::current(const ProjectionContext& p, bool sync) {
    struct statx s{};
    if (p.io_.stat(p.generation_.get(), "current", AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_MNT_ID, &s) < 0) {
        if (errno == ENOENT) return {};
        reject(ExecutionCause::Io, "Current inspection", errno);
    }
    auto value = parse_current_selection(read(p, p.generation_.get(), "current", max_current_size, sync));
    if (std::holds_alternative<CurrentV1>(value) && (s.stx_mode & 07777) != 0600)
        reject(ExecutionCause::UnsafeMetadata, "CurrentV1 requires mode 0600");
    if (sync) p.barrier(p.generation_.get(), true);
    if (!plan_detail::OperationEvidence::same_observation(s, p.identity(p.generation_.get(), "current", false)))
        reject(ExecutionCause::UnsafeMetadata, "Current instance changed across read/barriers", 0,
            sync ? ProjectionOutcome::OutcomeUncertain : ProjectionOutcome::NotMutated);
    return value;
}
ExecutionAccess::Authorized ExecutionAccess::authorize(ProjectionContext& p, const ProjectionState& state,
                                                       const PlanIdentity& expected) {
    p.state(state); p.revalidate();
    // A full D5 scan must succeed; incomplete or unrelated corrupt plans block execution.
    (void)PlanAccess::retention(p);
    auto doc = recover_activation_plan(p, state, expected).document;
    auto target_doc = generation(p, doc.intent.target_generation);
    if (target_doc.digest != doc.intent.target_generation_digest)
        reject(ExecutionCause::GenerationMismatch, "Target generation document digest mismatch");
    if (target_doc.generation.id == 0 && (!target_doc.generation.entries.empty() || !target_doc.generation.realizations.empty()))
        reject(ExecutionCause::MembershipMismatch, "Generation 0 must have empty payload membership");
    if (target_doc.generation.compatibility == generation::GenerationCompatibility::Modern) {
        auto environment = target_doc.generation;
        installed::capture_legacy_semantics(environment, max_document_size);
        installed::validate_environment(environment);
    }
    std::optional<generation::ValidatedGenerationDocument> base_doc;
    if (auto base = std::get_if<SelectedBase>(&doc.intent.base)) {
        base_doc = generation(p, selected_generation(base->current));
        if (base_doc->digest != base->generation_digest)
            reject(ExecutionCause::GenerationMismatch, "Base generation document digest mismatch");
        if (base_doc->generation.id == 0 && (!base_doc->generation.entries.empty() || !base_doc->generation.realizations.empty()))
            reject(ExecutionCause::MembershipMismatch, "Base generation 0 must have empty payload membership");
        if (std::holds_alternative<EmptyOwnership>(base->ownership) &&
            (!base_doc->generation.entries.empty() || !base_doc->generation.realizations.empty()))
            reject(ExecutionCause::MembershipMismatch, "Empty ownership requires empty membership");
    } else if (target_doc.generation.id != 0 || !target_doc.generation.entries.empty() ||
               !target_doc.generation.realizations.empty() || !doc.plan.sources.empty() || !doc.plan.operations.empty())
        reject(ExecutionCause::MembershipMismatch, "Absent selection requires empty generation 0 bootstrap");

    store::ObjectStore objects(p.binding_.store_authority);
    std::vector<SourceProjection> sources;
    std::size_t projection_entries = 0, projection_bytes = 0;
    std::map<std::string, std::size_t> target_legacy, base_legacy;
    for (const auto& entry : target_doc.generation.entries) ++target_legacy[entry.store_path];
    if (base_doc) for (const auto& entry : base_doc->generation.entries) ++base_legacy[entry.store_path];
    for (const auto* members : {&target_legacy, &base_legacy}) for (const auto& [locator, count] : *members)
        if (count != 1) reject(ExecutionCause::MembershipMismatch, "Ambiguous legacy locator membership");
    for (const auto& source : doc.plan.sources) {
        SourceProjection resolved;
        if (auto object = std::get_if<ArtifactSource>(&source)) {
            store::StoreGcLock gc(objects.root());
            store::AuthorityContext(objects.root(), p.binding_.generation_authority).validate_binding(false);
            objects.verify(object->artifact_id);
            resolved.locator = objects.object_path(object->artifact_id).native();
            auto tree = artifact::scan(resolved.locator);
            if (artifact::derive_artifact_id(tree) != object->artifact_id)
                reject(ExecutionCause::MembershipMismatch, "Artifact changed during authorization");
            // Reuse B's synchronization mechanics, including older valid objects;
            // verification alone is not payload durability evidence. No publication,
            // chmod or namespace initialization occurs here.
            auto pinned = d::open_directory(resolved.locator);
            const auto before = p.identity(pinned.get());
            store::object_detail::synchronize(resolved.locator, tree, p.io_.durable);
            for (auto path = std::filesystem::path(resolved.locator).parent_path();;) {
                auto parent = d::open_directory(path);
                d::directory_barrier(parent.get(), true, p.io_.durable);
                if (path.native() == p.binding_.store_authority) break;
                if (path == path.root_path()) reject(ExecutionCause::MembershipMismatch, "Artifact escaped bound store");
                path = path.parent_path();
            }
            auto named = d::open_directory(resolved.locator);
            if (!plan_detail::OperationEvidence::same_observation(before, p.identity(named.get())))
                reject(ExecutionCause::UnsafeMetadata, "Artifact root instance changed across synchronization");
            objects.verify(object->artifact_id);
            for (const auto& entry : tree.entries) {
                if (std::holds_alternative<artifact::Directory>(entry.value)) resolved.directories.insert(entry.path);
                else resolved.links.insert(entry.path);
            }
        } else {
            const auto locator = std::visit([](const auto& value) -> std::string {
                if constexpr (requires { value.locator; }) return value.locator; else return {};
            }, source);
            auto member = [&](const auto& members) {
                auto found = members.find(locator); return found != members.end() && found->second == 1;
            };
            std::optional<std::uint64_t> generation;
            if (member(target_legacy)) generation = target_doc.generation.id;
            else if (base_doc && member(base_legacy)) generation = base_doc->generation.id;
            if (!generation) reject(ExecutionCause::MembershipMismatch, "Legacy source is not generation membership");
            LegacySource legacy = std::holds_alternative<LegacyTreeSource>(source) ? LegacySource(std::get<LegacyTreeSource>(source)) : LegacySource(std::get<LegacyAliasSource>(source));
            auto inventory = verify_legacy_seal(p, state, *generation, legacy);
            resolved.locator = locator;
            resolved.links.insert(inventory.projection.begin(), inventory.projection.end());
        }
        for (const auto& path : resolved.links) { validate_projection_path(path); parents(resolved.directories, path); }
        for (const auto& path : resolved.directories) validate_projection_path(path);
        for (const auto* entries : {&resolved.links, &resolved.directories}) for (const auto& path : *entries) {
            const auto bytes = path.size() + resolved.locator.size();
            if (++projection_entries > max_retention_references || bytes > max_retention_bytes - projection_bytes)
                reject(ExecutionCause::MembershipMismatch, "Cumulative execution projection budget exceeded");
            projection_bytes += bytes;
        }
        sources.push_back(std::move(resolved));
    }
    std::map<std::string, std::size_t> artifact_index, legacy_index;
    for (std::size_t n = 0; n < sources.size(); ++n) {
        if (auto object = std::get_if<ArtifactSource>(&doc.plan.sources[n])) artifact_index.emplace(object->artifact_id.value(), n);
        else legacy_index.emplace(sources[n].locator, n);
    }
    auto membership = [&](const generation::Generation& g) {
        Membership m;
        auto append = [&](std::size_t n) {
            for (const auto& path : sources[n].links)
                if (!m.links.emplace(path, sources[n].locator + "/" + path).second)
                    reject(ExecutionCause::MembershipMismatch, "Conflicting generation projections");
            m.directories.insert(sources[n].directories.begin(), sources[n].directories.end());
        };
        for (const auto& entry : g.entries) {
            auto found = legacy_index.find(entry.store_path);
            if (found == legacy_index.end()) reject(ExecutionCause::MembershipMismatch, "Missing legacy generation source");
            append(found->second);
        }
        std::set<std::string> artifacts;
        for (const auto& record : g.realizations) {
            if (!artifacts.insert(record.artifact_id.value()).second) continue;
            auto found = artifact_index.find(record.artifact_id.value());
            if (found == artifact_index.end()) reject(ExecutionCause::MembershipMismatch, "Missing artifact generation source");
            append(found->second);
        }
        for (const auto& [path, ignored] : m.links)
            if (m.directories.contains(path)) reject(ExecutionCause::MembershipMismatch, "Generation link/directory collision");
        return m;
    };
    auto desired = membership(target_doc.generation);
    auto old = base_doc ? membership(base_doc->generation) : Membership{};
    Authorized result{std::move(doc), {}, {}, old.directories, desired.directories};
    auto add = [&](auto& map, const ActivationOperation& op, std::uint64_t n, const WitnessReference& witness) {
        if (n >= sources.size() || !sources[n].links.contains(op.path) ||
            !map.emplace(op.path, Link{sources[n].locator + "/" + op.path, witness}).second)
            reject(ExecutionCause::MembershipMismatch, "Operation is not an exact source projection member");
    };
    std::set<std::string> ensured;
    for (const auto& op : result.document.plan.operations) std::visit([&](const auto& value) {
        if constexpr (requires { value.old_source; }) add(result.old_links, op, value.old_source, value.old_witness);
        if constexpr (requires { value.new_source; }) add(result.desired_links, op, value.new_source, value.new_witness);
        if constexpr (std::is_same_v<std::decay_t<decltype(value)>, EnsureDirectory>) ensured.insert(op.path);
    }, op.state);
    auto equal = [](const auto& links, const auto& expected_links) {
        if (links.size() != expected_links.size()) return false;
        for (const auto& [path, link] : links) { auto found = expected_links.find(path);
            if (found == expected_links.end() || found->second != link.target) return false; }
        return true;
    };
    if (!equal(result.old_links, old.links) || !equal(result.desired_links, desired.links) || ensured != desired.directories)
        reject(ExecutionCause::MembershipMismatch, "Plan does not cover exact complete generation projections");
    for (const auto& [path, link] : result.desired_links)
        if (result.old_directories.contains(path))
            reject(ExecutionCause::MembershipMismatch, "Directory-to-link transition requires undefined deletion authority");
    if (auto base = std::get_if<SelectedBase>(&result.document.intent.base)) {
        if (auto adoption = std::get_if<AdoptionOwnership>(&base->ownership)) {
            auto root = p.directory(p.admin_.get(), "adoptions", true);
            auto container = p.directory(root.get(), adoption->id.value(), true);
            auto record = parse_adoption_record(read(p, container.get(), "adoption.toml", max_document_size, true));
            if (record.id != adoption->id || record.binding != p.binding_ ||
                CurrentSelection(record.current) != base->current || record.generation_digest != base->generation_digest ||
                record.entries.size() != result.old_links.size() ||
                std::set<std::string>(record.directories.begin(), record.directories.end()) != result.old_directories)
                reject(ExecutionCause::MembershipMismatch, "Adoption does not authorize exact selected base");
            for (const auto& source : record.sources)
                if (std::find(result.document.plan.sources.begin(), result.document.plan.sources.end(), source) == result.document.plan.sources.end())
                    reject(ExecutionCause::MembershipMismatch, "Adoption source identity/seal mismatch");
            for (const auto& entry : record.entries) {
                auto old = result.old_links.find(entry.path);
                if (old == result.old_links.end() || old->second != Link{entry.target, entry.witness})
                    reject(ExecutionCause::MembershipMismatch, "Adoption old projection/witness mismatch");
            }
            p.barrier(container.get(), true); p.barrier(root.get(), true);
        }
        if (auto v1 = std::get_if<CurrentV1>(&base->current)) {
            auto predecessor = recover_activation_plan(p, state, {v1->activation_id, v1->plan_digest}).document;
            if (predecessor.intent.target_generation != v1->generation || predecessor.intent.target_generation_digest != base->generation_digest)
                reject(ExecutionCause::GenerationMismatch, "Predecessor selected document identity mismatch");
            auto exact_source = [&](std::size_t n) {
                const auto& source = result.document.plan.sources.at(n);
                if (std::find(predecessor.plan.sources.begin(), predecessor.plan.sources.end(), source) == predecessor.plan.sources.end())
                    reject(ExecutionCause::MembershipMismatch, "Base source identity/seal differs from selected predecessor");
            };
            for (const auto& entry : base_doc->generation.entries) exact_source(legacy_index.at(entry.store_path));
            for (const auto& record : base_doc->generation.realizations) exact_source(artifact_index.at(record.artifact_id.value()));
            std::map<std::string, WitnessReference> witnesses;
            for (const auto& op : predecessor.plan.operations) std::visit([&](const auto& value) {
                if constexpr (requires { value.new_witness; }) witnesses.emplace(op.path, value.new_witness);
            }, op.state);
            if (witnesses.size() != result.old_links.size()) reject(ExecutionCause::MembershipMismatch, "Predecessor projection incomplete");
            for (const auto& [path, link] : result.old_links)
                if (!witnesses.contains(path) || witnesses.at(path) != link.witness)
                    reject(ExecutionCause::MembershipMismatch, "Old witness is not predecessor desired witness");
        }
    }
    p.revalidate(); return result;
}
void ExecutionAccess::require_selected(const ProjectionContext& p, const Authorized& a) {
    const auto actual = current(p);
    if (!actual || *actual != CurrentSelection(target(a.document)))
        reject(ExecutionCause::SelectionConflict, "Reconciliation requires exact selected CurrentV1");
}
void ExecutionAccess::directory(const ProjectionContext& p, std::string_view path, bool sync) {
    auto fd = p.directory(p.root_.get(), path);
    const auto s = p.identity(fd.get());
    if (s.stx_uid != ::geteuid() || (s.stx_mode & 07022))
        reject(ExecutionCause::UnsafeMetadata, "Unsafe payload directory owner/mode");
    if (sync) { p.barrier(fd.get(), true); auto [parent, leaf] = p.parent(path); p.barrier(parent.get(), true); }
}
void ExecutionAccess::absent(const ProjectionContext& p, std::string_view path, bool sync) {
    auto [parent, leaf] = p.parent(path); struct statx s{};
    if (p.io_.stat(parent.get(), leaf.c_str(), AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_MNT_ID, &s) == 0)
        reject(ExecutionCause::IncompleteBase, "Removed path still exists");
    if (errno != ENOENT) reject(ExecutionCause::Io, "Inspect removed path", errno);
    if (sync) p.barrier(parent.get(), true);
}
void ExecutionAccess::verify_base(ProjectionContext& p, const ProjectionState& state, const Authorized& a) {
    for (const auto& [path, link] : a.old_links) p.recover_symlink(state, path, link.target, link.witness);
    for (const auto& path : a.old_directories) directory(p, path, true);
    if (auto base = std::get_if<SelectedBase>(&a.document.intent.base)) {
        if (auto v1 = std::get_if<CurrentV1>(&base->current)) {
            auto predecessor = inspect_activation_plan(p, state, v1->activation_id);
            for (const auto& op : predecessor.plan.operations)
                if (std::holds_alternative<RemoveLink>(op.state) && !a.old_directories.contains(op.path)) absent(p, op.path, true);
        }
    }
    if (std::holds_alternative<AbsentBase>(a.document.intent.base) ||
        (std::get_if<SelectedBase>(&a.document.intent.base) && std::holds_alternative<EmptyOwnership>(std::get<SelectedBase>(a.document.intent.base).ownership))) {
        if (store::administrative::entries(p.root_.get()) != std::vector<std::string>{".athena-activation"})
            reject(ExecutionCause::IncompleteBase, "Generation 0 bootstrap projection is not empty");
    }
}
SelectionResult ExecutionAccess::commit(ProjectionContext& p, const ProjectionState& state, const PlanIdentity& expected, bool recovery) {
    auto a = authorize(p, state, expected);
    const auto actual = current(p);
    const auto selected = CurrentSelection(target(a.document));
    if (actual && *actual == selected) {
        if (current(p, true) != actual) reject(ExecutionCause::SelectionConflict, "Selected current changed during recovery");
        p.revalidate(); return {SelectionState::TargetSelected, ProjectionOutcome::MutatedDurably};
    }
    if (!base_matches(a.document.intent.base, actual)) reject(ExecutionCause::SelectionConflict, "Exact base selection mismatch");
    if (recovery) return {SelectionState::BaseSelected, ProjectionOutcome::NotMutated};
    verify_base(p, state, a);
    const auto bytes = serialize(selected);
    const auto preparation = ".athena-current-" + expected.activation_id.value();
    bool touched = false;
    try {
        open_how how{}; how.flags = O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC; how.mode = 0600;
        how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV;
        const int raw = p.io_.open(p.generation_.get(), preparation.c_str(), how);
        if (raw < 0) {
            if (errno != EEXIST) reject(ExecutionCause::Io, "Exclusive current preparation", errno);
            if (read(p, p.generation_.get(), preparation, max_current_size) != bytes)
                reject(ExecutionCause::PreparationConflict, "Conflicting/incomplete current preparation");
        } else {
            touched = true; auto fd = d::Fd::adopt(raw, p.io_.durable);
            d::complete_write(fd.get(), bytes, p.io_.durable);
        }
        touched = true;
        const auto instance = p.identity(p.generation_.get(), preparation, false);
        if ((instance.stx_mode & 07777) != 0600)
            reject(ExecutionCause::UnsafeMetadata, "Current preparation requires mode 0600");
        if (read(p, p.generation_.get(), preparation, max_current_size, true) != bytes)
            reject(ExecutionCause::PreparationConflict, "Current preparation changed");
        p.barrier(p.generation_.get(), true); p.revalidate();
        if (current(p) != actual) reject(ExecutionCause::SelectionConflict, "Base substituted before commit");
        if (generation(p, a.document.intent.target_generation).digest != a.document.intent.target_generation_digest)
            reject(ExecutionCause::GenerationMismatch, "Target changed before commit");
        if (auto base = std::get_if<SelectedBase>(&a.document.intent.base);
            base && generation(p, selected_generation(base->current)).digest != base->generation_digest)
            reject(ExecutionCause::GenerationMismatch, "Base changed before commit");
        if (read(p, p.generation_.get(), preparation, max_current_size) != bytes)
            reject(ExecutionCause::PreparationConflict, "Current preparation substituted before rename");
        if (!plan_detail::OperationEvidence::same_observation(instance, p.identity(p.generation_.get(), preparation, false)))
            reject(ExecutionCause::PreparationConflict, "Current preparation instance changed across barriers");
        if (p.io_.rename(p.generation_.get(), preparation.c_str(), p.generation_.get(), "current", actual ? 0 : RENAME_NOREPLACE) < 0)
            reject(ExecutionCause::Io, "Atomic current selection rename", errno);
        p.barrier(p.generation_.get(), true);
        if (current(p) != std::optional<CurrentSelection>(selected)) reject(ExecutionCause::SelectionConflict, "Committed current substituted");
        return {SelectionState::TargetSelected, ProjectionOutcome::MutatedDurably};
    } catch (ExecutionError& e) { if (touched) e.outcome = ProjectionOutcome::OutcomeUncertain; throw; }
    catch (ProjectionError& e) { if (touched) e.outcome = ProjectionOutcome::OutcomeUncertain; throw; }
    catch (const d::PersistenceError& e) {
        const auto cause = e.stage == d::PersistenceError::Stage::FileBarrier || e.stage == d::PersistenceError::Stage::DirectoryBarrier ?
            ExecutionCause::Durability : ExecutionCause::Io;
        reject(cause, e.what(), e.error, touched ? ProjectionOutcome::OutcomeUncertain : ProjectionOutcome::NotMutated);
    }
    catch (const std::system_error& e) {
        reject(ExecutionCause::Io, e.what(), e.code().value(), touched ? ProjectionOutcome::OutcomeUncertain : ProjectionOutcome::NotMutated);
    }
    catch (const std::exception& e) { reject(ExecutionCause::Io, e.what(), 0, touched ? ProjectionOutcome::OutcomeUncertain : ProjectionOutcome::NotMutated); }
}
SelectionResult commit_selection(ProjectionContext& p, const ProjectionState& s, const PlanIdentity& id) { return ExecutionAccess::commit(p,s,id,false); }
SelectionResult recover_selection(ProjectionContext& p, const ProjectionState& s, const PlanIdentity& id) { return ExecutionAccess::commit(p,s,id,true); }
std::optional<CurrentSelection> ExecutionAccess::observe(ProjectionContext& p, const ProjectionState& s) {
    p.state(s); p.revalidate(); auto selected = ExecutionAccess::current(p); p.revalidate(); return selected;
}
std::optional<CurrentSelection> inspect_selection(ProjectionContext& p, const ProjectionState& s) {
    return ExecutionAccess::observe(p, s);
}
}
