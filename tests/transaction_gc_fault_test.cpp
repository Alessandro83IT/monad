// SPDX-License-Identifier: GPL-3.0-or-later
#include "object_store_test_support.hpp"
#include "store/transaction_pins.hpp"
#include "store/gc.hpp"
#include "generation/generation.hpp"
#include <linux/magic.h>
#include <sys/vfs.h>
using namespace athena;
namespace d = core::detail;
namespace a = store::administrative;
namespace {
struct Fixture {
    StoreFixture temporary;
    fs::path store = temporary.root / "store", generations = temporary.root / "generations";
    Fixture() {
        fs::create_directory(store);
        generation::GenerationManager(generations,generation::GenerationManager::GenerationDirectoryTag{}).initialize();
        gc().collect();
    }
    generation::GenerationManager manager() const { return {generations,generation::GenerationManager::GenerationDirectoryTag{}}; }
    store::AuthorityContext authority() const { return {store,generations}; }
    store::GarbageCollector gc() const { return store::GarbageCollector(authority()); }
    fs::path object() const { return store::ObjectStore(store).object_path(core::ArtifactId("sha256:"+std::string(64,'a'))); }
    void payload() { fs::create_directories(object()/"nested"); std::ofstream(object()/"nested/data")<<"complete"; }
};
enum class Fault { None, TxMkdir, LeaseWrite, LeaseSync, PinWrite, PinSync, PinRename, PinParent,
    RetireRename, RetireSource, RetireDest, TrashMkdir, TrashLeaseWrite, DetachRename,
    DetachSource, DetachDest, Chmod, Unlink, FinalLease, FinalContainer, FinalBarrier };
Fault fault=Fault::None;
bool retired=false,detached=false,pin_replaced=false,cleaned=false;
fs::path path(int fd) { return fs::read_symlink("/proc/self/fd/"+std::to_string(fd)); }
bool fail(Fault target) { if(fault!=target)return false; errno=EIO; return true; }
int mkdir_op(int parent,const char* name,mode_t mode) {
    const auto location=path(parent).filename();
    if (std::string(name).starts_with(".prepare-") &&
        ((location==".athena-transactions" && fail(Fault::TxMkdir)) || (location==".athena-gc-trash" && fail(Fault::TrashMkdir)))) return -1;
    return ::mkdirat(parent,name,mode);
}
ssize_t write_op(int fd,const void* bytes,size_t count) {
    const auto location=path(fd), parent=location.parent_path();
    if (location.filename()=="lease" && ((parent.parent_path().filename()==".athena-transactions" && fail(Fault::LeaseWrite)) ||
        (parent.parent_path().filename()==".athena-gc-trash" && fail(Fault::TrashLeaseWrite)))) return -1;
    if (parent.parent_path().filename()==".athena-transactions" && location.filename().string().starts_with(".athena-tmp-") && fail(Fault::PinWrite)) return -1;
    return ::write(fd,bytes,count);
}
int rename_op(int parent,const char* name,int dest,const char* target,unsigned flags) {
    const bool retirement=path(dest).filename()==".athena-retired-transactions";
    const bool detach=a::hex_name(name,64);
    if ((std::string(target)=="pin.toml" && fail(Fault::PinRename)) || (retirement && fail(Fault::RetireRename)) || (detach && fail(Fault::DetachRename))) return -1;
    const int result=d::linux_syscalls().rename(parent,name,dest,target,flags);
    if(result==0) { if(retirement)retired=true; if(detach)detached=true; if(std::string(target)=="pin.toml")pin_replaced=true; }
    return result;
}
int sync_op(int fd) {
    const auto location=path(fd), parent=location.parent_path(); const auto name=location.filename().string();
    if ((name=="lease" && parent.parent_path().filename()==".athena-transactions" && fail(Fault::LeaseSync)) ||
        (name.starts_with(".athena-tmp-") && parent.parent_path().filename()==".athena-transactions" && fail(Fault::PinSync)) ||
        (pin_replaced && parent.filename()==".athena-transactions" && fail(Fault::PinParent)) ||
        (retired && name==".athena-transactions" && fail(Fault::RetireSource)) ||
        (retired && name==".athena-retired-transactions" && fail(Fault::RetireDest)) ||
        (detached && name=="sha256" && fail(Fault::DetachSource)) ||
        (detached && parent.filename()==".athena-gc-trash" && fail(Fault::DetachDest)) ||
        (cleaned && name==".athena-gc-trash" && fail(Fault::FinalBarrier))) return -1;
    return ::fsync(fd);
}
int chmod_op(int fd,mode_t mode) { if(fail(Fault::Chmod))return -1; return ::fchmod(fd,mode); }
int unlink_op(int parent,const char* name,int flags) {
    const auto location=path(parent);
    if ((std::string(name)=="data" && fail(Fault::Unlink)) ||
        (std::string(name)=="lease" && location.parent_path().filename()==".athena-gc-trash" && fail(Fault::FinalLease)) ||
        ((flags&AT_REMOVEDIR) && a::hex_name(name,32) && location.filename()==".athena-gc-trash" && fail(Fault::FinalContainer))) return -1;
    const int result=::unlinkat(parent,name,flags);
    if(result==0 && (flags&AT_REMOVEDIR) && a::hex_name(name,32))cleaned=true;
    return result;
}
d::Syscalls ops() { auto result=d::linux_syscalls(); result.mkdir=mkdir_op; result.write=write_op; result.rename=rename_op; result.fsync=sync_op; result.chmod=chmod_op; result.unlink=unlink_op; return result; }
void reset(Fault next=Fault::None) { fault=next; retired=detached=pin_replaced=cleaned=false; }
template<class F> void failure(F f) { bool rejected=false; try{f();}catch(const std::exception&){rejected=true;} require(rejected,"fault not propagated"); }
void registration() {
    for(auto point:{Fault::TxMkdir,Fault::LeaseWrite,Fault::LeaseSync}) {
        Fixture fixture; reset(point); failure([&]{store::TransactionPins owner(fixture.authority(),ops());});
        reset(); fixture.payload(); require(fixture.gc().collect().size()==1,"unregistered preparation became root");
    }
}
void pin_updates() {
    for(auto point:{Fault::PinWrite,Fault::PinSync,Fault::PinRename,Fault::PinParent}) {
        Fixture fixture; std::string tid;
        {
            store::TransactionPins owner(fixture.authority()); owner.add(core::ArtifactId("sha256:"+std::string(64,'a'))); tid=owner.id();
            reset(point); failure([&]{owner.add(core::ArtifactId("sha256:"+std::string(64,'b')),ops());});
            failure([&]{owner.abandon();});
        }
        reset(); auto transaction=d::open_directory(fixture.authority().registry()/tid);
        auto roots=store::parse_pins(a::read_file(transaction.get(),"pin.toml"),tid);
        require(roots.contains("sha256:"+std::string(64,'a')),"pin update lost previous root");
        require(roots.contains("sha256:"+std::string(64,'b'))==(point==Fault::PinParent),"atomic update outcome");
        fixture.payload(); fixture.gc().collect(); require(fs::exists(fixture.object()),"failed update lost pin protection");
    }
}
void retirement() {
    for(auto point:{Fault::RetireRename,Fault::RetireSource,Fault::RetireDest}) {
        Fixture fixture;
        {
            store::TransactionPins owner(fixture.authority()); reset(point);
            try { owner.abandon(ops()); require(false,"retirement failure ignored"); }
            catch(const d::PersistenceError& e) { require(e.outcome_uncertain==(point!=Fault::RetireRename),"retirement classification"); }
        }
        reset(); fixture.gc().collect(); // Explicit abandonment has no continuing recovery use.
    }
}
void collection() {
    for(auto point:{Fault::TrashMkdir,Fault::TrashLeaseWrite,Fault::DetachRename,Fault::DetachSource,Fault::DetachDest,
                    Fault::Chmod,Fault::Unlink,Fault::FinalLease,Fault::FinalContainer,Fault::FinalBarrier}) {
        Fixture fixture; fixture.payload(); reset(point); failure([&]{fixture.gc().collect(ops());});
        const bool before=point==Fault::TrashMkdir || point==Fault::TrashLeaseWrite || point==Fault::DetachRename;
        require(fs::exists(fixture.object())==before,"GC fault authoritative boundary");
        reset(); fixture.gc().reclaim_trash(); fixture.gc().collect();
        require(!fs::exists(fixture.object()),"restart did not collect orphan");
    }
}
void lost_binding() {
    for(int kind=0;kind<3;++kind) {
        Fixture fixture; { store::TransactionPins owner(fixture.authority()); owner.add(core::ArtifactId("sha256:"+std::string(64,'a'))); }
        fixture.payload(); const auto registry=fixture.authority().registry();
        const fs::path source=kind==0?registry:kind==1?registry/"authority.toml":fixture.generations/".athena-store-authority.toml";
        fs::rename(source,fixture.temporary.root/"lost-binding");
        failure([&]{fixture.gc().collect();}); require(fs::exists(fixture.object()/"nested/data"),"lost binding treated as no roots");
    }
    Fixture fixture; const auto second=fixture.temporary.root/"second-store"; fs::create_directory(second);
    failure([&]{store::TransactionPins owner({second,fixture.generations});});
    require(!fs::exists(second/"objects"),"mismatched authority published namespace");
}
void uncertain_generation() {
    Fixture fixture;store::TransactionPins owner(fixture.authority());auto injected=d::linux_syscalls();
    injected.rename=[](int a,const char* x,int b,const char* y,unsigned f)->int {
        const int result=d::linux_syscalls().rename(a,x,b,y,f);
        if(result==0 && std::string(y)=="1")retired=true;
        return result;
    };
    injected.fsync=[](int fd)->int {
        if(retired && path(fd).filename()=="generations") {errno=EIO;return -1;}
        return ::fsync(fd);
    };
    reset();
    try { fixture.manager().create_pinned({}, {}, {},owner,injected);require(false,"uncertain generation ignored"); }
    catch(const d::PersistenceError& error) {require(error.outcome_uncertain,"generation uncertainty missing");}
    failure([&]{owner.abandon();});
    failure([&]{fixture.manager().create_pinned({}, {}, {},owner);});
    require(fs::exists(fixture.authority().registry()/owner.id()),"uncertain generation retired transaction");
}

void multiple_transactions() {
    Fixture fixture;
    { store::TransactionPins first(fixture.authority()); first.add(core::ArtifactId("sha256:"+std::string(64,'a'))); }
    { store::TransactionPins second(fixture.authority()); second.add(core::ArtifactId("sha256:"+std::string(64,'a')));
      second.add(core::ArtifactId("sha256:"+std::string(64,'b'))); }
    fixture.payload();
    const auto other=store::ObjectStore(fixture.store).object_path(core::ArtifactId("sha256:"+std::string(64,'b')));
    fs::create_directory(other);
    const auto unreachable=store::ObjectStore(fixture.store).object_path(core::ArtifactId("sha256:"+std::string(64,'c')));
    fs::create_directory(unreachable);
    require(fixture.gc().collect()==std::vector<fs::path>{unreachable},"multiple transaction pin union");
    require(fs::exists(fixture.object()) && fs::exists(other),"shared transaction root lost");
}

}
int main() {
    try { registration(); pin_updates(); retirement(); collection(); lost_binding(); uncertain_generation(); multiple_transactions(); std::cout<<"M2.5-C syscall/failure matrix passed\n"; return 0; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
