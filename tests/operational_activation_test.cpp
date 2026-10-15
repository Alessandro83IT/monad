// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation/operational_activation.hpp"
#include "activation_execution_support.hpp"
#include <iostream>
using namespace athena::activation;
using namespace execution_test;
namespace {
OperationalGeneration generation(Fixture& f, unsigned n) {
    auto d=athena::generation::read_generation_document(f.binding.generation_authority,n);
    return {n,d.digest,d.bytes,{}};
}
OperationalPlanInput request(Fixture& f) {
    return {id(10),f.binding,AbsentBase{}, {},generation(f,0),{}};
}
template<class F, class Check> void fails(F fn,OperationalStage stage,OperationalFailure failure, Check check) {
    try {fn();throw std::runtime_error("failure expected");}
    catch(const OperationalActivationError& e) {
        require(e.stage==stage&&e.failure==failure&&bool(e.underlying),"error boundary");
        check(e);
    }
}
template<class F> void fails(F fn,OperationalStage stage,OperationalFailure failure) {
    fails(fn,stage,failure,[](const OperationalActivationError& e) {
        try { std::rethrow_exception(e.underlying); }
        catch(const std::exception& original) { require(std::string(original.what()).size()>0,"original diagnostic"); }
    });
}
void execution_error(const OperationalActivationError& e, ExecutionCause cause,
                     ProjectionOutcome outcome, int error, const char* diagnostic) {
    try { std::rethrow_exception(e.underlying); }
    catch(const ExecutionError& original) {
        require(original.cause==cause&&original.outcome==outcome&&original.error==error&&
            std::string(original.what())==diagnostic,"original ExecutionError details");
        return;
    }
    throw std::runtime_error("Expected original ExecutionError");
}
void selected(Fixture& f, ProjectionContext& p, const OperationalPlanInput& r) {
    require(inspect_selection(p,*f.state)==std::optional<CurrentSelection>(
        CurrentV1{r.target.id,r.activation_id,build_operational_plan(r).digest}),"exact selected identity");
}
void run() {
    { Fixture f;auto p=f.open();auto r=request(f);
      const auto file=f.root/"generations/0/generation.toml";
      const auto original=r.target.document;
      write(file,"id=0\nentries=[]\nunexpected=[0,0,0]\n");
      // D8.2 reparses persisted bytes before publishing or selecting anything.
      fails([&]{activate_operational(p,*f.state,r);},OperationalStage::Identity,OperationalFailure::BeforeSelection,
          [](const OperationalActivationError& e){
              try {std::rethrow_exception(e.underlying);}
              catch(const athena::core::TypeError& error){
                  require(std::string_view(error.what()).find("Unknown TOML field")!=std::string_view::npos,
                      "D8.2 reached shared typed parser");return;
              }
              throw std::runtime_error("D8.2 lost frontend error");
          });
      require(!inspect_selection(p,*f.state),"parser failure must not select current");
      require(!fs::exists(f.root/"projection/.athena-activation/plans"/r.activation_id.value()),
          "parser failure must not publish a plan");
      write(file,original);
      activate_operational(p,*f.state,r); }
    { Fixture f;auto p=f.open();auto r=request(f);
      auto result=activate_operational(p,*f.state,r);
      require(result.reconciliation.state==ReconciliationState::Complete,"complete transition");
      require(inspect_selection(p,*f.state)==std::optional<CurrentSelection>(CurrentV1{0,r.activation_id,result.identity.digest}),"single identity");
      auto again=activate_operational(p,*f.state,r,result.identity.digest);
      require(again.publication==PlanPublication::AlreadyPublished&&again.identity.digest==result.identity.digest&&again.identity.activation_id==result.identity.activation_id,"idempotent retry under held State");
      auto next=request(f);next.activation_id=id(12);next.base=f.base(10,0,result.identity);
      next.base_generation=generation(f,0);next.target=generation(f,1);
      auto transitioned=activate_operational(p,*f.state,next);
      require(inspect_selection(p,*f.state)==std::optional<CurrentSelection>(CurrentV1{1,next.activation_id,transitioned.identity.digest}),"selected base transition");
      r.activation_id=id(11);
      fails([&]{activate_operational(p,*f.state,r);},OperationalStage::Identity,OperationalFailure::BeforeSelection);
      require(!fs::exists(f.root/"projection/.athena-activation/plans"/id(11).value()),"conflict before publication"); }
    { Fixture f;auto p=f.open();auto r=request(f);r.target.document="bad";
      fails([&]{activate_operational(p,*f.state,r);},OperationalStage::Identity,OperationalFailure::BeforeSelection,
          [](const OperationalActivationError& e){execution_error(e,ExecutionCause::GenerationMismatch,
              ProjectionOutcome::NotMutated,0,"Requested generation differs from persisted document");});
      require(!inspect_selection(p,*f.state),"identity error never selects"); }
    { Fixture f;auto p=f.open();auto r=request(f);
      fails([&]{activate_operational(p,*f.state,r,PlanDigest("sha256:"+std::string(64,'a')));},OperationalStage::Construction,OperationalFailure::BeforeSelection,
          [](const OperationalActivationError& e){
              try {std::rethrow_exception(e.underlying);}
              catch(const SchemaError& original){require(original.code()==SchemaErrorCode::Reference&&
                  std::string(original.what())=="Authorized plan digest mismatch","original SchemaError details");}
          });
      r.old_witnesses.push_back({"x",WitnessReference{id(1),0}});
      fails([&]{activate_operational(p,*f.state,r);},OperationalStage::Construction,OperationalFailure::BeforeSelection); }
    { Fixture f;auto r=request(f);ProjectionIo io;auto real=io.rename;
      io.rename=[real](int a,const char* b,int c,const char* d,unsigned flags){if(std::string(d)=="current"){errno=EIO;return -1;}return real(a,b,c,d,flags);};
      auto p=f.open(io);
      fails([&]{activate_operational(p,*f.state,r);},OperationalStage::Selection,OperationalFailure::SelectionUncertain,
          [](const OperationalActivationError& e){execution_error(e,ExecutionCause::Io,
              ProjectionOutcome::OutcomeUncertain,EIO,"Atomic current selection rename");});
      auto fresh=f.open();auto canonical=build_operational_plan(r);PlanIdentity identity{r.activation_id,canonical.digest};
      require(inspect_activation_plan(fresh,*f.state,r.activation_id).digest==identity.digest,"published plan retained");
      require(!inspect_selection(fresh,*f.state),"plan not selected");
      rejected([&]{reconcile_selection(fresh,*f.state,identity);});
      activate_operational(fresh,*f.state,r); }
    { Fixture f;auto r=request(f);ProjectionIo io;auto real=io.rename;
      io.rename=[real](int a,const char* b,int c,const char* d,unsigned flags){auto rc=real(a,b,c,d,flags);if(rc==0&&std::string(d)=="current"){errno=EIO;return -1;}return rc;};
      auto p=f.open(io);
      fails([&]{activate_operational(p,*f.state,r);},OperationalStage::Selection,OperationalFailure::SelectionUncertain,
          [](const OperationalActivationError& e){execution_error(e,ExecutionCause::Io,
              ProjectionOutcome::OutcomeUncertain,EIO,"Atomic current selection rename");});
      auto fresh=f.open();selected(f,fresh,r);
      // Retry after a post-rename D6 error: fail exactly the initial current
      // observation. Count mutations to prove no publication/selection/D7 follows.
      ProjectionIo retry_io;auto real_stat=retry_io.stat;unsigned faults=0,mutations=0;
      retry_io.stat=[&](int fd,const char* name,int flags,unsigned mask,struct statx* st) {
          if(std::string(name)=="current"){++faults;errno=EIO;return -1;}
          return real_stat(fd,name,flags,mask,st);
      };
      auto real_rename=retry_io.rename;
      retry_io.rename=[&](int a,const char* b,int c,const char* d,unsigned flags){++mutations;return real_rename(a,b,c,d,flags);};
      auto real_mkdir=retry_io.mkdir;
      retry_io.mkdir=[&](int fd,const char* name,mode_t mode){++mutations;return real_mkdir(fd,name,mode);};
      auto real_symlink=retry_io.symlink;
      retry_io.symlink=[&](const char* a,int fd,const char* b){++mutations;return real_symlink(a,fd,b);};
      auto real_link=retry_io.link;
      retry_io.link=[&](int a,const char* b,int c,const char* d,int flags){++mutations;return real_link(a,b,c,d,flags);};
      auto real_unlink=retry_io.unlink;
      retry_io.unlink=[&](int fd,const char* name,int flags){++mutations;return real_unlink(fd,name,flags);};
      auto retry_context=f.open(retry_io);
      fails([&]{activate_operational(retry_context,*f.state,r);},OperationalStage::Identity,
          OperationalFailure::SelectionUncertain,[](const OperationalActivationError& e){
              require(!e.identity,"inspection failure precedes canonical identity");
              execution_error(e,ExecutionCause::Io,ProjectionOutcome::NotMutated,EIO,"Current inspection");
          });
      require(faults==1&&mutations==0,"failed initial observation does not reach mutations");
      selected(f,fresh,r);
      auto alternative=request(f);alternative.activation_id=id(11);alternative.target=generation(f,1);
      fails([&]{activate_operational(retry_context,*f.state,alternative);},OperationalStage::Identity,
          OperationalFailure::SelectionUncertain,[](const OperationalActivationError& e){
              execution_error(e,ExecutionCause::Io,ProjectionOutcome::NotMutated,EIO,"Current inspection");
          });
      require(faults==2&&mutations==0&&!fs::exists(f.root/"projection/.athena-activation/plans"/id(11).value()),
          "unobservable current cannot publish or apply alternative plan");
      selected(f,fresh,r);
      auto recovered=activate_operational(fresh,*f.state,r);
      require(recovered.identity.activation_id==r.activation_id&&
          recovered.identity.digest==build_operational_plan(r).digest,"retry preserves original identity");
      selected(f,fresh,r);
      ProjectionIo confirmed_io;auto real_sync=confirmed_io.fsync;
      confirmed_io.fsync=[&](int fd){
          if(fs::read_symlink("/proc/self/fd/"+std::to_string(fd))==f.root/"generations/current") {errno=EIO;return -1;}
          return real_sync(fd);
      };
      auto confirmed=f.open(confirmed_io);
      fails([&]{activate_operational(confirmed,*f.state,r);},OperationalStage::Selection,
          OperationalFailure::AfterSelection,[&](const OperationalActivationError& e){
              require(e.identity&&e.identity->activation_id==recovered.identity.activation_id&&
                  e.identity->digest==recovered.identity.digest,"confirmed retry error identity");
              execution_error(e,ExecutionCause::Durability,ProjectionOutcome::OutcomeUncertain,EIO,"Selected current file barrier");
          });
      selected(f,fresh,r); }
    { Fixture f;auto r=request(f);ProjectionIo io;auto real=io.fsync;unsigned current_syncs=0;
      io.fsync=[&](int fd){auto path=fs::read_symlink("/proc/self/fd/"+std::to_string(fd));
        if(path==f.root/"generations/current"&&++current_syncs==1){errno=EIO;return -1;}return real(fd);};
      auto p=f.open(io);
      fails([&]{activate_operational(p,*f.state,r);},OperationalStage::Reconciliation,OperationalFailure::AfterSelection,
          [](const OperationalActivationError& e){execution_error(e,ExecutionCause::Durability,
              ProjectionOutcome::OutcomeUncertain,EIO,"Selected current file barrier");});
      auto fresh=f.open();selected(f,fresh,r);
      activate_operational(fresh,*f.state,r); }
    { Fixture f;auto r=request(f);ProjectionIo io;
      io.rename=[](int,const char*,int,const char*,unsigned){errno=EIO;return -1;};auto p=f.open(io);
      fails([&]{activate_operational(p,*f.state,r);},OperationalStage::Publication,OperationalFailure::BeforeSelection);
      auto fresh=f.open();require(!inspect_selection(fresh,*f.state),"D4 error never selects"); }
}
}
int main(){try{run();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
