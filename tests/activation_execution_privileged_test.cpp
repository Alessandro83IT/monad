// SPDX-License-Identifier: GPL-3.0-or-later
// VM-only qualification. CTest label prevents ordinary host validation from running it.
#include "activation/reconciliation.hpp"
#include "activation/legacy.hpp"
#include "activation_execution_support.hpp"
#include "artifact/scanner.hpp"
#include "artifact/identity.hpp"
#include "store/transaction_pins.hpp"
#include "store/manifest.hpp"
#include "store/metadata.hpp"
#include <iostream>
#include <sys/mount.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/vfs.h>
#include <linux/magic.h>
#include <sys/syscall.h>
#include <linux/capability.h>
using namespace execution_test;
using namespace athena;
namespace {
realization::RealizationRecord record(core::ArtifactId artifact) {
    using namespace core;
    RepositorySnapshotRef snapshot{RepositoryId("repo"),RepositoryRevision("rev"),SchemaVersion(1),ContentDigest(d1_test::digest('a')),SnapshotId(d1_test::digest('b'))};
    return {{PackageId("demo"),PackageVersion("1"),snapshot.repository_id,snapshot,RepositoryProvenance{snapshot.repository_id,snapshot.revision,{},snapshot}},artifact,{},ArtifactOrigin::AmbientSourceBuild,
        installed::SemanticState{PackageMetadata{PackageId("demo"),PackageVersion("1")}}};
}
struct SourceFixture : Fixture {
    PlanIdentity zero;
    SourceFixture() : zero(bootstrap_source_authority()) {}
    core::ArtifactId object(std::string name,std::string content) {
        auto source=root/name;fs::create_directory(source);fs::create_directory(source/"bin");write(source/"bin/tool",content);
        auto artifact=artifact::derive_artifact_id(artifact::scan(source));objects.publish_unprotected_for_test(source,artifact);return artifact;
    }
    void generation(unsigned n,const std::vector<realization::RealizationRecord>& records) {
        fs::create_directories(root/"generations"/std::to_string(n));
        fs::permissions(root/"generations"/std::to_string(n),fs::perms::owner_all);
        write(root/"generations"/std::to_string(n)/"generation.toml",generation::serialize(generation::make_generation(n,{},records)));
        fs::permissions(root/"generations"/std::to_string(n)/"generation.toml",fs::perms::owner_read|fs::perms::owner_write);
    }
    PlanIdentity publish(unsigned activation,unsigned n,ActivationBase base,std::vector<ActivationSource> sources,std::vector<ActivationOperation> operations) {
        auto p=open();ActivationIntent intent{id(activation),binding.generation_authority,binding.store_authority,binding.projection_root,binding.projection_id,n,digest(n),std::move(base)};
        ActivationPlan plan{id(activation),std::move(sources),std::move(operations)};
        return {id(activation),publish_activation_plan(p,*state,intent,plan).document.digest};
    }
};
void interrupted(Fixture& f,const PlanIdentity& plan,std::string kind) {
    f.state.reset();int pipefd[2];require(::pipe(pipefd)==0,"VM kill pipe");auto child=::fork();require(child>=0,"VM kill fork");
    if(!child){::close(pipefd[0]);try{ProjectionState state(f.binding.generation_authority);ProjectionIo io;
        auto stop=[&]{char byte='x';if(::write(pipefd[1],&byte,1)!=1)_exit(9);::pause();};
        if(kind=="link"){auto real=io.link;io.link=[&,real](int a,const char* b,int c,const char* d,int flags){auto rc=real(a,b,c,d,flags);if(rc==0&&fs::read_symlink("/proc/self/fd/"+std::to_string(c)).filename()==plan.activation_id.value())stop();return rc;};}
        else if(kind=="remove"){auto real=io.unlink;io.unlink=[&,real](int a,const char* b,int flags){auto rc=real(a,b,flags);if(rc==0&&std::string(b)=="tool")stop();return rc;};}
        else{auto real=io.rename;io.rename=[&,real](int a,const char* b,int c,const char* d,unsigned flags){auto rc=real(a,b,c,d,flags);if(rc==0&&std::string(d)=="tool")stop();return rc;};}
        auto p=f.open(io);reconcile_selection(p,state,plan);_exit(10);
    }catch(...){_exit(11);}}
    ::close(pipefd[1]);char byte;require(::read(pipefd[0],&byte,1)==1,"VM kill application boundary");::close(pipefd[0]);require(::kill(child,SIGKILL)==0,"VM SIGKILL");int status;require(::waitpid(child,&status,0)==child&&WIFSIGNALED(status),"VM wait kill");
    f.state=std::make_unique<ProjectionState>(f.binding.generation_authority);auto p=f.open();reconcile_selection(p,*f.state,plan);reconcile_selection(p,*f.state,plan);
    require(inspect_reconciliation(p,*f.state,plan).state==ReconciliationState::Complete,"VM application recovery complete");
}
void sources() {
    SourceFixture f; auto a=f.object("a","first"), b=f.object("b","second"); auto p=f.open();
    const auto& zero=f.zero;
    f.generation(1,{record(a)});f.generation(2,{record(a)});f.generation(3,{record(b)});f.generation(4,{});
    auto create=f.publish(2,1,f.base(1,0,zero),{ArtifactSource{a}},{{0,"bin",EnsureDirectory{}},{1,"bin/tool",CreateLink{0,{id(2),1}}}});
    {ProjectionIo io;io.durable.fsync=[](int){errno=EIO;return -1;};auto injected=f.open(io);
        rejected([&]{commit_selection(injected,*f.state,create);});}
    require(recover_selection(p,*f.state,create).state==SelectionState::BaseSelected,"source fsync failure does not select target");
    commit_selection(p,*f.state,create);
    {ProjectionIo io;io.durable.fsync=[](int){errno=EIO;return -1;};auto injected=f.open(io);
        rejected([&]{reconcile_selection(injected,*f.state,create);});}
    require(inspect_reconciliation(p,*f.state,create).state==ReconciliationState::Incomplete,"selected is not reconciled");
    auto keep=f.publish(3,2,f.base(2,1,create),{ArtifactSource{a}},{{0,"bin",EnsureDirectory{}},{1,"bin/tool",KeepLink{0,{id(2),1},0,{id(3),1}}}});
    rejected([&]{commit_selection(p,*f.state,keep);}); // incomplete predecessor cannot become a base
    interrupted(f,create,"rename");commit_selection(p,*f.state,keep);
    require(inspect_reconciliation(p,*f.state,keep).state==ReconciliationState::Incomplete,"KEEP needs successor witness");
    interrupted(f,keep,"link");
    std::vector<ActivationSource> pair{ArtifactSource{a},ArtifactSource{b}};
    if(source_identity_less(pair[1],pair[0]))std::swap(pair[0],pair[1]);
    unsigned old=std::get<ArtifactSource>(pair[0]).artifact_id==a?0:1;
    auto replace=f.publish(4,3,f.base(3,2,keep),pair,{{0,"bin",EnsureDirectory{}},{1,"bin/tool",ReplaceLink{old,{id(3),1},1-old,{id(4),1}}}});
    commit_selection(p,*f.state,replace);interrupted(f,replace,"rename");
    require(fs::read_symlink(f.root/"projection/bin/tool")==f.objects.object_path(b)/"bin/tool","exact replacement membership");
    auto remove=f.publish(5,4,f.base(4,3,replace),{ArtifactSource{b}},{{0,"bin/tool",RemoveLink{0,{id(4),1}}}});
    commit_selection(p,*f.state,remove);interrupted(f,remove,"remove");
    auto back=f.plan(6,0,f.base(5,4,remove));commit_selection(p,*f.state,back);reconcile_selection(p,*f.state,back);
    require(fs::is_directory(f.root/"projection/bin"),"no D9 scaffolding deletion");
    // Digest agreement alone cannot grant omitted membership authority.
    auto omitted=f.publish(7,1,f.base(6,0,back),{ArtifactSource{a}},{});
    rejected([&]{commit_selection(p,*f.state,omitted);});
    // D1 permits REMOVE + ENSURE_DIRECTORY at one path. Replaying after mkdir
    // must recognize the final directory instead of attempting to unlink it.
    auto file_stage=f.root/"node-file";fs::create_directory(file_stage);write(file_stage/"node","old");
    auto file_id=artifact::derive_artifact_id(artifact::scan(file_stage));f.objects.publish_unprotected_for_test(file_stage,file_id);
    auto tree_stage=f.root/"node-tree";fs::create_directories(tree_stage/"node");write(tree_stage/"node/tool","child");
    auto tree_id=artifact::derive_artifact_id(artifact::scan(tree_stage));f.objects.publish_unprotected_for_test(tree_stage,tree_id);
    f.generation(5,{record(file_id)});f.generation(6,{record(tree_id)});
    auto node=f.publish(8,5,f.base(6,0,back),{ArtifactSource{file_id}},{{0,"node",CreateLink{0,{id(8),0}}}});
    commit_selection(p,*f.state,node);reconcile_selection(p,*f.state,node);
    std::vector<ActivationSource> transition_sources{ArtifactSource{file_id},ArtifactSource{tree_id}};
    if(source_identity_less(transition_sources[1],transition_sources[0]))std::swap(transition_sources[0],transition_sources[1]);
    unsigned file_index=std::get<ArtifactSource>(transition_sources[0]).artifact_id==file_id?0:1;
    auto directory=f.publish(9,6,f.base(8,5,node),transition_sources,{{0,"node",RemoveLink{file_index,{id(8),0}}},{1,"node",EnsureDirectory{}},{2,"node/tool",CreateLink{1-file_index,{id(9),2}}}});
    commit_selection(p,*f.state,directory);reconcile_selection(p,*f.state,directory);reconcile_selection(p,*f.state,directory);
    require(inspect_reconciliation(p,*f.state,directory).state==ReconciliationState::Complete,"link-to-directory repeatable final state");
    auto conversion=f.publish(10,5,f.base(9,6,directory),transition_sources,{{0,"node/tool",RemoveLink{1-file_index,{id(9),2}}},{1,"node",CreateLink{file_index,{id(10),1}}}});
    rejected([&]{commit_selection(p,*f.state,conversion);});
    require(fs::is_directory(f.root/"projection/node"),"directory-to-link never deletes scaffolding");

}
void legacy() {
    Fixture f;auto source=f.root/"store/demo-1";fs::create_directories(source/"bin");write(source/"bin/tool","legacy");
    store::write_manifest(source,{{"bin/tool"}});store::write_metadata(source,{"demo","1","fixture","checksum","cmake"});
    write(f.root/"generations/1/generation.toml","id = 1\n\n[[entries]]\npackage = \"demo\"\nstore = \""+source.native()+"\"\n");
    write(f.root/"generations/current","1\n");fs::create_directory(f.root/"projection/bin");fs::create_symlink(source/"bin/tool",f.root/"projection/bin/tool");
    auto p=f.open();auto sealed=seal_legacy(p,*f.state,1,source.native()).source;
    adopt_legacy_projection(p,*f.state,d1_test::adoption_id,{sealed});
    std::vector<ActivationSource> sources;std::visit([&](const auto& value){sources.emplace_back(value);},sealed);
    ActivationIntent intent{id(9),f.binding.generation_authority,f.binding.store_authority,f.binding.projection_root,f.binding.projection_id,1,f.digest(1),
        SelectedBase{HistoricalCurrent{1,"1\n"},f.digest(1),AdoptionOwnership{d1_test::adoption_id}}};
    ActivationPlan plan{id(9),sources,{{0,"bin",EnsureDirectory{}},{1,"bin/tool",KeepLink{0,{d1_test::adoption_id,0},0,{id(9),1}}}}};
    auto expected=PlanIdentity{id(9),publish_activation_plan(p,*f.state,intent,plan).document.digest};
    commit_selection(p,*f.state,expected);reconcile_selection(p,*f.state,expected);
    // A new seal cannot rewrite the old selected source identity at the same locator.
    write(source/"bin/tool","changed");auto changed=seal_legacy(p,*f.state,1,source.native()).source;
    std::vector<ActivationSource> changed_sources;std::visit([&](const auto& value){changed_sources.emplace_back(value);},changed);
    auto removal_intent=ActivationIntent{id(11),f.binding.generation_authority,f.binding.store_authority,f.binding.projection_root,f.binding.projection_id,0,f.digest(0),f.base(9,1,expected)};
    auto removal_plan=ActivationPlan{id(11),changed_sources,{{0,"bin/tool",RemoveLink{0,{id(9),1}}}}};
    auto mismatch=PlanIdentity{id(11),publish_activation_plan(p,*f.state,removal_intent,removal_plan).document.digest};
    rejected([&]{commit_selection(p,*f.state,mismatch);});
    require(inspect_selection(p,*f.state)==std::optional<CurrentSelection>(CurrentV1{1,id(9),expected.digest}),"source mismatch preserves selection");
    write(source/"bin/tool","legacy");
    removal_intent.activation_id=id(10);removal_plan.activation_id=id(10);removal_plan.sources=sources;
    auto remove=PlanIdentity{id(10),publish_activation_plan(p,*f.state,removal_intent,removal_plan).document.digest};
    commit_selection(p,*f.state,remove);reconcile_selection(p,*f.state,remove);reconcile_selection(p,*f.state,remove);
    require(inspect_reconciliation(p,*f.state,remove).state==ReconciliationState::Complete,"adopted succession/removal complete");

}
void owner() {
    {Fixture f;auto p=f.open();auto plan=f.plan(1,0,AbsentBase{});commit_selection(p,*f.state,plan);
        auto current=f.root/"generations/current";require(::chown(current.c_str(),65534,65534)==0,"foreign current owner");
        rejected([&]{recover_selection(p,*f.state,plan);});require(::chown(current.c_str(),::geteuid(),::getegid())==0,"restore current owner");recover_selection(p,*f.state,plan);}
    Fixture f;auto p=f.open();p.ensure_directory(*f.state,"bin");p.create_symlink(*f.state,"bin/tool","literal",{id(1),0});
    auto path=f.root/"projection/bin/tool";
    require(::lchown(path.c_str(),65534,65534)==0,"foreign owner fixture");
    rejected([&]{p.remove_symlink(*f.state,"bin/tool","literal",{id(1),0});});
    rejected([&]{p.replace_symlink(*f.state,"bin/tool","literal",{id(1),0},"new",{id(2),0});});
    require(::lchown(path.c_str(),::geteuid(),::getegid())==0,"restore owner");
    p.remove_symlink(*f.state,"bin/tool","literal",{id(1),0});
    auto plan=f.plan(3,0,AbsentBase{}); // foreign payload is preserved, bootstrap rejects
    rejected([&]{commit_selection(p,*f.state,plan);});
}
void mounts(bool bind) {
    Fixture f;auto p=f.open();p.ensure_directory(*f.state,"bin");p.create_symlink(*f.state,"bin/tool","literal",{id(1),0});
    auto destination=f.root/"projection/bin",external=f.root/"external";fs::create_directory(external);
    if(::mount(bind?external.c_str():"tmpfs",destination.c_str(),bind?nullptr:"tmpfs",bind?MS_BIND:0,bind?nullptr:"size=1m")<0){if(errno==EPERM)throw std::system_error(EPERM,std::generic_category(),"mount prerequisite");throw std::system_error(errno,std::generic_category(),"mount fixture");}
    struct Guard {fs::path path;~Guard(){if(::umount2(path.c_str(),0)<0){std::cerr<<"FAIL unmount; preserving fixture: "<<path<<'\n';_exit(1);}}} guard{destination};
    auto plan=f.plan(1,0,AbsentBase{});
    auto gen_external=f.root/"gen-external";fs::create_directory(gen_external);fs::copy_file(f.root/"generations/0/generation.toml",gen_external/"generation.toml");
    auto gen_target=f.root/"generations/0";
    if(::mount(bind?gen_external.c_str():"tmpfs",gen_target.c_str(),bind?nullptr:"tmpfs",bind?MS_BIND:0,bind?nullptr:"size=1m")<0)throw std::system_error(errno,std::generic_category(),"generation mount fixture");
    Guard generation_guard{gen_target};
    rejected([&]{commit_selection(p,*f.state,plan);});
    rejected([&]{p.ensure_directory(*f.state,"bin/sub");});
    rejected([&]{p.remove_symlink(*f.state,"bin/tool","literal",{id(1),0});});
    rejected([&]{p.replace_symlink(*f.state,"bin/tool","literal",{id(1),0},"new",{id(2),0});});
}

}
int main(int argc,char** argv) {
    if (argc > 1 && std::string(argv[1]) == "fixture-construction-failure") {
        try {
            const auto parent = fs::temp_directory_path() / ("athena-d6-exception-" + std::to_string(::getpid()));
            fs::create_directory(parent);
            require(fs::directory_iterator(parent) == fs::directory_iterator{}, "D6 exception fixture parent initially empty");
            require(::setenv("ATHENA_EXECUTION_TEST_TMPDIR", parent.c_str(), 1) == 0, "D6 exception fixture parent");
            require(::setenv("ATHENA_TEST_FAIL_D6_FIXTURE_SETUP", "1", 1) == 0, "D6 failure injection");
            try { execution_test::Fixture f; throw std::runtime_error("D6 injected failure was not raised"); }
            catch (const std::runtime_error& e) {
                require(std::string(e.what()) == "injected D6 fixture-construction failure", "D6 expected injected exception");
            }
            ::unsetenv("ATHENA_TEST_FAIL_D6_FIXTURE_SETUP");
            require(fs::directory_iterator(parent) == fs::directory_iterator{}, "D6 constructor failure removed owned temporary directory");
            fs::remove(parent);
            return 0;
        } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    }
    if(::geteuid()!=0){std::cout<<"SKIP: Debian qualification VM root required\n";return 77;}
    __user_cap_header_struct header{_LINUX_CAPABILITY_VERSION_3,0};__user_cap_data_struct data[2]{};
    if(::syscall(SYS_capget,&header,data)<0 || !(data[CAP_SYS_ADMIN/32].effective & (1u << (CAP_SYS_ADMIN%32)))) {std::cout<<"SKIP: CAP_SYS_ADMIN required for complete scanner/mount inspection\n";return 77;}
    try {
        const std::string mode=argc>1?argv[1]:"sources";
        if(mode=="legacy")legacy();else if(mode=="owner")owner();else if(mode=="bind"||mode=="nested")mounts(mode=="bind");
        else if(mode=="ext4") {
            const auto parent=::getenv("ATHENA_D67_EXT4_TMPDIR");if(!parent){std::cout<<"SKIP: ATHENA_D67_EXT4_TMPDIR required\n";return 77;}
            auto fd=core::detail::open_directory(parent);auto st=core::detail::stat_fd(fd.get());struct statfs filesystem{};
            require(st.st_uid==0 && (st.st_mode&07777)==0700,"private root-owned ext4 fixture parent");
            require(::fstatfs(fd.get(),&filesystem)==0 && filesystem.f_type==EXT4_SUPER_MAGIC,"ext4 fixture prerequisite");
            require(::setenv("ATHENA_EXECUTION_TEST_TMPDIR",parent,1)==0,"fixture parent");sources();
        }else if(mode=="sources")sources();else throw std::runtime_error("Unknown VM mode");
    }catch(const std::system_error& e){if(e.code().value()==EPERM){std::cout<<"SKIP: "<<e.what()<<'\n';return 77;}std::cerr<<e.what()<<'\n';return 1;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
