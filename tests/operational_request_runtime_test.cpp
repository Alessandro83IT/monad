// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation/operational_request_runtime.hpp"
#include "activation/operational_request_codec.hpp"
#include "activation/legacy.hpp"
#include "activation_execution_support.hpp"
#include <atomic>
#include <thread>

using namespace athena::activation;
using namespace execution_test;
namespace {
OperationalGeneration generation(Fixture& f, unsigned n) {
    const auto d = athena::generation::read_generation_document(f.binding.generation_authority, n);
    return {n, d.digest, d.bytes, {}};
}
OperationalPlanInput input(Fixture& f, unsigned activation = 10, unsigned target = 0) {
    return {id(activation), f.binding, AbsentBase{}, {}, generation(f, target), {}};
}
template<class F> void execution_failure(F action, ExecutionCause cause) {
    try { action(); }
    catch (const ExecutionError& e) {
        require(e.cause == cause && e.outcome == ProjectionOutcome::NotMutated, "read-only failure category");
        return;
    }
    throw std::runtime_error("Expected ExecutionError");
}
void unchanged(Fixture& f, ProjectionContext& p) {
    require(!inspect_selection(p, *f.state), "no current publication");
    require(!fs::exists(f.root / "projection/.athena-activation/plans"), "no plan namespace bootstrap");
}
void preparation_and_retry() {
    Fixture f; auto p = f.open();
    // A historical document is retained byte-for-byte, not normalized to v3.
    const std::string historical = "# historical\r\nid=0\r\nentries=[]\r\n";
    write(f.root / "generations/0/generation.toml", historical);
    auto original = input(f);
    auto prepared = prepare_operational_request(p, *f.state, original);
    const auto bytes = serialize_operational_request(prepared);
    require(prepared.input().target.document == historical, "historical exact bytes");
    require(revalidate_operational_request(p, *f.state, prepared) == OperationalRequestState::BaseCurrent,
        "base observation");
    unchanged(f, p);
    original.target.document[0] = 'X'; original.binding.projection_root = "/wrong";
    require(serialize_operational_request(prepared) == bytes, "caller mutation isolated");
    auto decoded = parse_operational_request(bytes);
    require(decoded.identity().digest == prepared.identity().digest, "B1.1 identity preserved");
    auto result = activate_prepared_operational(p, *f.state, decoded);
    require(result.identity.digest == prepared.identity().digest &&
        result.identity.activation_id == prepared.identity().activation_id, "D8 identity pinned");
    require(result.reconciliation.state == ReconciliationState::Complete, "D7 complete");
    require(revalidate_operational_request(p, *f.state, decoded) == OperationalRequestState::TargetSelected,
        "exact retry observation");
    auto again = activate_prepared_operational(p, *f.state, decoded);
    require(again.publication == PlanPublication::AlreadyPublished, "retry same plan");
    require(serialize_operational_request(decoded) == bytes, "no retry rebase");
    auto successor = input(f, 11, 1);
    successor.base = f.base(10, 0, result.identity);
    successor.base_generation = generation(f, 0);
    auto next = prepare_operational_request(p, *f.state, successor);
    activate_prepared_operational(p, *f.state, next);
    execution_failure([&] { revalidate_operational_request(p, *f.state, prepared); },
        ExecutionCause::SelectionConflict);
    require(serialize_operational_request(prepared) == bytes, "old base remains frozen");
}
void invalid_inputs() {
    Fixture f; auto p = f.open(); auto raw = input(f);
    const auto prepared = prepare_operational_request(p, *f.state, raw);
    const PlanDigest wrong(d1_test::digest('f'));
    d1_test::rejects([&] { prepare_operational_request(p, *f.state, raw, wrong); }, SchemaErrorCode::Reference);
    d1_test::rejects([&] { revalidate_operational_request(p, *f.state, prepared, wrong); }, SchemaErrorCode::Reference);
    auto bad = raw; bad.base_generation = raw.target;
    d1_test::rejects([&] { prepare_operational_request(p, *f.state, bad); }, SchemaErrorCode::Reference);
    bad = raw; bad.target.document = "not TOML";
    rejected([&] { prepare_operational_request(p, *f.state, bad); });
    bad = raw; bad.target.digest = athena::generation::GenerationDocumentDigest(d1_test::digest('a'));
    d1_test::rejects([&] { prepare_operational_request(p, *f.state, bad); }, SchemaErrorCode::Reference);
    bad = raw; bad.old_witnesses.push_back({"bin/tool", {id(9), 0}});
    rejected([&] { prepare_operational_request(p, *f.state, bad); });
    bad = raw; bad.target.sources.push_back({LegacyTreeSource{f.binding.store_authority + "/ghost",
        LegacySealDigest(d1_test::digest('a'))}, {"bin/tool"}, {}});
    rejected([&] { prepare_operational_request(p, *f.state, bad); });
    bad = raw; bad.binding.projection_root = "/unrelated";
    auto claims = PreparedOperationalRequest::create(bad);
    execution_failure([&] { revalidate_operational_request(p, *f.state, claims); }, ExecutionCause::SelectionConflict);
    f.state.reset();
    {
        Fixture other;
        execution_failure([&] { revalidate_operational_request(p, *other.state, prepared); }, ExecutionCause::SelectionConflict);
    }
    f.state = std::make_unique<ProjectionState>(f.binding.generation_authority);
    unchanged(f, p);
}
void changed_documents() {
    for (unsigned mode = 0; mode != 5; ++mode) {
        Fixture f; auto p = f.open(); auto prepared = prepare_operational_request(p, *f.state, input(f));
        const auto file = f.root / "generations/0/generation.toml";
        if (mode == 0) write(file, "# different valid exact bytes\nid=0\nentries=[]\n");
        if (mode == 1) write(file, "id=[");
        if (mode == 2) fs::remove(file);
        if (mode == 3) {
            fs::rename(file, file.string() + ".original"); fs::create_symlink(file.string() + ".original", file);
        }
        if (mode == 4) write(file, std::string(max_document_size + 1, '#'));
        rejected([&] { revalidate_operational_request(p, *f.state, prepared); });
        try { activate_prepared_operational(p, *f.state, prepared); throw std::runtime_error("expected D8 failure"); }
        catch (const OperationalActivationError& e) {
            require(e.stage == OperationalStage::Identity && e.failure == OperationalFailure::BeforeSelection,
                "D8 rechecks documents before publication");
        }
        unchanged(f, p);
    }
    Fixture f; auto p = f.open(); auto first = prepare_operational_request(p, *f.state, input(f));
    auto result = activate_prepared_operational(p, *f.state, first);
    auto raw = input(f, 11, 1); raw.base = f.base(10, 0, result.identity); raw.base_generation = generation(f, 0);
    auto next = prepare_operational_request(p, *f.state, raw);
    write(f.root / "generations/0/generation.toml", "# altered base\nid=0\nentries=[]\n");
    execution_failure([&] { revalidate_operational_request(p, *f.state, next); }, ExecutionCause::GenerationMismatch);
    require(!fs::exists(f.root / "projection/.athena-activation/plans" / id(11).value()), "no successor publication");
    write(f.root / "generations/0/generation.toml", raw.base_generation->document);
    const auto selected = activate_prepared_operational(p, *f.state, next);
    const auto frozen = serialize_operational_request(next);
    write(f.root / "generations/0/generation.toml", "# altered base during target retry\nid=0\nentries=[]\n");
    execution_failure([&] { revalidate_operational_request(p, *f.state, next); }, ExecutionCause::GenerationMismatch);
    try { activate_prepared_operational(p, *f.state, next); throw std::runtime_error("expected retry document rejection"); }
    catch (const OperationalActivationError& e) {
        require(e.stage == OperationalStage::Identity && e.failure == OperationalFailure::AfterSelection && e.underlying,
            "selected retry still validates original base document");
        try { std::rethrow_exception(e.underlying); }
        catch (const ExecutionError& original) { require(original.cause == ExecutionCause::GenerationMismatch,
            "retry retains document failure cause"); }
    }
    require(inspect_selection(p, *f.state) == std::optional<CurrentSelection>(
        CurrentV1{1, id(11), selected.identity.digest}) && serialize_operational_request(next) == frozen,
        "failed retry preserves selected target and frozen original base");
}
void competing_requests() {
    Fixture f; auto p = f.open();
    auto a = prepare_operational_request(p, *f.state, input(f, 10));
    auto b = prepare_operational_request(p, *f.state, input(f, 11));
    // Release the preparation interval, model a competing committed request,
    // then reacquire State: no cached observation may authorize B's old base.
    f.state.reset(); f.state = std::make_unique<ProjectionState>(f.binding.generation_authority);
    activate_prepared_operational(p, *f.state, a);
    execution_failure([&] { revalidate_operational_request(p, *f.state, b); }, ExecutionCause::SelectionConflict);
    try { activate_prepared_operational(p, *f.state, b); throw std::runtime_error("expected conflict"); }
    catch (const OperationalActivationError& e) {
        require(e.failure == OperationalFailure::BeforeSelection && e.stage == OperationalStage::Identity,
            "competing request rejected by D8");
    }
    require(!fs::exists(f.root / "projection/.athena-activation/plans" / id(11).value()), "no rebase publication");
    // Concurrent read consumers have independent snapshots; neither can mutate
    // the prepared request through decoder/encoder aliases.
    const auto bytes = serialize_operational_request(a);
    std::atomic<bool> okay{true};
    auto reader = [&] {
        try { for (unsigned n = 0; n != 20; ++n)
            if (serialize_operational_request(parse_operational_request(bytes)) != bytes) okay = false;
        } catch (...) { okay = false; }
    };
    std::thread one(reader), two(reader); one.join(); two.join();
    require(okay, "concurrent immutable request consumption");
}
void retry_digest_and_inspection() {
    Fixture f; auto p = f.open(); const auto prepared = prepare_operational_request(p, *f.state, input(f));
    activate_prepared_operational(p, *f.state, prepared);
    const auto file = f.root / "generations/current";
    write(file, serialize(CurrentSelection(CurrentV1{0, id(10), PlanDigest(d1_test::digest('f'))})));
    execution_failure([&] { revalidate_operational_request(p, *f.state, prepared); }, ExecutionCause::SelectionConflict);
    ProjectionIo io; const auto stat = io.stat; unsigned faults = 0;
    io.stat = [&](int fd, const char* name, int flags, unsigned mask, struct statx* out) {
        if (std::string_view(name) == "current") { ++faults; errno = EIO; return -1; }
        return stat(fd, name, flags, mask, out);
    };
    auto faulty = f.open(io);
    execution_failure([&] { revalidate_operational_request(faulty, *f.state, prepared); }, ExecutionCause::Io);
    try { activate_prepared_operational(faulty, *f.state, prepared); throw std::runtime_error("expected uncertain"); }
    catch (const OperationalActivationError& e) {
        require(e.failure == OperationalFailure::SelectionUncertain && e.stage == OperationalStage::Identity &&
            !e.identity && bool(e.underlying), "D8 uncertainty retained");
    }
    require(faults == 2, "each call inspects afresh");
}
void decoded_claims_are_not_authority() {
    Fixture f; auto p = f.open();
    auto initial = prepare_operational_request(p, *f.state, input(f));
    auto selected = activate_prepared_operational(p, *f.state, initial);
    // Plausible source identity/inventory and exact persisted document. No
    // physical source, seal, C binding or witness authority is manufactured.
    const auto locator = f.binding.store_authority + "/ghost";
    // Historical provenance deliberately reaches verify_legacy_seal, instead
    // of failing earlier while reading modern installed-package semantics.
    write(f.root / "generations/1/generation.toml",
        "id=1\nentries=[{package='pkg',store='" + locator + "'}]\n");
    auto raw = input(f, 11, 1);
    raw.base = f.base(10, 0, selected.identity);
    raw.base_generation = generation(f, 0);
    raw.target.sources.push_back({LegacyTreeSource{locator, LegacySealDigest(d1_test::digest('a'))}, {"bin/tool"}, {}});
    auto prepared = prepare_operational_request(p, *f.state, raw);
    auto decoded = parse_operational_request(serialize_operational_request(prepared));
    require(revalidate_operational_request(p, *f.state, decoded) == OperationalRequestState::BaseCurrent,
        "identity observation is explicitly not source authorization");
    unsigned source_observations = 0;
    ProjectionIo io; const auto stat = io.stat;
    io.stat = [&](int fd, const char* name, int flags, unsigned mask, struct statx* out) {
        if (std::string_view(name) == "ghost") ++source_observations;
        return stat(fd, name, flags, mask, out);
    };
    auto observed = f.open(io);
    try { activate_prepared_operational(observed, *f.state, decoded); throw std::runtime_error("expected source rejection"); }
    catch (const OperationalActivationError& e) {
        require(e.stage == OperationalStage::Selection && e.failure == OperationalFailure::SelectionUncertain,
            "fresh D6 rejects unauthenticated inventory");
        require(bool(e.underlying), "source rejection retains underlying error");
        try { std::rethrow_exception(e.underlying); }
        catch (const LegacyError& original) {
            require(original.cause == LegacyCause::UnsafeSource && original.error == ENOENT &&
                original.outcome == ProjectionOutcome::NotMutated && original.underlying,
                "fresh legacy source inspection rejects missing inode");
            try { std::rethrow_exception(original.underlying); }
            catch (const ProjectionError& primitive) {
                require(primitive.cause == ProjectionCause::IoFailure && primitive.error == ENOENT,
                    "source rejection preserves physical observation failure");
            }
        }
    }
    require(source_observations != 0, "D6 actually observed the claimed source inode");
    require(inspect_selection(p, *f.state) == std::optional<CurrentSelection>(
        CurrentV1{0, id(10), selected.identity.digest}) && !fs::exists(f.root / "projection/bin/tool"),
        "no ownership from claims");
    require(fs::exists(f.root / "projection/.athena-activation/plans" / id(11).value()), "D4 residue retained");
}
void read_only_and_changing_current() {
    Fixture f; unsigned mutations = 0;
    ProjectionIo io;
    const auto mkdir = io.mkdir; const auto rename = io.rename; const auto unlink = io.unlink;
    const auto symlink = io.symlink; const auto link = io.link; const auto sync = io.fsync;
    io.mkdir = [&](int a, const char* b, mode_t c) { ++mutations; return mkdir(a, b, c); };
    io.rename = [&](int a, const char* b, int c, const char* d, unsigned e) { ++mutations; return rename(a,b,c,d,e); };
    io.unlink = [&](int a, const char* b, int c) { ++mutations; return unlink(a,b,c); };
    io.symlink = [&](const char* a, int b, const char* c) { ++mutations; return symlink(a,b,c); };
    io.link = [&](int a, const char* b, int c, const char* d, int e) { ++mutations; return link(a,b,c,d,e); };
    io.fsync = [&](int a) { ++mutations; return sync(a); };
    auto p = f.open(io);
    auto prepared = prepare_operational_request(p, *f.state, input(f));
    (void)revalidate_operational_request(p, *f.state, prepared);
    require(mutations == 0, "preparation/revalidation never publish, synchronize or recover");
    unchanged(f, p);
    ProjectionIo changing; const auto stat = changing.stat; unsigned observations = 0;
    changing.stat = [&](int fd, const char* name, int flags, unsigned mask, struct statx* out) {
        if (std::string_view(name) == "current" && ++observations == 2) {
            // Deliberate violation of external-writer exclusion, solely to
            // demonstrate sensitivity of the final reobservation.
            const auto file = f.root / "generations/current";
            write(file, serialize(CurrentSelection(CurrentV1{0, id(10), prepared.identity().digest})));
            fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write);
        }
        return stat(fd, name, flags, mask, out);
    };
    auto raced = f.open(changing);
    execution_failure([&] { revalidate_operational_request(raced, *f.state, prepared); },
        ExecutionCause::SelectionConflict);
    require(!fs::exists(f.root / "projection/.athena-activation/plans"), "observation race never publishes");
}
void uncertain_retry() {
    for (unsigned mode = 0; mode != 3; ++mode) {
        Fixture f; auto ordinary = f.open();
        auto prepared = prepare_operational_request(ordinary, *f.state, input(f));
        const auto bytes = serialize_operational_request(prepared);
        ProjectionIo io;
        const auto rename = io.rename; const auto sync = io.fsync;
        io.rename = [=](int a, const char* b, int c, const char* d, unsigned flags) {
            const bool current = std::string_view(d) == "current";
            if (mode == 0 && current) { errno = EIO; return -1; }
            const auto result = rename(a,b,c,d,flags);
            if (mode == 1 && current && result == 0) { errno = EIO; return -1; }
            return result;
        };
        io.fsync = [&](int fd) {
            if (mode == 2 && fs::read_symlink("/proc/self/fd/" + std::to_string(fd)) == f.root / "generations/current") {
                errno = EIO; return -1;
            }
            return sync(fd);
        };
        auto faulty = f.open(io);
        try { activate_prepared_operational(faulty, *f.state, prepared); throw std::runtime_error("expected fault"); }
        catch (const OperationalActivationError& e) {
            require(e.stage == (mode == 2 ? OperationalStage::Reconciliation : OperationalStage::Selection) &&
                e.failure == (mode == 2 ? OperationalFailure::AfterSelection : OperationalFailure::SelectionUncertain),
                "prepared path preserves D8 conservative failure");
            require(e.identity && e.identity->activation_id == prepared.identity().activation_id &&
                e.identity->digest == prepared.identity().digest && bool(e.underlying), "failure retains pinned identity");
        }
        require(fs::exists(f.root / "projection/.athena-activation/plans" / id(10).value()), "uncertain plan retained");
        auto fresh = f.open();
        require(revalidate_operational_request(fresh, *f.state, prepared) ==
            (mode == 0 ? OperationalRequestState::BaseCurrent : OperationalRequestState::TargetSelected),
            "reobservation does not infer barriers");
        auto retry = activate_prepared_operational(fresh, *f.state, parse_operational_request(bytes));
        require(retry.reconciliation.state == ReconciliationState::Complete &&
            retry.identity.digest == prepared.identity().digest && serialize_operational_request(prepared) == bytes,
            "explicit same-identity retry repairs through D8");
    }
}
void physical_witness_is_not_observation_authority() {
    Fixture f; auto p = f.open();
    const auto prepared = prepare_operational_request(p, *f.state, input(f));
    const auto frozen = serialize_operational_request(prepared);
    const WitnessReference owner{id(40), 0};
    const auto destination = f.root / "projection/external-link";
    const auto witness = f.root / "projection/.athena-activation/witnesses" / id(40).value() / "0";
    const auto saved = f.root / "original-witness";
    // Real D2 operation, no fabricated stat metadata: witness and destination
    // are hard links to the same symlink inode on the local filesystem.
    p.create_symlink(*f.state, "external-link", "/unavailable/payload", owner);
    struct stat link_stat{}, witness_stat{};
    require(::lstat(destination.c_str(), &link_stat) == 0 && ::lstat(witness.c_str(), &witness_stat) == 0 &&
        S_ISLNK(link_stat.st_mode) && S_ISLNK(witness_stat.st_mode) && link_stat.st_ino == witness_stat.st_ino &&
        link_stat.st_dev == witness_stat.st_dev && link_stat.st_nlink >= 2, "physical hard-linked witness");
    p.validate_owned_symlink("external-link", "/unavailable/payload", owner);
    require(revalidate_operational_request(p, *f.state, prepared) == OperationalRequestState::BaseCurrent,
        "identity revalidation does not certify payload even with a real witness");
    // Keep the old inode alive, so replacement cannot be confused with inode
    // number reuse. Equal target text never proves witness identity.
    fs::rename(witness, saved); fs::create_symlink("/unavailable/payload", witness);
    struct stat replacement{};
    require(::lstat(witness.c_str(), &replacement) == 0 && replacement.st_ino != link_stat.st_ino,
        "same-target replacement has a different physical inode");
    try { p.validate_owned_symlink("external-link", "/unavailable/payload", owner);
        throw std::runtime_error("expected stale witness rejection"); }
    catch (const ProjectionError& e) { require(e.cause == ProjectionCause::OwnershipMismatch,
        "D2 rejects stale physical witness"); }
    require(revalidate_operational_request(p, *f.state, prepared) == OperationalRequestState::BaseCurrent,
        "identity-only observation does not hide its scope after witness substitution");
    try { activate_prepared_operational(p, *f.state, prepared); throw std::runtime_error("expected nonempty base rejection"); }
    catch (const OperationalActivationError& e) {
        require(e.stage == OperationalStage::Selection && e.failure == OperationalFailure::SelectionUncertain && e.underlying,
            "D6 reauthorizes rather than trusting the observation");
        try { std::rethrow_exception(e.underlying); }
        catch (const ExecutionError& original) { require(original.cause == ExecutionCause::IncompleteBase,
            "empty-base request cannot take over a nonempty projection"); }
    }
    struct stat after{}, after_witness{}, old_witness{};
    require(::lstat(destination.c_str(), &after) == 0 && ::lstat(witness.c_str(), &after_witness) == 0 &&
        ::lstat(saved.c_str(), &old_witness) == 0 && after.st_ino == link_stat.st_ino &&
        old_witness.st_ino == link_stat.st_ino && after_witness.st_ino == replacement.st_ino &&
        !inspect_selection(p, *f.state) && serialize_operational_request(prepared) == frozen,
        "physical destination, stale witness, retained original and request remain unchanged");
    require(fs::exists(f.root / "projection/.athena-activation/plans" / id(10).value()), "D4 residue retained");
}
void run() {
    preparation_and_retry(); invalid_inputs(); changed_documents(); competing_requests();
    retry_digest_and_inspection(); decoded_claims_are_not_authority();
    read_only_and_changing_current(); uncertain_retry();
    physical_witness_is_not_observation_authority();
}
}
int main() { return d1_test::run(run); }
