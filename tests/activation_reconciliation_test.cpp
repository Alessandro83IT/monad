// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation/reconciliation.hpp"
#include "activation_execution_support.hpp"
#include <iostream>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <linux/fs.h>
using namespace execution_test;
namespace {
WitnessReference old() { return {id(10),0}; }
WitnessReference next() { return {id(11),0}; }
void create(ProjectionContext& p, const ProjectionState& s) { p.ensure_directory(s,"bin"); p.create_symlink(s,"bin/link","old",old()); }
void replace(ProjectionContext& p, const ProjectionState& s) { p.replace_symlink(s,"bin/link","old",old(),"new",next()); }
void replay(ProjectionContext& p, const ProjectionState& s) {
    p.ensure_directory(s,"dir"); replace(p,s); p.remove_symlink(s,"bin/link","new",next());
}
void mechanics() {
    Fixture f; auto p=f.open(); create(p,*f.state);
    replace(p,*f.state); replace(p,*f.state);
    p.validate_owned_symlink("bin/link","new",next());
    require(fs::read_symlink(f.root/"projection"/".athena-activation"/"witnesses"/id(10).value()/"0")=="old","retain old witness");
    p.remove_symlink(*f.state,"bin/link","new",next()); p.remove_symlink(*f.state,"bin/link","new",next());
    require(!fs::exists(f.root/"projection"/"bin"/"link"),"REMOVE satisfied");
    p.ensure_directory(*f.state,"bin");
    fs::create_symlink("new",f.root/"projection"/"bin"/"link");
    rejected([&]{p.remove_symlink(*f.state,"bin/link","new",next());});
    rejected([&]{replace(p,*f.state);});
    require(fs::read_symlink(f.root/"projection"/"bin"/"link")=="new","foreign preserved");
    fs::create_symlink("bin",f.root/"projection"/"alias");
    rejected([&]{p.ensure_directory(*f.state,"alias/sub");});
    rejected([&]{p.ensure_directory(*f.state,"alias");});
    write(f.root/"projection"/"regular","foreign");
    rejected([&]{p.ensure_directory(*f.state,"regular");});
    rejected([&]{p.replace_symlink(*f.state,"regular","old",old(),"new",next());});
    p.ensure_directory(*f.state,"directory");
    rejected([&]{p.replace_symlink(*f.state,"directory","old",old(),"new",next());});
    rejected([&]{p.ensure_directory(*f.state,".athena-activation/work");});
    require(fs::is_regular_file(f.root/"projection"/"regular") && fs::is_directory(f.root/"projection"/"directory"),"no conversion");
}
void orchestration() {
    Fixture f; auto p=f.open(); auto plan=f.plan(1,0,AbsentBase{});
    rejected([&]{reconcile_selection(p,*f.state,plan);});
    commit_selection(p,*f.state,plan);
    {ProjectionIo io;auto real=io.fsync;
        io.fsync=[&,real](int fd){if(fs::read_symlink("/proc/self/fd/"+std::to_string(fd)).filename()=="current"){errno=EIO;return -1;}return real(fd);};
        auto injected=f.open(io);rejected([&]{reconcile_selection(injected,*f.state,plan);});}
    require(inspect_reconciliation(p,*f.state,plan).state==ReconciliationState::Complete,"empty selected complete");
    reconcile_selection(p,*f.state,plan); reconcile_selection(p,*f.state,plan);
    auto pending=f.plan(2,1,f.base(1,0,plan));
    rejected([&]{reconcile_selection(p,*f.state,pending);});
    require(recover_selection(p,*f.state,pending).state==SelectionState::BaseSelected,"no implicit selection");
}
void faults() {
    for(unsigned operation=0;operation<3;++operation) {
        unsigned count=0;
        auto action=[&](ProjectionContext& p,const ProjectionState& s) {
            if(operation==0) replace(p,s);
            else if(operation==1) p.remove_symlink(s,"bin/link","old",old());
            else p.ensure_directory(s,"dir");
        };
        { Fixture f; auto p=f.open(); create(p,*f.state); ProjectionIo io; auto real=io.fsync;
            io.fsync=[&](int fd){++count;return real(fd);}; auto injected=f.open(io); action(injected,*f.state); }
        for(unsigned boundary=1;boundary<=count;++boundary) {
            Fixture f; auto p=f.open(); create(p,*f.state); ProjectionIo io; auto real=io.fsync; unsigned hits=0;
            io.fsync=[&](int fd){if(++hits==boundary){errno=EIO;return -1;}return real(fd);}; auto injected=f.open(io);
            rejected([&]{action(injected,*f.state);}); auto fresh=f.open(); action(fresh,*f.state); action(fresh,*f.state);
        }
        std::cout<<"D7 operation "<<operation<<" fsync boundaries: "<<count<<'\n';
    }
    for(unsigned operation=0;operation<7;++operation) {
        Fixture f; auto p=f.open(); create(p,*f.state); ProjectionIo io;
        if(operation==0) io.rename=[](int,const char*,int,const char*,unsigned){errno=EIO;return -1;};
        if(operation==1) io.unlink=[](int,const char*,int){errno=EIO;return -1;};
        if(operation==2) io.mkdir=[](int,const char*,mode_t){errno=EIO;return -1;};
        if(operation==3) io.link=[](int,const char*,int,const char*,int){errno=EIO;return -1;};
        if(operation==4) io.symlink=[](const char*,int,const char*){errno=EIO;return -1;};
        if(operation==5) { auto real=io.stat; io.stat=[real](int fd,const char* name,int flags,unsigned mask,struct statx* s){auto rc=real(fd,name,flags,mask,s); if(rc==0 && std::string(name)=="link") ++s->stx_mnt_id; return rc;}; }
        if(operation==6) { auto real=io.stat; io.stat=[real](int fd,const char* name,int flags,unsigned mask,struct statx* s){auto rc=real(fd,name,flags,mask,s); if(rc==0 && std::string(name)=="link") ++s->stx_uid; return rc;}; }
        auto injected=f.open(io);
        rejected([&]{if(operation==1) injected.remove_symlink(*f.state,"bin/link","old",old()); else if(operation==2) injected.ensure_directory(*f.state,"dir"); else replace(injected,*f.state);});
        auto fresh=f.open(); fresh.validate_owned_symlink("bin/link","old",old());
    }
}
void kills() {
    for(unsigned operation=0;operation<3;++operation) {
        Fixture f; {auto p=f.open();create(p,*f.state);} f.state.reset(); int fd[2]; require(::pipe(fd)==0,"pipe");
        auto child=::fork(); require(child>=0,"fork");
        if(!child) {::close(fd[0]); try {ProjectionState s(f.binding.generation_authority); ProjectionIo io;
            auto stop=[&]{char byte='x';if(::write(fd[1],&byte,1)!=1)_exit(8);::pause();};
            if(operation==0) {auto real=io.rename;io.rename=[&,real](int a,const char* b,int c,const char* d,unsigned flags){auto rc=real(a,b,c,d,flags);if(rc==0 && std::string(d)=="link")stop();return rc;};}
            if(operation==1) {auto real=io.unlink;io.unlink=[&,real](int a,const char* b,int flags){auto rc=real(a,b,flags);if(rc==0 && std::string(b)=="link")stop();return rc;};}
            if(operation==2) {auto real=io.mkdir;io.mkdir=[&,real](int a,const char* b,mode_t m){auto rc=real(a,b,m);if(rc==0 && std::string(b)=="dir")stop();return rc;};}
            auto p=f.open(io);if(operation==0)replace(p,s);else if(operation==1)p.remove_symlink(s,"bin/link","old",old());else p.ensure_directory(s,"dir");_exit(9);
        }catch(...){_exit(10);}}
        ::close(fd[1]);char byte;require(::read(fd[0],&byte,1)==1,"kill mutation boundary");::close(fd[0]);require(::kill(child,SIGKILL)==0,"kill");int status;require(::waitpid(child,&status,0)==child&&WIFSIGNALED(status),"wait");
        f.state=std::make_unique<ProjectionState>(f.binding.generation_authority);auto p=f.open();
        if(operation==0){replace(p,*f.state);p.validate_owned_symlink("bin/link","new",next());}
        else if(operation==1){p.remove_symlink(*f.state,"bin/link","old",old());p.remove_symlink(*f.state,"bin/link","old",old());}
        else {p.ensure_directory(*f.state,"dir");p.ensure_directory(*f.state,"dir");}
    }
}
}
int main(){try{mechanics();orchestration();faults();kills();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
