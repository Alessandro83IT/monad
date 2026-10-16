// SPDX-License-Identifier: GPL-3.0-or-later
#include "realization_executor_support.hpp"
#include "store/gc.hpp"
#include "store/gc_trash.hpp"
#include "gc_trash_unsafe_test_access.hpp"
#include "generation/generation.hpp"
#include "generation/publication.hpp"
#include <sys/wait.h>
#include <sys/file.h>
#include <signal.h>
#include <cstring>
using namespace execution_test;
namespace d = core::detail;
namespace a = store::administrative;
namespace {
struct Fixture {
    StoreFixture temporary;
    fs::path store = temporary.root / "store", generations = temporary.root / "generations";
    Fixture() { fs::create_directory(store); generation::GenerationManager(generations, generation::GenerationManager::GenerationDirectoryTag{}).initialize(); }
    store::AuthorityContext authority() const { return {store, generations}; }
    fs::path object(char c) const { return store::ObjectStore(store).object_path(core::ArtifactId("sha256:" + std::string(64,c))); }
    void payload(char c) { fs::create_directory(object(c)); std::ofstream(object(c) / "data") << "complete"; }
    generation::GenerationManager manager() const { return {generations, generation::GenerationManager::GenerationDirectoryTag{}}; }
    store::GarbageCollector gc() const { return store::GarbageCollector(authority()); }
};
core::ArtifactId id(char c) { return core::ArtifactId("sha256:" + std::string(64,c)); }
void send(int fd) { require(::write(fd,"x",1)==1,"send"); }
void receive(int fd) { char c; require(::read(fd,&c,1)==1,"receive"); }
struct Pipe { int fd[2]; Pipe() { require(::pipe2(fd,O_CLOEXEC)==0,"pipe"); } ~Pipe() { ::close(fd[0]); ::close(fd[1]); } };
void wait_ok(pid_t pid) { int status; require(::waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0,"child result"); }
template<class F> void rejects(F f) { bool rejected=false; try { f(); } catch (const std::exception&) { rejected=true; } require(rejected,"expected failure"); }
int random_calls=0;
ssize_t random_collision(void* bytes,size_t n,unsigned) { std::memset(bytes, random_calls++ == 0 ? 0 : 1,n); return n; }
ssize_t zero_random(void* bytes,size_t n,unsigned) { std::memset(bytes,0,n); return n; }
int ready=-1,gate=-1;
int pause_kind=0;
bool renamed=false;
int rename_op(int source,const char* name,int dest,const char* target,unsigned flags) {
    const auto parent=fs::read_symlink("/proc/self/fd/"+std::to_string(dest)).filename();
    const bool point=(pause_kind==1 && std::string(target)=="pin.toml") ||
        (pause_kind==2 && a::hex_name(name,32) && a::hex_name(target,32)) ||
        (pause_kind==3 && std::string(target)=="1") ||
        (pause_kind==4 && a::hex_name(name,64));
    if (point) { send(ready); receive(gate); }
    const auto result=d::linux_syscalls().rename(source,name,dest,target,flags);
    if (result==0 && point) renamed=true;
    return result;
}
int sync_failure(int fd) { if (renamed) { errno=EIO; return -1; } return ::fsync(fd); }
void store_conflict(const Fixture& fixture) {
    bool conflict=false;
    try { core::FileLock lock(core::store_gc_lock_path(fixture.store),core::LockKind::StoreGc,core::LockMode::Exclusive,"probe",false); }
    catch (const std::system_error& e) { conflict=e.code().value()==EWOULDBLOCK || e.code().value()==EAGAIN; }
    require(conflict,"StoreGc serialization absent");
}
realization::RealizationRecord membership(const Fixture& fixture) {
    auto selection=plan({recipe(fixture.temporary.root/"source")}).selections()[0].identity();
    return {selection,id('a'),{},core::ArtifactOrigin::AmbientSourceBuild};
}
void generation_directory_link_contract() {
    Fixture fixture;
    core::FileLock state(core::state_lock_path(fixture.generations), core::LockKind::State);
    auto valid = [&] {
        require(generation::publication::existing_ids_for_gc(fixture.generations) == std::vector<std::uint64_t>{0},
            "valid leaf generation is a GC root on the current supported filesystem");
    };
    auto invalid = [&](std::string_view expected) {
        try { generation::publication::existing_ids_for_gc(fixture.generations); }
        catch (const std::runtime_error& error) {
            require(std::string_view(error.what()) == expected, "GC root refused for an unexpected preliminary reason");
            std::cout << "GC NEGATIVE: " << error.what() << '\n'; return;
        }
        throw std::runtime_error("unsafe GC root was accepted");
    };
    valid();
    const auto zero = fixture.generations / "0", metadata = zero / "generation.toml";
    const auto child = zero / "unexpected-child";
    fs::create_directory(child); invalid("unsafe canonical generation directory: 0");
    require(generation::publication::existing_ids(fixture.generations) == std::vector<std::uint64_t>{0},
        "generic historical enumeration keeps its compatibility contract");
    require(fs::remove(child), "remove fixture-only child"); valid();
    fs::permissions(zero, fs::perms::owner_all | fs::perms::group_read);
    invalid("unsafe canonical generation directory: 0"); fs::permissions(zero, fs::perms::owner_all); valid();
    fs::permissions(metadata, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read);
    invalid("unsafe canonical generation metadata: 0"); fs::permissions(metadata, fs::perms::owner_read | fs::perms::owner_write); valid();
    fs::create_hard_link(metadata, fixture.temporary.root / "metadata-alias");
    invalid("unsafe canonical generation metadata: 0"); fs::remove(fixture.temporary.root / "metadata-alias"); valid();
    fs::rename(metadata, fixture.temporary.root / "saved-metadata"); fs::create_symlink(fixture.temporary.root / "saved-metadata", metadata);
    invalid("unsafe canonical generation metadata: 0"); fs::remove(metadata); fs::rename(fixture.temporary.root / "saved-metadata", metadata); valid();
    fs::create_directory_symlink(zero, fixture.generations / "1"); invalid("unsafe canonical generation entry: 1"); fs::remove(fixture.generations / "1"); valid();
    std::ofstream(fixture.generations / "1") << "not a generation directory"; invalid("unsafe canonical generation entry: 1"); fs::remove(fixture.generations / "1"); valid();
    // nlink=2 historically permits unrelated files and non-followed symlinks.
    // Btrfs's nlink=1 branch must preserve that SAME acceptance boundary.
    std::ofstream(zero / "unrelated") << "opaque"; fs::create_directory_symlink(fixture.temporary.root, zero / "unrelated-link"); valid();
    fs::remove(zero / "unrelated"); fs::remove(zero / "unrelated-link"); valid();
    struct statfs filesystem {}; require(::fstatfs(d::open_directory(zero).get(), &filesystem) == 0, "generation filesystem witness");
    const auto status = d::stat_fd(d::open_directory(zero).get());
    std::cout << "GC filesystem=" << filesystem.f_type << " directory_nlink=" << status.st_nlink << " strict shape/metadata matrix PASS\n";
}
void collisions() {
    Fixture fixture; fixture.gc().collect();
    auto first=d::linux_syscalls(); first.random=zero_random;
    { store::TransactionPins transaction(fixture.authority(),first); require(transaction.id()==std::string(32,'0'),"fixed collision fixture"); }
    auto second=d::linux_syscalls(); second.random=random_collision; random_calls=0;
    { store::TransactionPins transaction(fixture.authority(),second); require(transaction.id()!=std::string(32,'0'),"transaction collision not retried"); }
    const auto registry=d::open_directory(fixture.authority().registry());
    require(a::entries(registry.get()).size()==3,"collision damaged existing registry");
}
void update_and_retirement(int kind) {
    Fixture fixture; fixture.gc().collect(); fixture.payload('a'); Pipe signal,release;
    const auto pid=::fork(); require(pid>=0,"fork owner");
    if (!pid) {
        try {
            store::TransactionPins owner(fixture.authority()); owner.add(id('a'));
            pause_kind=kind; ready=signal.fd[1]; gate=release.fd[0];
            auto ops=d::linux_syscalls(); ops.rename=rename_op;
            if (kind==1) owner.add(id('b'),ops);
            else {
                const auto target=fixture.manager().create_unprotected_for_test({}, {membership(fixture)});
                owner.handoff(target.id,ops);
            }
            ::_exit(0);
        } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; ::_exit(1); }
    }
    ::close(signal.fd[1]); signal.fd[1]=-1; receive(signal.fd[0]); store_conflict(fixture);
    if (kind==1) {
        // The pre-replacement record remains complete. GC is excluded while
        // this old snapshot and a private temporary coexist.
        const auto registry=d::open_directory(fixture.authority().registry());
        for (const auto& name:a::entries(registry.get())) if (a::hex_name(name,32)) {
            const auto tx=d::open_at(registry.get(),name,O_RDONLY|O_DIRECTORY);
            require(store::parse_pins(a::read_file(tx.get(),"pin.toml"),name)==std::set<std::string>{id('a').value()},"torn old snapshot");
        }
    }
    send(release.fd[1]); wait_ok(pid);
    if (kind==1) fixture.payload('b');
    fixture.gc().collect(); require(fs::exists(fixture.object('a')),"release/update lost live object");
    if (kind==1) require(fs::exists(fixture.object('b')),"new pin lost");
}
void generation_vs_gc() {
    Fixture fixture; fixture.gc().collect(); Pipe signal,release;
    const auto pid=::fork(); require(pid>=0,"generation publisher fork");
    if (!pid) {
        try {
            store::TransactionPins owner(fixture.authority()); owner.add(id('a')); fixture.payload('a');
            core::FileLock state(core::state_lock_path(fixture.generations),core::LockKind::State);
            store::StoreGcLock lock(fixture.store);
            pause_kind=3; ready=signal.fd[1]; gate=release.fd[0]; auto ops=d::linux_syscalls(); ops.rename=rename_op;
            generation::publication::persist(fixture.generations,1,generation::serialize(generation::make_generation(1,{}, {membership(fixture)})),ops);
            ::_exit(0);
        } catch (...) { ::_exit(1); }
    }
    ::close(signal.fd[1]); signal.fd[1]=-1; receive(signal.fd[0]); store_conflict(fixture);
    bool conflict=false;
    try { core::FileLock state(core::state_lock_path(fixture.generations),core::LockKind::State,core::LockMode::Shared,"probe",false); }
    catch (const std::system_error&) { conflict=true; }
    require(conflict,"State publication serialization absent"); send(release.fd[1]); wait_ok(pid);
    fixture.gc().collect(); require(fs::exists(fixture.object('a')),"generation publication lost object");
}
void inherited_lease() {
    Fixture fixture; Pipe inherited,release;
    const auto owner=::fork(); require(owner>=0,"fork owner");
    if (!owner) {
        try {
            store::TransactionPins pins(fixture.authority()); pins.add(id('a'));
            const auto inheritor=::fork(); if (inheritor<0) ::_exit(1);
            if (!inheritor) { send(inherited.fd[1]); receive(release.fd[0]); ::_exit(0); }
            // Owner closes its references; inheritor retains the shared OFD.
            ::_exit(0);
        } catch (...) { ::_exit(1); }
    }
    receive(inherited.fd[0]); wait_ok(owner);
    auto registry=d::open_directory(fixture.authority().registry()); std::string tid;
    for (const auto& name:a::entries(registry.get())) if (a::hex_name(name,32)) tid=name;
    auto transaction=d::open_at(registry.get(),tid,O_RDONLY|O_DIRECTORY); auto lease=d::open_at(transaction.get(),"lease",O_RDWR);
    require(::flock(lease.get(),LOCK_EX|LOCK_NB)<0 && errno==EWOULDBLOCK,"inherited OFD lost lease");
    fixture.payload('a'); fixture.gc().collect(); require(fs::exists(fixture.object('a')),"inherited lease pin lost");
    send(release.fd[1]); // No PID-based interpretation or expiry.
}
void detach_contract() {
    Fixture fixture; fixture.gc().collect();
    { store::TransactionPins pin(fixture.authority()); pin.add(id('a')); }
    fixture.payload('a');
    require(fixture.gc().collect().empty() && fs::exists(fixture.object('a')),
        "ordinary collector detached an actively pinned object");
    {
        auto trash=store::UnsafeGcTrashTestAccess::create(fixture.authority().objects(),true);
        const auto container=fixture.authority().objects()/".athena-gc-trash"/store::UnsafeGcTrashTestAccess::id(trash);
        fs::create_directory(container/std::string(64,'a'));
        rejects([&] { core::FileLock state(core::state_lock_path(fixture.generations),core::LockKind::State,core::LockMode::Shared);
                      store::StoreGcLock lock(fixture.store); store::UnsafeGcTrashTestAccess::detach(trash,std::string(64,'a')); });
        require(fs::exists(fixture.object('a')/"data"),"detach collision changed source");
        rejects([&] { store::UnsafeGcTrashTestAccess::reclaim(trash,fixture.authority()); });
    }
    {
        auto trash=store::UnsafeGcTrashTestAccess::create(fixture.authority().objects(),true);
        const auto payload=fixture.authority().objects()/".athena-gc-trash"/
            store::UnsafeGcTrashTestAccess::id(trash)/std::string(64,'a');
        fs::create_symlink(fixture.object('a'),payload);
        core::FileLock state(core::state_lock_path(fixture.generations),core::LockKind::State,core::LockMode::Shared);
        store::StoreGcLock lock(fixture.store);
        rejects([&] { store::UnsafeGcTrashTestAccess::confirm(trash); });
        require(fs::exists(fixture.object('a')/"data"),"trash alias validation touched authoritative inode");
    }
    // Explicit EXDEV injection preserves pre-visibility semantics.
    auto cross=d::linux_syscalls(); cross.rename=[](int,const char*,int,const char*,unsigned)->int { errno=EXDEV; return -1; };
    auto source=d::open_directory(fixture.authority().objects()); auto dest=d::open_directory(fixture.store);
    try { a::rename_detach(source.get(),std::string(64,'a'),dest.get(),"destination",cross); require(false,"EXDEV accepted"); }
    catch (const d::PersistenceError& e) { require(!e.outcome_uncertain && e.error==EXDEV,"EXDEV classification"); }
    require(fs::exists(fixture.object('a')/"data"),"EXDEV changed source");
}
void retirement_failure() {
    Fixture fixture;
    {
        store::TransactionPins owner(fixture.authority()); owner.add(id('a'));
        const auto target=fixture.manager().create_unprotected_for_test({}, {membership(fixture)});
        auto ops=d::linux_syscalls(); ops.rename=rename_op; ops.fsync=sync_failure;
        // No blocking here; fail the first barrier after retirement rename.
        pause_kind=0; renamed=false;
        ops.rename=[](int a,const char* x,int b,const char* y,unsigned f)->int {
            const int result=d::linux_syscalls().rename(a,x,b,y,f);
            if(result==0 && store::administrative::hex_name(x,32)) renamed=true;
            return result;
        };
        try { owner.handoff(target.id,ops); require(false,"retirement fault ignored"); }
        catch (const d::PersistenceError& e) { require(e.outcome_uncertain,"retirement uncertainty missing"); }
    }
    fixture.payload('a'); fixture.gc().collect(); require(fs::exists(fixture.object('a')),"uncertain retirement lost generation root");
}
void reclaim_claims() {
    Fixture fixture; fixture.gc().collect(); fixture.payload('a'); std::string container_id;
    {
        auto trash=store::UnsafeGcTrashTestAccess::create(fixture.authority().objects(),true); container_id=store::UnsafeGcTrashTestAccess::id(trash);
        core::FileLock state(core::state_lock_path(fixture.generations),core::LockKind::State,core::LockMode::Shared);
        store::StoreGcLock lock(fixture.store); store::UnsafeGcTrashTestAccess::detach(trash,std::string(64,'a'));
    }
    Pipe signal,release;
    const auto claimant=::fork(); require(claimant>=0,"reclaimer fork");
    if(!claimant) {
        ::fcntl(signal.fd[1],F_SETFD,0); ::fcntl(release.fd[0],F_SETFD,0);
        const auto ready_text=std::to_string(signal.fd[1]),gate_text=std::to_string(release.fd[0]);
        ::execl("/proc/self/exe","transaction_gc_protocol_test","slow-reclaim",fixture.store.c_str(),fixture.generations.c_str(),ready_text.c_str(),gate_text.c_str(),nullptr);
        ::_exit(127);
    }
    ::close(signal.fd[1]);signal.fd[1]=-1;receive(signal.fd[0]);
    const auto competing=::fork();require(competing>=0,"competing reclaimer fork");
    if(!competing) {
        ::execl("/proc/self/exe","transaction_gc_protocol_test","reclaim",fixture.store.c_str(),fixture.generations.c_str(),nullptr);::_exit(127);
    }
    wait_ok(competing);
    const auto data=fixture.authority().objects()/".athena-gc-trash"/container_id/std::string(64,'a')/"data";
    require(fs::exists(data),"competing reclaimer stole claimed payload");
    send(release.fd[1]);wait_ok(claimant);
    require(store::GcTrash::containers(fixture.authority().objects()).empty(),"claimed cleanup residue");
}

}
int main(int argc,char** argv) {
    try {
        if(argc==4 || argc==6) {
            store::GarbageCollector gc(store::AuthorityContext(argv[2],argv[3]));auto ops=d::linux_syscalls();
            if(argc==6) {
                ready=std::stoi(argv[4]);gate=std::stoi(argv[5]);
                ops.unlink=[](int fd,const char* name,int flags)->int {
                    if(std::string(name)=="data") {send(ready);receive(gate);}
                    return ::unlinkat(fd,name,flags);
                };
            }
            gc.reclaim_trash(ops);return 0;
        }
        generation_directory_link_contract(); collisions(); update_and_retirement(1); update_and_retirement(2); generation_vs_gc(); inherited_lease(); detach_contract(); retirement_failure(); reclaim_claims();
          std::cout<<"M2.5-C protocol/contention tests passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
