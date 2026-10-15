// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation/operational_request_archive.hpp"
#include "activation/operational_request_codec.hpp"
#include "activation_execution_support.hpp"
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <signal.h>

using namespace athena::activation;
using namespace execution_test;
namespace {
struct ArchiveFixture : Fixture {
    fs::path directory = root / "requests";
    ArchiveFixture() {
        fs::create_directory(directory); fs::permissions(directory, fs::perms::owner_all);
        auto dir=athena::core::detail::open_directory(directory);auto parent=athena::core::detail::open_directory(root);
        athena::core::detail::directory_barrier(dir.get());athena::core::detail::directory_barrier(parent.get());
    }
    auto archive(RequestArchiveIo io = {}) { return OperationalRequestArchive::open(directory, binding, std::move(io)); }
    PreparedOperationalRequest request() {
        auto p = open(); const auto doc = athena::generation::read_generation_document(binding.generation_authority,0);
        return prepare_operational_request(p,*state,{id(10),binding,AbsentBase{}, {},{0,doc.digest,doc.bytes,{}},{}});
    }
    auto final() const { return directory / (id(10).value()+".toml"); }
    auto pending() const { return directory / ("."+id(10).value()+".pending"); }
};
std::string bytes(const fs::path& path) {
    std::ifstream in(path,std::ios::binary); require(bool(in),"read fixture");
    return {std::istreambuf_iterator<char>(in),{}};
}
void record(const fs::path& path,std::string_view data) {
    write(path,data);fs::permissions(path,fs::perms::owner_read|fs::perms::owner_write);
}
template<class F> RequestArchiveError fails(F action) {
    try { action(); } catch(const RequestArchiveError& e) {require(bool(e.underlying),"archive original exception retained");return e;}
    throw std::runtime_error("Expected archive rejection");
}
void no_activation(ArchiveFixture& f,ProjectionContext& p) {
    require(!inspect_selection(p,*f.state),"archive does not select current");
    require(!fs::exists(f.root/"projection/.athena-activation/plans"),"archive does not publish D4 plan");
}
void roundtrip() {
    ArchiveFixture f;
    write(f.root/"generations/0/generation.toml","# original historical bytes\r\nid = 0\r\nentries = []\r\n");
    auto p=f.open();auto r=f.request();auto a=f.archive();
    const auto canonical=serialize_operational_request(r);
    require(a.persist(p,*f.state,r)==RequestPublication::Published,"new immutable publication");
    require(bytes(f.final())==canonical&&!fs::exists(f.pending()),"exact canonical bytes, rename consumes preparation");
    require(a.persist(p,*f.state,r)==RequestPublication::AlreadyPublished,"idempotent publication");
    auto fresh=f.archive();auto loaded=fresh.recover(p,*f.state,identify_operational_request(r));
    require(loaded.publication==RequestPublication::AlreadyPublished&&serialize_operational_request(loaded.request)==canonical,
        "restart recovers exact detached claims");
    no_activation(f,p);
    auto result=activate_prepared_operational(p,*f.state,loaded.request);
    require(result.identity.digest==r.identity().digest,"execution explicitly goes through B1/D8");
    auto retry=fresh.recover(p,*f.state,identify_operational_request(r));
    require(revalidate_operational_request(p,*f.state,retry.request)==OperationalRequestState::TargetSelected,"exact selected retry");
}
void partial_and_collision() {
    ArchiveFixture f;auto p=f.open();auto r=f.request();auto a=f.archive();const auto canonical=serialize_operational_request(r);
    record(f.pending(),std::string_view(canonical).substr(0,canonical.size()/2));
    auto e=fails([&]{a.recover(p,*f.state,identify_operational_request(r));});
    require(e.stage==RequestArchiveStage::Read&&e.outcome==RequestArchiveOutcome::NotMutated,"partial recovery cannot invent suffix");
    require(a.persist(p,*f.state,r)==RequestPublication::Published,"explicit complete request resumes exact prefix");
    auto other=r.input();other.target.document="# different bytes\nid=0\nentries=[]\n";
    other.target.digest=athena::generation::validate_generation_document(other.target.document,0).digest;
    auto changed=PreparedOperationalRequest::create(other);
    // Independent expected digest must not be learned from the stored envelope.
    fails([&]{a.recover(p,*f.state,identify_operational_request(changed));});
    require(bytes(f.final())==canonical,"identity collision never replaces final");
    record(f.pending(),"unrelated");
    fails([&]{a.persist(p,*f.state,r);});fails([&]{a.recover(p,*f.state,identify_operational_request(r));});
    require(bytes(f.pending())=="unrelated"&&bytes(f.final())==canonical,"conflicting residue preserved");
    no_activation(f,p);
}
void unsafe_entries() {
    for(unsigned mode=0;mode!=7;++mode) {
        ArchiveFixture f;auto p=f.open();auto r=f.request();auto a=f.archive();a.persist(p,*f.state,r);
        if(mode==0)record(f.final(),"corrupt");
        if(mode==1)fs::permissions(f.final(),fs::perms::group_read,fs::perm_options::add);
        if(mode==2){fs::rename(f.final(),f.root/"saved");fs::create_symlink(f.root/"saved",f.final());}
        if(mode==3)fs::create_hard_link(f.final(),f.root/"extra-link");
        if(mode==4){fs::remove(f.final());fs::create_directory(f.final());}
        if(mode==5){fs::remove(f.final());require(::mkfifo(f.final().c_str(),0600)==0,"fifo fixture");}
        if(mode==6){int fd=::open(f.final().c_str(),O_WRONLY);require(fd>=0,"sparse fixture open");
            const auto rc=::ftruncate(fd,max_operational_request_size+1);::close(fd);require(rc==0,"sparse oversized record");}
        auto error=fails([&]{a.recover(p,*f.state,identify_operational_request(r));});
        require(error.outcome==RequestArchiveOutcome::NotMutated,"unsafe record rejected before mutation");
        no_activation(f,p);
    }
    ArchiveFixture f;auto p=f.open();auto r=f.request();auto a=f.archive();
    fs::rename(f.directory,f.root/"saved-archive");fs::create_directory(f.directory);fs::permissions(f.directory,fs::perms::owner_all);
    fails([&]{a.persist(p,*f.state,r);});require(fs::is_empty(f.directory),"stale archive handle cannot write replacement");
    fs::create_directory_symlink(f.root/"saved-archive",f.root/"alias");
    fails([&]{OperationalRequestArchive::open(f.root/"alias",f.binding);});
    fails([&]{OperationalRequestArchive::open(f.root/"generations/archive",f.binding);});
    fails([&]{OperationalRequestArchive::open(f.root/"requests/../saved-archive",f.binding);});
}
void stale() {
    ArchiveFixture f;auto p=f.open();auto r=f.request();auto a=f.archive();a.persist(p,*f.state,r);
    auto first=activate_prepared_operational(p,*f.state,r);
    const auto target=athena::generation::read_generation_document(f.binding.generation_authority,1);
    auto next=prepare_operational_request(p,*f.state,{id(11),f.binding,f.base(10,0,first.identity),
        r.input().target,{1,target.digest,target.bytes,{}},{}});
    activate_prepared_operational(p,*f.state,next);
    auto e=fails([&]{a.recover(p,*f.state,identify_operational_request(r));});
    require(e.stage==RequestArchiveStage::Validation&&e.outcome==RequestArchiveOutcome::NotMutated,"stale base rejected without rebase");
    require(bytes(f.final())==serialize_operational_request(r),"stale original retained");
    require(inspect_selection(p,*f.state)==std::optional<CurrentSelection>(CurrentV1{1,id(11),next.identity().digest}),"no automatic replay");
}
void identity_and_missing_generation() {
    ArchiveFixture f;auto p=f.open();auto r=f.request();auto a=f.archive();a.persist(p,*f.state,r);
    auto wrong=identify_operational_request(r);wrong.plan.activation_id=id(99);
    auto error=fails([&]{a.recover(p,*f.state,wrong);});
    require(error.outcome==RequestArchiveOutcome::NotMutated,"independent missing identity never enumerates alternatives");
    auto wrong_binding=f.binding;wrong_binding.projection_id=d1_test::projection_id;
    wrong_binding.projection_id=ProjectionId(std::string(32,'a'));
    auto other=OperationalRequestArchive::open(f.directory,wrong_binding);
    fails([&]{other.recover(p,*f.state,identify_operational_request(r));});
    fs::remove(f.root/"generations/0/generation.toml");
    fails([&]{a.recover(p,*f.state,identify_operational_request(r));});
    require(!fs::exists(f.root/"generations/0/generation.toml")&&bytes(f.final())==serialize_operational_request(r),
        "archive is not a GC root and never reconstructs missing generations");
    no_activation(f,p);
}
void equivalent_plan_different_envelope() {
    ArchiveFixture f;auto p=f.open();auto initial=f.request();auto selected=activate_prepared_operational(p,*f.state,initial);
    const auto locator=f.binding.store_authority+"/ghost";
    write(f.root/"generations/1/generation.toml","id=1\nentries=[{package='pkg',store='"+locator+"'}]\n");
    const auto target=athena::generation::read_generation_document(f.binding.generation_authority,1);
    OperationalPlanInput raw{id(11),f.binding,f.base(10,0,selected.identity),initial.input().target,
        {1,target.digest,target.bytes,{{LegacyTreeSource{locator,LegacySealDigest(d1_test::digest('a'))},{"bin/tool"},{}}}}, {}};
    auto original=prepare_operational_request(p,*f.state,raw);
    raw.target.sources[0].directories={"bin"}; // Same derived D8 operations.
    auto alternative=PreparedOperationalRequest::create(raw);
    require(original.identity().digest==alternative.identity().digest&&
        serialize_operational_request(original)!=serialize_operational_request(alternative),"plan digest is not an envelope identity");
    auto expected=identify_operational_request(original),different=identify_operational_request(alternative);
    require(expected.envelope!=different.envelope,"B2 exact envelope identity is independent of plan digest");
    auto a=f.archive();a.persist(p,*f.state,original);
    const auto file=f.directory/(id(11).value()+".toml");
    record(file,serialize_operational_request(alternative));
    auto error=fails([&]{a.recover(p,*f.state,expected);});
    require(error.stage==RequestArchiveStage::Read&&error.outcome==RequestArchiveOutcome::NotMutated,
        "same-plan canonical envelope substitution rejected before barriers");
    record(file,serialize_operational_request(original));a.recover(p,*f.state,expected);
    require(inspect_selection(p,*f.state)==std::optional<CurrentSelection>(CurrentV1{0,id(10),selected.identity.digest}),
        "recovering stored source claims never executes them");
}
void fault_boundaries() {
    // Fail every write/barrier/publication event before OR after its real syscall.
    unsigned total=0;
    {ArchiveFixture f;auto p=f.open();auto r=f.request();RequestArchiveIo io;auto w=io.write;auto sync=io.files.fsync;auto rename=io.files.rename;
     io.write=[&](int fd,const void* b,size_t n){++total;return w(fd,b,n);};
     io.files.fsync=[&](int fd){++total;return sync(fd);};
     io.files.rename=[&](int a,const char* b,int c,const char* d,unsigned flags){++total;return rename(a,b,c,d,flags);};
     auto a=f.archive(io);a.persist(p,*f.state,r);}
    require(total==7,"process-death matrix covers every write, barrier and rename event");
    for(bool after:{false,true})for(unsigned boundary=1;boundary<=total;++boundary) {
        ArchiveFixture f;auto p=f.open();auto r=f.request();RequestArchiveIo io;
        unsigned count=0;bool armed=false;auto hit=[&]{return armed&&++count==boundary;};
        auto w=io.write;auto sync=io.files.fsync;auto rename=io.files.rename;
        io.write=[&](int fd,const void* b,size_t n)->ssize_t{bool fail=hit();if(fail&&!after){errno=EIO;return -1;}
            auto rc=w(fd,b,n);if(fail){errno=EIO;return -1;}return rc;};
        io.files.fsync=[&](int fd){bool fail=hit();if(fail&&!after){errno=EIO;return -1;}
            auto rc=sync(fd);if(fail){errno=EIO;return -1;}return rc;};
        io.files.rename=[&](int a,const char* b,int c,const char* d,unsigned flags){bool fail=hit();if(fail&&!after){errno=EIO;return -1;}
            auto rc=rename(a,b,c,d,flags);if(fail){errno=EIO;return -1;}return rc;};
        auto a=f.archive(io);armed=true;auto error=fails([&]{a.persist(p,*f.state,r);});armed=false;
        require(error.outcome==RequestArchiveOutcome::Uncertain,"archive errors never claim activation outcome");
        auto fresh=f.archive();
        // Full request retry is the only authority to resume a short/empty prefix.
        fresh.persist(p,*f.state,r);auto loaded=fresh.recover(p,*f.state,identify_operational_request(r));
        require(serialize_operational_request(loaded.request)==serialize_operational_request(r),"same bytes after every fault");
        no_activation(f,p);
    }
    std::cout<<"B2 fault boundaries: "<<total<<" x before/after\n";
}
void short_write_and_read_faults() {
    {
    ArchiveFixture f;auto p=f.open();auto r=f.request();RequestArchiveIo io;auto w=io.write;unsigned writes=0;
    io.write=[&](int fd,const void* b,size_t n)->ssize_t{
        if(++writes==1){errno=EINTR;return -1;}return w(fd,b,std::min(n,size_t(17)));};
    auto a=f.archive(io);a.persist(p,*f.state,r);require(writes>2,"short writes and EINTR");
    RequestArchiveIo bad;auto pread=bad.files.pread;bool once=true;
    bad.files.pread=[&](int fd,void* b,size_t n,off_t o)->ssize_t{
        if(once){once=false;errno=EINTR;return -1;}return pread(fd,b,n,o);};
    auto retry=f.archive(bad);retry.recover(p,*f.state,identify_operational_request(r));
    bad.files.pread=[](int,void*,size_t,off_t)->ssize_t{return 0;};
    auto incomplete=f.archive(bad);fails([&]{incomplete.recover(p,*f.state,identify_operational_request(r));});
    }
    for(bool invalid_count:{false,true}) {
        ArchiveFixture empty;auto context=empty.open();auto original=empty.request();RequestArchiveIo broken;
        broken.write=[&](int,const void*,size_t n)->ssize_t{return invalid_count?static_cast<ssize_t>(n+1):0;};
        auto failing=empty.archive(broken);auto error=fails([&]{failing.persist(context,*empty.state,original);});
        require(error.stage==RequestArchiveStage::Write&&error.outcome==RequestArchiveOutcome::Uncertain,"invalid/zero write fails closed");
        require(!fs::exists(empty.final()),"invalid write cannot publish");
        auto repaired=empty.archive();repaired.persist(context,*empty.state,original);no_activation(empty,context);
    }
}
void publication_collision() {
    ArchiveFixture f;auto p=f.open();auto r=f.request();RequestArchiveIo io;auto rename=io.files.rename;
    io.files.rename=[&](int a,const char* b,int c,const char* d,unsigned flags){
        require(flags==RENAME_NOREPLACE,"immutable no-replace publication");record(f.final(),"foreign");
        return rename(a,b,c,d,flags);};
    auto a=f.archive(io);auto error=fails([&]{a.persist(p,*f.state,r);});
    require(error.stage==RequestArchiveStage::Publication&&error.outcome==RequestArchiveOutcome::Uncertain,"publication conflict reported");
    require(bytes(f.final())=="foreign"&&bytes(f.pending())==serialize_operational_request(r),"racing final and original pending preserved");
    no_activation(f,p);
}
void cleanup_faults() {
    for(unsigned mode=0;mode!=4;++mode) {
        ArchiveFixture f;auto p=f.open();auto r=f.request();auto a=f.archive();a.persist(p,*f.state,r);
        record(f.pending(),serialize_operational_request(r));
        RequestArchiveIo io;auto unlink=io.files.unlink;auto sync=io.files.fsync;bool removed=false;
        io.files.unlink=[&](int fd,const char* n,int flags){if(mode==0){errno=EIO;return -1;}
            auto rc=unlink(fd,n,flags);removed=true;if(mode==1){errno=EIO;return -1;}return rc;};
        io.files.fsync=[&](int fd){if(removed&&mode>=2){if(mode==3)(void)sync(fd);errno=EIO;return -1;}return sync(fd);};
        auto faulty=f.archive(io);auto error=fails([&]{faulty.recover(p,*f.state,identify_operational_request(r));});
        require(error.outcome==RequestArchiveOutcome::Uncertain,"cleanup uncertainty explicit");
        require(bytes(f.final())==serialize_operational_request(r),"cleanup never removes durable final");
        auto fresh=f.archive();fresh.recover(p,*f.state,identify_operational_request(r));fresh.recover(p,*f.state,identify_operational_request(r));
        require(!fs::exists(f.pending()),"exact residue cleanup idempotent");no_activation(f,p);
    }
}
void substitutions() {
    for(bool root:{false,true}) {
        ArchiveFixture f;auto p=f.open();auto r=f.request();RequestArchiveIo io;auto sync=io.files.fsync;bool changed=false;
        io.files.fsync=[&](int fd){auto rc=sync(fd);if(!changed){changed=true;
            if(root){fs::rename(f.directory,f.root/"retired-archive");fs::create_directory(f.directory);fs::permissions(f.directory,fs::perms::owner_all);}
            else{fs::rename(f.pending(),f.root/"retired-request");record(f.pending(),"foreign");}}
            return rc;};
        auto a=f.archive(io);fails([&]{a.persist(p,*f.state,r);});
        require(changed&&!fs::exists(f.final()),"substitution before publication detected");no_activation(f,p);
    }
}
void child_processes() {
    ArchiveFixture f;auto p=f.open();auto r=f.request();auto a=f.archive();a.persist(p,*f.state,r);
    f.state.reset();
    pid_t children[2];
    for(auto& child:children){child=::fork();require(child>=0,"fork recovery");
        if(!child){::execl("/proc/self/exe","operational_request_archive_test","recover",f.root.c_str(),r.identity().digest.value().c_str(),identify_operational_request(r).envelope.value().c_str(),nullptr);::_exit(99);}}
    for(auto child:children){int status;require(::waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"serialized fresh-process recovery");}
    f.state=std::make_unique<ProjectionState>(f.binding.generation_authority);no_activation(f,p);
}
void process_death() {
    // Kill a fresh exec at every write/barrier/rename boundary, then reopen under
    // a newly acquired State. This models process death, not storage power loss.
    for(bool after:{false,true})for(unsigned boundary=1;boundary<=7;++boundary) {
        ArchiveFixture f;auto r=f.request();f.state.reset();
        int pipefd[2];require(::pipe2(pipefd,O_CLOEXEC)==0,"death pipe");
        const auto child=::fork();require(child>=0,"death fork");
        if(!child){::close(pipefd[0]);if(::fcntl(pipefd[1],F_SETFD,0)<0)::_exit(98);
            const auto event=std::to_string(boundary),fd=std::to_string(pipefd[1]);const char* when=after?"after":"before";
            ::execl("/proc/self/exe","operational_request_archive_test","crash",f.root.c_str(),r.identity().digest.value().c_str(),identify_operational_request(r).envelope.value().c_str(),
                event.c_str(),when,fd.c_str(),nullptr);::_exit(99);}
        ::close(pipefd[1]);char ready=0;const auto received=::read(pipefd[0],&ready,1);::close(pipefd[0]);
        (void)::kill(child,SIGKILL);int status;require(::waitpid(child,&status,0)==child,"wait death");
        require(received==1&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGKILL,"child reached crash boundary");
        f.state=std::make_unique<ProjectionState>(f.binding.generation_authority);auto p=f.open();auto fresh=f.archive();
        // Empty/partial unpublished preparations require the independent full
        // request; a complete preparation/final is recoverable by pinned identity.
        if(fs::exists(f.final())||(fs::exists(f.pending())&&bytes(f.pending())==serialize_operational_request(r)))
            fresh.recover(p,*f.state,identify_operational_request(r));
        else {fails([&]{fresh.recover(p,*f.state,identify_operational_request(r));});fresh.persist(p,*f.state,r);}
        auto loaded=fresh.recover(p,*f.state,identify_operational_request(r));
        require(serialize_operational_request(loaded.request)==serialize_operational_request(r),"death recovery preserves original exact request");
        no_activation(f,p);
    }
}
void run() {roundtrip();partial_and_collision();unsafe_entries();stale();identity_and_missing_generation();equivalent_plan_different_envelope();fault_boundaries();short_write_and_read_faults();publication_collision();cleanup_faults();substitutions();child_processes();process_death();}
}
int main(int argc,char** argv) {
    return d1_test::run([&]{
        if((argc==5&&std::string_view(argv[1])=="recover")||(argc==8&&std::string_view(argv[1])=="crash")) {
            fs::path root(argv[2]);ProjectionBinding binding{d1_test::projection_id,(root/"generations").native(),(root/"store").native(),(root/"projection").native()};
            athena::store::ObjectStore objects(root/"store");ProjectionState state(binding.generation_authority);
            auto context=ProjectionContext::open(binding,objects);
            if(argc==8) {
                const auto boundary=std::stoul(argv[5]);const bool after=std::string_view(argv[6])=="after";const int output=std::stoi(argv[7]);
                unsigned count=0;auto pause=[&]{char ready='x';if(::write(output,&ready,1)!=1)::_exit(97);for(;;)::pause();};
                RequestArchiveIo io;auto w=io.write;auto sync=io.files.fsync;auto rename=io.files.rename;
                io.write=[&](int fd,const void* b,size_t n)->ssize_t{const bool hit=++count==boundary;if(hit&&!after)pause();auto rc=w(fd,b,n);if(hit)pause();return rc;};
                io.files.fsync=[&](int fd){const bool hit=++count==boundary;if(hit&&!after)pause();auto rc=sync(fd);if(hit)pause();return rc;};
                io.files.rename=[&](int a,const char* b,int c,const char* d,unsigned flags){const bool hit=++count==boundary;if(hit&&!after)pause();auto rc=rename(a,b,c,d,flags);if(hit)pause();return rc;};
                auto archive=OperationalRequestArchive::open(root/"requests",binding,io);
                const auto doc=athena::generation::read_generation_document(binding.generation_authority,0);
                auto r=prepare_operational_request(context,state,{id(10),binding,AbsentBase{}, {},{0,doc.digest,doc.bytes,{}},{}});
                require(r.identity().digest==PlanDigest(argv[3])&&identify_operational_request(r).envelope==RequestEnvelopeDigest(argv[4]),
                    "child independent plan and exact-byte identities");archive.persist(context,state,r);
                throw std::runtime_error("crash boundary was not reached");
            }
            auto archive=OperationalRequestArchive::open(root/"requests",binding);
            auto loaded=archive.recover(context,state,{{id(10),PlanDigest(argv[3])},RequestEnvelopeDigest(argv[4])});
            require(loaded.request.identity().activation_id==id(10),"child exact request");return;
        }
        run();
    });
}
