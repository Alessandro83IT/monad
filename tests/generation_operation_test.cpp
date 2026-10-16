// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation/generation_operation.hpp"
#include "activation/operational_request_codec.hpp"
#include "activation/legacy.hpp"
#include "activation_execution_support.hpp"
#include <sys/wait.h>
#include <limits>

using namespace athena::activation;
using namespace execution_test;
namespace {
std::string bytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "read test record");
    return {std::istreambuf_iterator<char>(file), {}};
}
struct OperationFixture : Fixture {
    fs::path archive_path = root / "requests";
    explicit OperationFixture(unsigned initial = 0) {
        fs::create_directory(archive_path); fs::permissions(archive_path, fs::perms::owner_all);
        auto dir = athena::core::detail::open_directory(archive_path);
        auto parent = athena::core::detail::open_directory(root);
        athena::core::detail::directory_barrier(dir.get());
        athena::core::detail::directory_barrier(parent.get());
        auto p = open();
        auto doc = athena::generation::read_generation_document(binding.generation_authority, 0);
        auto prepared = prepare_operational_request(p, *state,
            {id(10), binding, AbsentBase{}, {}, {0, doc.digest, doc.bytes, {}}, {}});
        activate_prepared_operational(p, *state, prepared);
        if (initial != 0) {
            auto target = athena::generation::read_generation_document(binding.generation_authority, initial);
            auto next = prepare_operational_request(p, *state,
                {id(9), binding, base(10, 0, prepared.identity()), prepared.input().target,
                 {initial, target.digest, target.bytes, {}}, {}});
            activate_prepared_operational(p, *state, next);
        }
    }
    auto archive(RequestArchiveIo io = {}) {
        return OperationalRequestArchive::open(archive_path, binding, std::move(io));
    }
    CurrentV1 current() {
        auto p = open(); auto actual = inspect_selection(p, *state);
        require(actual && std::holds_alternative<CurrentV1>(*actual), "test operational current");
        return std::get<CurrentV1>(*actual);
    }
    GenerationOperationInput input(unsigned target = 2, unsigned activation = 11) {
        // Test caller deliberately supplies explicit independently observed intent.
        const auto c = current();
        return {id(activation), binding, {c, digest(c.generation), ActivationOwnership{c.activation_id}}, target, {}, {}, {}};
    }
    auto prepare(ProjectionContext& p, GenerationOperationKind kind = GenerationOperationKind::Switch,
        unsigned target = 2, unsigned activation = 11) {
        return prepare_generation_operation(p, *state, kind, input(target, activation));
    }
    fs::path record(unsigned activation = 11) const { return archive_path / (id(activation).value() + ".toml"); }
    bool plan(unsigned activation = 11) const { return fs::exists(root / "projection/.athena-activation/plans" / id(activation).value()); }
};
template<class F> RequestArchiveError archive_failure(F action) {
    try { action(); }
    catch (const RequestArchiveError& error) { require(bool(error.underlying), "archive cause preserved"); return error; }
    throw std::runtime_error("Expected archive error");
}
void same_identity(const PreparedGenerationOperation& a, const PreparedGenerationOperation& b) {
    require(a.kind() == b.kind() && a.reference().request.plan.activation_id == b.reference().request.plan.activation_id &&
        a.reference().request.plan.digest == b.reference().request.plan.digest &&
        a.reference().request.envelope == b.reference().request.envelope &&
        serialize_operational_request(a.request()) == serialize_operational_request(b.request()), "frozen operation identity and bytes");
}
void roundtrip() {
    OperationFixture f; auto p = f.open(); auto archive = f.archive();
    const std::string historical = "# historical target\r\nid=2\r\nentries=[]\r\n";
    write(f.root / "generations/2/generation.toml", historical);
    auto raw = f.input();
    auto operation = prepare_generation_operation(p, *f.state, GenerationOperationKind::Switch, raw);
    const auto original = serialize_operational_request(operation.request());
    require(operation.request().input().target.document == historical, "retain exact original generation bytes");
    require(fs::is_empty(f.archive_path) && !f.plan(), "preparation does not persist or publish");
    raw.binding.projection_root = "/mutated"; raw.base.generation_digest = athena::generation::GenerationDocumentDigest(d1_test::digest('f'));
    require(serialize_operational_request(operation.request()) == original, "detached caller fields");
    require(persist_generation_operation(p, *f.state, archive, operation) == RequestPublication::Published, "canonical durable switch");
    require(bytes(f.record()) == original && f.current().generation == 0 && !f.plan(), "archiving does not select or publish D4");
    auto reopened = f.archive();
    auto recovered = recover_generation_operation(p, *f.state, reopened, operation.reference());
    same_identity(operation, recovered);
    auto result = activate_generation_operation(p, *f.state, reopened, operation.reference());
    require(result.identity.digest == operation.request().identity().digest &&
        result.reconciliation.state == ReconciliationState::Complete && f.current().generation == 2, "explicit D8 switch");
    require(activate_generation_operation(p, *f.state, reopened, operation.reference()).publication == PlanPublication::AlreadyPublished,
        "same original switch retry");
    auto rollback = f.prepare(p, GenerationOperationKind::Rollback, 0, 12);
    persist_generation_operation(p, *f.state, archive, rollback);
    activate_generation_operation(p, *f.state, archive, rollback.reference());
    require(f.current().generation == 0 && f.current().activation_id == id(12), "explicit rollback through D8");
    // Compare against ORIGINAL base 2, not the now selected rollback target 0.
    auto retry = recover_generation_operation(p, *f.state, archive, rollback.reference());
    same_identity(rollback, retry);
    require(activate_generation_operation(p, *f.state, archive, rollback.reference()).publication == PlanPublication::AlreadyPublished,
        "rollback selected-target retry does not rebase numeric direction");
    archive_failure([&] { activate_generation_operation(p, *f.state, archive, operation.reference()); });
    require(f.current().activation_id == id(12), "same-generation different-ID base replay rejected");
}
void invalid_inputs() {
    OperationFixture f; auto p = f.open(); auto raw = f.input();
    for (auto kind : {GenerationOperationKind::Switch, GenerationOperationKind::Rollback}) {
        auto bad = raw; bad.binding.generation_authority = "/unrelated/missing";
        rejected([&] { prepare_generation_operation(p, *f.state, kind, bad); });
    }
    rejected([&] { prepare_generation_operation(p, *f.state, static_cast<GenerationOperationKind>(99), raw); });
    rejected([&] { f.prepare(p, GenerationOperationKind::Rollback, 0); });
    rejected([&] { prepare_generation_operation(p, *f.state, GenerationOperationKind::Rollback, raw); });
    auto bad = raw; bad.target_generation = std::numeric_limits<std::uint64_t>::max();
    d1_test::rejects([&] { prepare_generation_operation(p, *f.state, GenerationOperationKind::Switch, bad); }, SchemaErrorCode::Bounds);
    bad = raw; bad.base.current = HistoricalCurrent{0, "0\n"}; bad.base.ownership = EmptyOwnership{};
    d1_test::rejects([&] { prepare_generation_operation(p, *f.state, GenerationOperationKind::Switch, bad); }, SchemaErrorCode::Reference);
    bad = raw; bad.base.ownership = ActivationOwnership{id(99)};
    rejected([&] { prepare_generation_operation(p, *f.state, GenerationOperationKind::Switch, bad); });
    bad = raw; bad.old_witnesses = {{"bin/tool", {id(10), 0}}};
    rejected([&] { prepare_generation_operation(p, *f.state, GenerationOperationKind::Switch, bad); });
    bad = raw; bad.base.generation_digest = athena::generation::GenerationDocumentDigest(d1_test::digest('f'));
    rejected([&] { prepare_generation_operation(p, *f.state, GenerationOperationKind::Switch, bad); });
    rejected([&] { prepare_generation_operation(p, *f.state, GenerationOperationKind::Switch, raw, PlanDigest(d1_test::digest('f'))); });
    require(fs::is_empty(f.archive_path) && !f.plan() && f.current().generation == 0, "bad preparation has no publication");
    f.state.reset();
    {
        fs::create_directory(f.root / "other-generations"); fs::permissions(f.root / "other-generations", fs::perms::owner_all);
        ProjectionState other((f.root / "other-generations").native());
        rejected([&] { prepare_generation_operation(p, other, GenerationOperationKind::Switch, raw); });
    }
    f.state = std::make_unique<ProjectionState>(f.binding.generation_authority);
}
void documents() {
    for (unsigned mode = 0; mode != 5; ++mode) {
        OperationFixture f; auto p = f.open(); auto archive = f.archive(); auto operation = f.prepare(p);
        persist_generation_operation(p, *f.state, archive, operation);
        const auto record = bytes(f.record()); const auto file = f.root / "generations/2/generation.toml";
        if (mode == 0) fs::remove(file);
        if (mode == 1) write(file, "id=[\n");
        if (mode == 2) write(file, "# changed valid original bytes\nid=2\nentries=[]\n");
        if (mode == 3) { fs::rename(file, f.root / "saved-generation"); fs::create_symlink(f.root / "saved-generation", file); }
        if (mode == 4) write(f.root / "generations/0/generation.toml", "# changed base\nid=0\nentries=[]\n");
        if (mode == 0 || mode == 1 || mode == 3) rejected([&] { f.prepare(p); });
        if (mode == 2) require(f.prepare(p).request().identity().digest != operation.request().identity().digest,
            "new explicit preparation sees new bytes; existing operation never rebases");
        archive_failure([&] { activate_generation_operation(p, *f.state, archive, operation.reference()); });
        require(!f.plan() && f.current().generation == 0 && bytes(f.record()) == record, "changed/missing document never reconstructed or selected");
    }
}
void conflicts() {
    OperationFixture f; auto p = f.open(); auto archive = f.archive();
    auto first = f.prepare(p), second = f.prepare(p, GenerationOperationKind::Switch, 1, 12);
    persist_generation_operation(p, *f.state, archive, first);
    auto wrong_binding = f.binding; wrong_binding.projection_id = ProjectionId(std::string(32, 'a'));
    auto foreign_archive = OperationalRequestArchive::open(f.archive_path, wrong_binding);
    archive_failure([&] { activate_generation_operation(p, *f.state, foreign_archive, first.reference()); });
    auto collision = f.prepare(p, GenerationOperationKind::Switch, 1, 11);
    archive_failure([&] { persist_generation_operation(p, *f.state, archive, collision); });
    auto wrong = first.reference(); wrong.request.envelope = RequestEnvelopeDigest(d1_test::digest('f'));
    archive_failure([&] { activate_generation_operation(p, *f.state, archive, wrong); });
    wrong = first.reference(); wrong.request.plan.digest = PlanDigest(d1_test::digest('f'));
    archive_failure([&] { recover_generation_operation(p, *f.state, archive, wrong); });
    wrong = first.reference(); wrong.kind = GenerationOperationKind::Rollback;
    rejected([&] { activate_generation_operation(p, *f.state, archive, wrong); });
    wrong.kind = static_cast<GenerationOperationKind>(99);
    rejected([&] { recover_generation_operation(p, *f.state, archive, wrong); });
    require(!f.plan() && f.current().generation == 0, "invalid intent/ref never reaches D4");
    persist_generation_operation(p, *f.state, archive, second);
    activate_generation_operation(p, *f.state, archive, second.reference());
    archive_failure([&] { persist_generation_operation(p, *f.state, archive, first); });
    archive_failure([&] { activate_generation_operation(p, *f.state, archive, first.reference()); });
    require(f.current().activation_id == id(12) && !f.plan(11), "competing operation never implicitly rebases");
}
void corrupt_records_and_unpublished() {
    for (unsigned mode = 0; mode != 4; ++mode) {
        OperationFixture f; auto p = f.open(); auto archive = f.archive(); auto operation = f.prepare(p);
        if (mode != 0) {
            persist_generation_operation(p, *f.state, archive, operation);
            auto observed = recover_generation_operation(p, *f.state, archive, operation.reference());
            same_identity(operation, observed); // A later activate must recover again.
        }
        if (mode == 1) write(f.record(), "corrupt");
        if (mode == 2) { fs::rename(f.record(), f.root / "saved-request"); fs::create_symlink(f.root / "saved-request", f.record()); }
        if (mode == 3) fs::create_hard_link(f.record(), f.root / "aliased-request");
        archive_failure([&] { activate_generation_operation(p, *f.state, archive, operation.reference()); });
        require(f.current().generation == 0 && !f.plan(), "no absent/corrupt archive fallback to in-memory or legacy request");
    }
}
void archive_faults() {
    for (auto kind : {GenerationOperationKind::Switch, GenerationOperationKind::Rollback})
    for (bool after : {false, true}) for (unsigned boundary = 1; boundary <= 7; ++boundary) {
        OperationFixture f(kind == GenerationOperationKind::Rollback ? 2 : 0);
        auto p = f.open(); auto operation = f.prepare(p, kind, kind == GenerationOperationKind::Rollback ? 0 : 2);
        const auto original = f.current(); RequestArchiveIo io; unsigned event = 0;
        auto hit = [&] { return ++event == boundary; };
        const auto w = io.write; const auto sync = io.files.fsync; const auto rename = io.files.rename;
        io.write = [&](int fd, const void* b, size_t n)->ssize_t { bool fail = hit(); if (fail && !after) { errno=EIO; return -1; }
            auto rc=w(fd,b,n); if (fail) { errno=EIO; return -1; } return rc; };
        io.files.fsync = [&](int fd) { bool fail=hit(); if (fail && !after) { errno=EIO; return -1; }
            auto rc=sync(fd); if (fail) { errno=EIO; return -1; } return rc; };
        io.files.rename = [&](int a,const char* b,int c,const char* d,unsigned flags) { bool fail=hit(); if (fail && !after) { errno=EIO; return -1; }
            auto rc=rename(a,b,c,d,flags); if (fail) { errno=EIO; return -1; } return rc; };
        auto faulty = f.archive(io);
        auto error = archive_failure([&] { persist_generation_operation(p, *f.state, faulty, operation); });
        require(error.outcome == RequestArchiveOutcome::Uncertain && f.current() == original && !f.plan(), "persist faults cannot select");
        auto fresh = f.archive(); persist_generation_operation(p, *f.state, fresh, operation);
        auto recovered = recover_generation_operation(p, *f.state, fresh, operation.reference()); same_identity(operation, recovered);
        activate_generation_operation(p, *f.state, fresh, operation.reference());
        require(f.current().activation_id == operation.request().identity().activation_id, "explicit original retry after persist fault");
    }
    for (auto kind : {GenerationOperationKind::Switch, GenerationOperationKind::Rollback})
    for (bool after : {false, true}) for (unsigned boundary = 1; boundary <= 3; ++boundary) {
        OperationFixture f(kind == GenerationOperationKind::Rollback ? 2 : 0); auto p=f.open(); auto archive=f.archive();
        auto operation=f.prepare(p,kind,kind == GenerationOperationKind::Rollback ? 0 : 2);
        persist_generation_operation(p,*f.state,archive,operation); const auto original=f.current();
        RequestArchiveIo io; const auto sync=io.files.fsync; unsigned event=0;
        io.files.fsync=[&](int fd) { bool fail=++event==boundary; if(fail&&!after){errno=EIO;return -1;}
            auto rc=sync(fd); if(fail){errno=EIO;return -1;}return rc; };
        auto faulty=f.archive(io);
        archive_failure([&]{activate_generation_operation(p,*f.state,faulty,operation.reference());});
        require(f.current()==original&&!f.plan(),"recover barrier failure prevents D8 entry");
        auto fresh=f.archive();activate_generation_operation(p,*f.state,fresh,operation.reference());
    }
}
void activation_faults() {
    for(auto kind:{GenerationOperationKind::Switch,GenerationOperationKind::Rollback}) for(unsigned mode=0;mode!=5;++mode) {
        OperationFixture f(kind==GenerationOperationKind::Rollback?2:0);auto p=f.open();auto archive=f.archive();
        auto operation=f.prepare(p,kind,kind==GenerationOperationKind::Rollback?0:2);
        persist_generation_operation(p,*f.state,archive,operation);const auto frozen=bytes(f.record());
        ProjectionIo io;const auto rename=io.rename;const auto sync=io.fsync;
        io.rename=[=](int a,const char* b,int c,const char* d,unsigned flags) {
            const bool publication=std::string_view(d)==id(11).value();
            if(mode==3&&publication){errno=EIO;return -1;}
            bool current=std::string_view(d)=="current";if(mode==0&&current){errno=EIO;return -1;}
            auto rc=rename(a,b,c,d,flags);if(((mode==1&&current)||(mode==4&&publication))&&rc==0){errno=EIO;return -1;}return rc;};
        io.fsync=[&](int fd) {if(mode==2&&fs::read_symlink("/proc/self/fd/"+std::to_string(fd))==f.root/"generations/current"){
                errno=EIO;return -1;}return sync(fd);};
        auto faulty=f.open(io);
        try {activate_generation_operation(faulty,*f.state,archive,operation.reference());throw std::runtime_error("Expected D8 fault");}
        catch(const OperationalActivationError& error) {
            require(error.stage==(mode>=3?OperationalStage::Publication:mode==2?OperationalStage::Reconciliation:OperationalStage::Selection)&&
                error.failure==(mode>=3?OperationalFailure::BeforeSelection:mode==2?OperationalFailure::AfterSelection:OperationalFailure::SelectionUncertain)&&
                error.identity&&error.identity->digest==operation.request().identity().digest&&error.underlying,"D8 error contract unchanged");
        }
        require((f.plan()||fs::exists(f.root/"projection/.athena-activation/plans/.preparations"/id(11).value()))&&
            bytes(f.record())==frozen,"D4/B2 residue retained on activation uncertainty");
        auto fresh=f.open();auto result=activate_generation_operation(fresh,*f.state,archive,operation.reference());
        require(result.reconciliation.state==ReconciliationState::Complete&&result.identity.digest==operation.request().identity().digest,
            "same original switch/rollback repairs through D8");
    }
}
void physical_witness_boundary() {
    OperationFixture f;auto p=f.open();auto archive=f.archive();auto operation=f.prepare(p);
    const WitnessReference owner{id(40),0};
    p.create_symlink(*f.state,"unclaimed-link","/unavailable/payload",owner);
    const auto witness=f.root/"projection/.athena-activation/witnesses"/id(40).value()/"0";
    const auto link=f.root/"projection/unclaimed-link";
    struct stat a{},b{};
    require(::lstat(link.c_str(),&a)==0&&::lstat(witness.c_str(),&b)==0&&S_ISLNK(a.st_mode)&&
        a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_nlink>=2,"actual hard-linked physical witness");
    persist_generation_operation(p,*f.state,archive,operation);
    auto recovered=recover_generation_operation(p,*f.state,archive,operation.reference());same_identity(operation,recovered);
    fs::rename(witness,f.root/"saved-witness");fs::create_symlink("/unavailable/payload",witness);
    rejected([&]{p.validate_owned_symlink("unclaimed-link","/unavailable/payload",owner);});
    // This link is unrelated to the selected base and the requested operation.
    // Existing D6/D7 preserves unrelated entries; B3 must not invent a stricter
    // namespace-wide policy or infer ownership of this inode from its archive.
    auto result=activate_generation_operation(p,*f.state,archive,operation.reference());
    struct stat preserved{};
    require(result.reconciliation.state==ReconciliationState::Complete&&f.current().generation==2&&
        ::lstat(link.c_str(),&preserved)==0&&preserved.st_dev==a.st_dev&&preserved.st_ino==a.st_ino,
        "switch preserves the unrelated physical inode without claiming it");
    rejected([&]{p.validate_owned_symlink("unclaimed-link","/unavailable/payload",owner);});
}
void source_claims() {
    for(auto kind:{GenerationOperationKind::Switch,GenerationOperationKind::Rollback}) {
        OperationFixture f(kind==GenerationOperationKind::Rollback?2:0);auto p=f.open();auto archive=f.archive();
        const auto locator=f.binding.store_authority+"/ghost";
        write(f.root/"generations/1/generation.toml","id=1\nentries=[{package='pkg',store='"+locator+"'}]\n");
        auto raw=f.input(1);raw.target_sources={{LegacyTreeSource{locator,LegacySealDigest(d1_test::digest('a'))},{"bin/tool"},{}}};
        auto operation=prepare_generation_operation(p,*f.state,kind,raw);const auto frozen=serialize_operational_request(operation.request());
        raw.target_sources[0].links[0][0]='X';raw.target_sources.clear();
        require(serialize_operational_request(operation.request())==frozen,"nested inventory aliases detached");
        persist_generation_operation(p,*f.state,archive,operation);const auto before=f.current();
        unsigned observations=0;ProjectionIo io;const auto stat=io.stat;
        io.stat=[&](int fd,const char* name,int flags,unsigned mask,struct statx* out){if(std::string_view(name)=="ghost")++observations;return stat(fd,name,flags,mask,out);};
        auto observed=f.open(io);
        try {activate_generation_operation(observed,*f.state,archive,operation.reference());throw std::runtime_error("Expected source rejection");}
        catch(const OperationalActivationError& error) {
            require(error.stage==OperationalStage::Selection&&error.failure==OperationalFailure::SelectionUncertain,"fresh D6 source authorization");
            try {std::rethrow_exception(error.underlying);}
            catch(const LegacyError& original) {require(original.cause==LegacyCause::UnsafeSource&&original.error==ENOENT,"actual missing source rejection");}
        }
        require(observations>0&&f.current()==before&&!fs::exists(f.root/"projection/bin/tool")&&bytes(f.record())==frozen,
            "neither rollback nor archive confers source ownership");
    }
}
void gc_lifetime() {
    {
        OperationFixture f;auto p=f.open();auto archive=f.archive();auto operation=f.prepare(p,GenerationOperationKind::Switch,1);
        persist_generation_operation(p,*f.state,archive,operation);
        athena::generation::GenerationManager manager(f.root/"generations",athena::generation::GenerationManager::GenerationDirectoryTag{});
        rejected([&]{manager.discard(1);}); // Process-wide hierarchy rejects State reacquisition.
        require(fs::exists(f.root/"generations/1/generation.toml"),"manager never called within adapter State");
        f.state.reset();manager.discard(1);
        f.state=std::make_unique<ProjectionState>(f.binding.generation_authority);
        archive_failure([&]{activate_generation_operation(p,*f.state,archive,operation.reference());});
        require(fs::exists(f.record())&&!f.plan()&&f.current().generation==0,"B2 is not a GC/discard root; no recreation after State gap");
    }
    {
        OperationFixture f;auto p=f.open();auto archive=f.archive();auto operation=f.prepare(p,GenerationOperationKind::Switch,1);
        persist_generation_operation(p,*f.state,archive,operation);activate_generation_operation(p,*f.state,archive,operation.reference());
        auto next=f.prepare(p,GenerationOperationKind::Switch,2,12);
        persist_generation_operation(p,*f.state,archive,next);activate_generation_operation(p,*f.state,archive,next.reference());
        f.state.reset();athena::generation::GenerationManager manager(f.root/"generations",athena::generation::GenerationManager::GenerationDirectoryTag{});
        try {manager.discard(1);throw std::runtime_error("Expected D4 retention");}
        catch(const std::runtime_error& error){require(std::string_view(error.what()).find("retained by an activation plan")!=std::string_view::npos,"existing D4/D5 protects inactive generation");}
        f.state=std::make_unique<ProjectionState>(f.binding.generation_authority);
    }
}
// B4 gate: a live transaction lease is NOT a generation metadata pin.
// Physical metadata witness, no fabricated payload or source authorization.
void transaction_generation_lifetime_gate() {
    OperationFixture f;
    const auto before = f.current();
    f.state.reset();
    athena::store::TransactionPins pins({f.binding.store_authority, f.binding.generation_authority});
    const auto registry = pins.authority().registry() / pins.id();
    require(fs::exists(registry / "pin.toml"), "durable transaction exists before State");
    f.state = std::make_unique<ProjectionState>(f.binding.generation_authority);
    auto p = f.open(); auto archive = f.archive();
    auto operation = f.prepare(p, GenerationOperationKind::Switch, 1);
    const auto reference = operation.reference();
    const auto frozen = serialize_operational_request(operation.request());
    persist_generation_operation(p, *f.state, archive, operation);
    require(!f.plan() && f.current() == before, "B2 persist has no D4 or premature selection");
    f.state.reset();
    // Deterministic cooperating competitor at the inter-operation State gap.
    // It obeys the same lease -> State -> StoreGc hierarchy; discard does not
    // consult the transaction registry because pins identify artifacts only.
    athena::generation::GenerationManager manager(f.root / "generations",
        athena::generation::GenerationManager::GenerationDirectoryTag{});
    manager.discard(1);
    require(!fs::exists(f.root / "generations/1") && fs::exists(registry / "pin.toml"),
        "live transaction cannot retain archived target generation");
    f.state = std::make_unique<ProjectionState>(f.binding.generation_authority);
    archive_failure([&] { recover_generation_operation(p, *f.state, archive, reference); });
    archive_failure([&] { activate_generation_operation(p, *f.state, archive, reference); });
    require(f.current() == before && !f.plan() && bytes(f.record()) == frozen,
        "lost lifetime fails closed without rebase, recreation, execution or archive damage");
    f.state.reset();
    pins.abandon(); // Explicit test cleanup only, never a recovery policy.
}
void concurrent_operations() {
    OperationFixture f;auto p=f.open();auto archive=f.archive();auto first=f.prepare(p,GenerationOperationKind::Switch,1,11);
    auto second=f.prepare(p,GenerationOperationKind::Switch,2,12);
    persist_generation_operation(p,*f.state,archive,first);persist_generation_operation(p,*f.state,archive,second);
    f.state.reset();pid_t children[2];unsigned index=0;
    for(const auto* operation:{&first,&second}) {
        auto& child=children[index++];child=::fork();require(child>=0,"fork independent operations");
        if(!child){const auto& r=operation->reference().request;
            ::execl("/proc/self/exe","generation_operation_test","activate",f.root.c_str(),r.plan.activation_id.value().c_str(),
                r.plan.digest.value().c_str(),r.envelope.value().c_str(),nullptr);::_exit(99);}
    }
    unsigned success=0,conflict=0;
    for(auto child:children){int status;require(::waitpid(child,&status,0)==child&&WIFEXITED(status),"wait independent operations");
        success+=WEXITSTATUS(status)==0;conflict+=WEXITSTATUS(status)==77;}
    require(success==1&&conflict==1,"exactly one explicit base wins; other is stale");
    f.state=std::make_unique<ProjectionState>(f.binding.generation_authority);
    const auto current=f.current();require((current.generation==1&&current.activation_id==id(11))||
        (current.generation==2&&current.activation_id==id(12)),"winner preserves request identity");
    const auto loser=current.activation_id==id(11)?12:11;
    require(!f.plan(loser)&&fs::exists(f.record(11))&&fs::exists(f.record(12)),"loser retained without D4 publication/rebase");
}
void run(){roundtrip();invalid_inputs();documents();conflicts();corrupt_records_and_unpublished();archive_faults();activation_faults();source_claims();physical_witness_boundary();gc_lifetime();transaction_generation_lifetime_gate();concurrent_operations();}
}
int main(int argc,char** argv) {
    if(argc==6&&std::string_view(argv[1])=="activate") {
        try {
            fs::path root(argv[2]);ProjectionBinding binding{d1_test::projection_id,(root/"generations").native(),(root/"store").native(),(root/"projection").native()};
            athena::store::ObjectStore objects(root/"store");ProjectionState state(binding.generation_authority);
            auto context=ProjectionContext::open(binding,objects);auto archive=OperationalRequestArchive::open(root/"requests",binding);
            activate_generation_operation(context,state,archive,{GenerationOperationKind::Switch,{{ActivationId(argv[3]),PlanDigest(argv[4])},RequestEnvelopeDigest(argv[5])}});
            return 0;
        } catch(const RequestArchiveError& error) {
            try {std::rethrow_exception(error.underlying);}
            catch(const ExecutionError& cause){if(cause.cause==ExecutionCause::SelectionConflict)return 77;}
            catch(...){}return 98;
        } catch(...){return 99;}
    }
    return d1_test::run(run);
}
