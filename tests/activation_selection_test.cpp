// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation/selection.hpp"
#include "activation_execution_support.hpp"
#include "store/gc.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <iostream>
using namespace athena::activation;
namespace fs = std::filesystem;
using d1_test::require;
namespace {
using namespace execution_test;
void source_authority_bootstrap() {
    Fixture f;
    athena::store::AuthorityContext authority(f.binding.store_authority,f.binding.generation_authority);
    require(!fs::exists(f.root/"generations/current"),"persisted generations are not an initialized authority");
    f.state.reset();
    try {
        athena::store::TransactionPins pins(authority);
        throw std::runtime_error("uninitialized authority accepted");
    } catch(const std::runtime_error& e) {
        require(std::string(e.what())=="uninitialized generation authority","pin rejection identifies missing current");
    }
    rejected([&]{athena::store::GarbageCollector(authority).collect();});
    require(!fs::exists(authority.registry()) && !fs::exists(f.root/"generations/.athena-store-authority.toml"),
        "rejection does not bind uninitialized authorities");
    athena::generation::GenerationManager manager(f.root/"generations",athena::generation::GenerationManager::GenerationDirectoryTag{});
    rejected([&]{manager.initialize();});
    require(!fs::exists(f.root/"generations/current"),"projection binding requires explicit D6 bootstrap");
    f.state=std::make_unique<ProjectionState>(f.binding.generation_authority);
    auto zero=f.bootstrap_source_authority();
    auto p=f.open();
    require(inspect_selection(p,*f.state)==std::optional<CurrentSelection>(CurrentV1{0,zero.activation_id,zero.digest}),
        "source authority retains explicit D6 selection");
    authority.validate_binding(false);
    f.state.reset();
    { athena::store::TransactionPins pins(authority); pins.abandon(); }
    require(athena::store::GarbageCollector(authority).collect().empty(),"initialized source authority supports GC");
}
void transitions() {
    Fixture f; auto p=f.open(); auto first=f.plan(1,0,AbsentBase{});
    require(recover_selection(p,*f.state,first).state==SelectionState::BaseSelected,"recovery never promotes");
    require(commit_selection(p,*f.state,first).state==SelectionState::TargetSelected,"bootstrap 0");
    require(inspect_selection(p,*f.state)==std::optional<CurrentSelection>(CurrentV1{0,first.activation_id,first.digest}),"separate current identity observation");
    require(commit_selection(p,*f.state,first).state==SelectionState::TargetSelected,"exact retry");
    auto next=f.plan(2,1,f.base(1,0,first)); commit_selection(p,*f.state,next);
    require(athena::generation::GenerationManager(f.root/"generations",athena::generation::GenerationManager::GenerationDirectoryTag{}).current().id==1,"manager reads CurrentV1");
    rejected([&]{commit_selection(p,*f.state,first);});
    auto back=f.plan(3,0,f.base(2,1,next)); commit_selection(p,*f.state,back);
    require(recover_selection(p,*f.state,back).state==SelectionState::TargetSelected,"return to 0");
    rejected([&]{commit_selection(p,*f.state,{back.activation_id,PlanDigest("sha256:"+std::string(64,'a'))});});
    f.state.reset();
    athena::generation::GenerationManager manager(f.root/"generations",athena::generation::GenerationManager::GenerationDirectoryTag{});
    rejected([&]{manager.switch_to(1,f.root/"projection");});
}
void invalid() {
    { Fixture f; auto p=f.open(); auto plan=f.plan(1,1,AbsentBase{}); rejected([&]{commit_selection(p,*f.state,plan);}); }
    { Fixture f; auto p=f.open(); auto plan=f.plan(1,0,AbsentBase{}); write(f.root/"projection"/"foreign","x"); rejected([&]{commit_selection(p,*f.state,plan);}); }
    { Fixture f; auto p=f.open(); auto plan=f.plan(1,0,AbsentBase{}); write(f.root/"generations"/"current","00\n"); rejected([&]{commit_selection(p,*f.state,plan);}); }
    { Fixture f; auto p=f.open(); auto plan=f.plan(1,0,AbsentBase{}); write(f.root/"generations"/"0"/"generation.toml","bad"); rejected([&]{commit_selection(p,*f.state,plan);}); }
    { Fixture f; auto p=f.open(); auto plan=f.plan(1,0,AbsentBase{}); write(f.root/"generations"/(".athena-current-"+id(1).value()),"incomplete"); rejected([&]{commit_selection(p,*f.state,plan);}); require(fs::exists(f.root/"generations"/(".athena-current-"+id(1).value())),"preserve residue"); }
    { Fixture f; write(f.root/"generations"/"current","0\n"); auto p=f.open(); auto plan=f.plan(1,1,SelectedBase{HistoricalCurrent{0,"0\n"},f.digest(0),EmptyOwnership{}}); commit_selection(p,*f.state,plan); }
    { Fixture f; auto p=f.open(); auto plan=f.plan(1,0,AbsentBase{}); fs::permissions(f.root/"generations"/"0"/"generation.toml",fs::perms::others_write,fs::perm_options::add); rejected([&]{commit_selection(p,*f.state,plan);}); }
    { Fixture f; auto p=f.open(); auto plan=f.plan(1,0,AbsentBase{}); fs::create_symlink("0/generation.toml",f.root/"generations"/"current"); rejected([&]{commit_selection(p,*f.state,plan);}); }
    { Fixture f; auto p=f.open(); auto plan=f.plan(1,0,AbsentBase{}); write(f.root/"generations/0/generation.toml","id = 0\n"); rejected([&]{commit_selection(p,*f.state,plan);}); }
    { Fixture f; auto p=f.open(); auto plan=f.plan(1,0,AbsentBase{}); auto inspected=inspect_activation_plan(p,*f.state,plan.activation_id);
      write(f.root/"generations"/(".athena-current-"+plan.activation_id.value()),serialize(CurrentSelection(CurrentV1{0,plan.activation_id,plan.digest})));
      fs::permissions(f.root/"generations"/(".athena-current-"+plan.activation_id.value()),fs::perms::owner_read|fs::perms::owner_write|fs::perms::group_read);
      rejected([&]{commit_selection(p,*f.state,plan);});require(!fs::exists(f.root/"generations/current"),"unsafe preparation never selected"); }
    { Fixture f; auto plan=f.plan(1,0,AbsentBase{}); ProjectionIo io;
      io.durable.write=[](int,const void*,size_t)->ssize_t{errno=EIO;return -1;}; auto p=f.open(io);
      rejected([&]{commit_selection(p,*f.state,plan);});auto fresh=f.open();
      require(recover_selection(fresh,*f.state,plan).state==SelectionState::BaseSelected,"partial preparation has no authority");
      rejected([&]{commit_selection(fresh,*f.state,plan);}); }
    { Fixture f; auto plan=f.plan(1,0,AbsentBase{}); ProjectionIo io;
      io.rename=[](int,const char*,int,const char*,unsigned){errno=EIO;return -1;};auto p=f.open(io);
      rejected([&]{commit_selection(p,*f.state,plan);});auto fresh=f.open();
      require(recover_selection(fresh,*f.state,plan).state==SelectionState::BaseSelected,"failed rename preserves base");
      commit_selection(fresh,*f.state,plan); }
    { Fixture f; auto p=f.open(); auto plan=f.plan(1,0,AbsentBase{}); auto plans=f.root/"projection/.athena-activation/plans";
      // D5 refuses unrelated incomplete preparation before selecting any target.
      fs::create_directory(plans/".preparations"/id(99).value());
      rejected([&]{commit_selection(p,*f.state,plan);});require(!fs::exists(f.root/"generations/current"),"D5 incomplete blocks commit"); }
}
void faults() {
    unsigned boundaries=0;
    { Fixture f; auto plan=f.plan(1,0,AbsentBase{}); ProjectionIo io; auto real=io.fsync; io.fsync=[&](int fd){++boundaries;return real(fd);}; auto p=f.open(io); commit_selection(p,*f.state,plan); }
    for(unsigned n=1;n<=boundaries;++n) {
        Fixture f; auto plan=f.plan(1,0,AbsentBase{}); ProjectionIo io; auto real=io.fsync; unsigned count=0;
        io.fsync=[&](int fd){if(++count==n){errno=EIO;return -1;}return real(fd);}; auto p=f.open(io);
        rejected([&]{commit_selection(p,*f.state,plan);});
        auto fresh=f.open(); auto state=recover_selection(fresh,*f.state,plan).state;
        if(state==SelectionState::BaseSelected) commit_selection(fresh,*f.state,plan);
        require(recover_selection(fresh,*f.state,plan).state==SelectionState::TargetSelected,"fsync recovery");
    }
    std::cout << "D6 fsync boundaries: " << boundaries << '\n';
}
void io_faults() {
    for(unsigned kind=0;kind<3;++kind) {
        auto run=[&](unsigned boundary) {
            Fixture f;auto plan=f.plan(1,0,AbsentBase{});ProjectionIo io;unsigned count=0;bool armed=false;
            auto fault=[&]{if(!armed)return false;return ++count==boundary;};
            if(kind==0){auto real=io.open;io.open=[&,real](int fd,const char* name,const open_how& how){if(fault()){errno=EIO;return -1;}return real(fd,name,how);};}
            if(kind==1){auto real=io.stat;io.stat=[&,real](int fd,const char* name,int flags,unsigned mask,struct statx* st){if(fault()){errno=EIO;return -1;}return real(fd,name,flags,mask,st);};}
            if(kind==2){auto real=io.pread;io.pread=[&,real](int fd,void* bytes,size_t size,off_t offset)->ssize_t{if(fault()){errno=EIO;return -1;}return real(fd,bytes,size,offset);};}
            auto p=f.open(io);armed=true;
            if(boundary)rejected([&]{commit_selection(p,*f.state,plan);});else commit_selection(p,*f.state,plan);
            armed=false;
            if(boundary){auto fresh=f.open();auto result=recover_selection(fresh,*f.state,plan);
                if(result.state==SelectionState::BaseSelected)commit_selection(fresh,*f.state,plan);
                recover_selection(fresh,*f.state,plan);}
            return count;
        };
        unsigned count=run(0);for(unsigned n=1;n<=count;++n)run(n);
        std::cout<<"D6 I/O kind "<<kind<<" boundaries: "<<count<<'\n';
    }
}
void instance_substitution() {
    Fixture f;auto plan=f.plan(1,0,AbsentBase{});{auto p=f.open();commit_selection(p,*f.state,plan);}
    ProjectionIo io;auto real=io.fsync;bool current_synced=false,changed=false;
    io.fsync=[&](int fd) {
        auto rc=real(fd);if(rc<0)return rc;
        auto path=fs::read_symlink("/proc/self/fd/"+std::to_string(fd));
        if(path==f.root/"generations/current")current_synced=true;
        else if(current_synced && !changed && path==f.root/"generations") {
            fs::rename(f.root/"generations/current",f.root/"retired-current");
            write(f.root/"generations/current",serialize(CurrentSelection(CurrentV1{0,plan.activation_id,plan.digest})));
            fs::permissions(f.root/"generations/current",fs::perms::owner_read|fs::perms::owner_write);changed=true;
        }
        return rc;
    };
    auto p=f.open(io);rejected([&]{recover_selection(p,*f.state,plan);});require(changed,"current barrier substitution observed");
    auto fresh=f.open();recover_selection(fresh,*f.state,plan);
}
void processes() {
    Fixture f; auto plan=f.plan(1,0,AbsentBase{});
    auto probe=[&](bool busy) {
        auto child=::fork();require(child>=0,"probe fork");
        if(!child){::execl("/proc/self/exe","activation_selection_test","probe",f.binding.generation_authority.c_str(),busy?"busy":"free",nullptr);_exit(20);}
        int status;require(::waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"independent State probe");
    };
    probe(true);
    rejected([&]{athena::core::FileLock lease(f.root/"lease",athena::core::LockKind::TransactionLease);});
    f.state.reset();probe(false);
    {athena::core::FileLock gc(athena::core::store_gc_lock_path(f.binding.store_authority),athena::core::LockKind::StoreGc);
        rejected([&]{ProjectionState state(f.binding.generation_authority);});}
    pid_t children[2];
    for(auto& child:children){child=::fork();require(child>=0,"commit fork");if(!child){::execl("/proc/self/exe","activation_selection_test","commit",f.binding.generation_authority.c_str(),f.binding.store_authority.c_str(),f.binding.projection_root.c_str(),plan.activation_id.value().c_str(),plan.digest.value().c_str(),nullptr);_exit(21);}}
    for(auto child:children){int status;require(::waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"serialized independent commits");}
    f.state=std::make_unique<ProjectionState>(f.binding.generation_authority);auto p=f.open();recover_selection(p,*f.state,plan);
    PlanIdentity competitors[]{f.plan(2,1,f.base(1,0,plan)),f.plan(3,2,f.base(1,0,plan))};
    f.state.reset();
    for(unsigned n=0;n<2;++n){children[n]=::fork();require(children[n]>=0,"competing fork");if(!children[n]){
        ::execl("/proc/self/exe","activation_selection_test","commit",f.binding.generation_authority.c_str(),f.binding.store_authority.c_str(),f.binding.projection_root.c_str(),competitors[n].activation_id.value().c_str(),competitors[n].digest.value().c_str(),nullptr);_exit(22);}}
    unsigned winners=0,conflicts=0;
    for(auto child:children){int status;require(::waitpid(child,&status,0)==child&&WIFEXITED(status),"competing wait");
        winners+=WEXITSTATUS(status)==0;conflicts+=WEXITSTATUS(status)==4;}
    require(winners==1&&conflicts==1,"exactly one different-plan commit wins");
}
void kills() {
    for(bool after : {false,true}) {
        Fixture f; auto plan=f.plan(1,0,AbsentBase{}); f.state.reset(); int pipefd[2]; require(::pipe(pipefd)==0,"pipe");
        auto child=::fork(); require(child>=0,"fork");
        if(!child) { ::close(pipefd[0]); try { ProjectionState state(f.binding.generation_authority); ProjectionIo io; auto real=io.rename;
            io.rename=[&](int a,const char* b,int c,const char* d,unsigned flags) {
                if(std::string(d)=="current") { if(after) { auto rc=real(a,b,c,d,flags); if(rc<0)return rc; }
                    char signal='x'; if(::write(pipefd[1],&signal,1)!=1) _exit(10); ::pause(); }
                return real(a,b,c,d,flags);
            };
            auto p=f.open(io); commit_selection(p,state,plan); _exit(11);
        } catch(...) {_exit(12);} }
        ::close(pipefd[1]); char signal; require(::read(pipefd[0],&signal,1)==1,"kill boundary"); ::close(pipefd[0]);
        require(::kill(child,SIGKILL)==0,"SIGKILL"); int status; require(::waitpid(child,&status,0)==child && WIFSIGNALED(status),"wait kill");
        f.state=std::make_unique<ProjectionState>(f.binding.generation_authority); auto p=f.open();
        require(recover_selection(p,*f.state,plan).state==(after?SelectionState::TargetSelected:SelectionState::BaseSelected),"selection survives kill boundary");
        commit_selection(p,*f.state,plan); recover_selection(p,*f.state,plan);
    }
}
}
int main(int argc,char** argv) { try {
    if(argc==2 && std::string(argv[1])=="metadata") {
        Fixture f;athena::generation::GenerationManager manager(f.root/"generations",athena::generation::GenerationManager::GenerationDirectoryTag{});
        require(::mkfifo((f.root/"generations/current").c_str(),0600)==0,"current FIFO fixture");
        rejected([&]{manager.current();});auto p=f.open();rejected([&]{inspect_selection(p,*f.state);});
        fs::remove(f.root/"generations/current");write(f.root/"generations/current",std::string(max_current_size+1,'1'));
        rejected([&]{manager.current();});rejected([&]{inspect_selection(p,*f.state);});
        f.state.reset();rejected([&]{manager.initialize();});
        Fixture writing;auto plan=writing.plan(1,0,AbsentBase{});ProjectionIo io;
        io.durable.write=[](int,const void*,size_t)->ssize_t{errno=EIO;return -1;};auto writer=writing.open(io);
        try {commit_selection(writer,*writing.state,plan);throw std::runtime_error("write failure accepted");}
        catch(const ExecutionError& e){require(e.error==EIO&&e.cause==ExecutionCause::Io&&e.outcome==ProjectionOutcome::OutcomeUncertain,"write errno/outcome preserved");}
        return 0;
    }
    if(argc==4 && std::string(argv[1])=="probe") {
        bool busy=std::string(argv[3])=="busy";
        try {athena::core::FileLock lock(athena::core::state_lock_path(argv[2]),athena::core::LockKind::State,athena::core::LockMode::Exclusive,"probe",false);return busy?2:0;}
        catch(const std::system_error& e){return busy&&(e.code().value()==EAGAIN||e.code().value()==EWOULDBLOCK)?0:3;}
    }
    if(argc==7 && std::string(argv[1])=="commit") {
        ProjectionState state(argv[2]);athena::store::ObjectStore objects(argv[3]);
        auto p=ProjectionContext::open({d1_test::projection_id,argv[2],argv[3],argv[4]},objects);
        try {commit_selection(p,state,{ActivationId(argv[5]),PlanDigest(argv[6])});return 0;}
        catch(const ExecutionError& e){if(e.cause==ExecutionCause::SelectionConflict)return 4;throw;}
    }
    source_authority_bootstrap();transitions();invalid();faults();io_faults();instance_substitution();kills();processes();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
