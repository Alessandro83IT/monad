#include "../src/dependency/resolver.hpp"
#include "../src/dependency/parser.hpp"
#include "../src/dependency/serializer.hpp"
#include "../src/dependency/constraint.hpp"
#include "../src/repository/repository_index.hpp"
#include <cassert>
#include <algorithm>
#include <memory>

using namespace athena;
namespace {
package::Package pkg(const std::string& name, const std::string& version="1", std::vector<dependency::Dependency> deps={}) {
    return {name,version,"","","","",std::move(deps)};
}
dependency::Dependency dep(dependency::ExpressionPtr e) {
    return {std::move(e),dependency::DependencyKind::Runtime,dependency::DependencyContext::Target};
}
bool has(const install::InstallPlan& p, const std::string& n) {
    return std::any_of(p.packages().begin(),p.packages().end(),[&](const auto& x){return x.name==n;});
}
}
int main() {
    repository::RepositoryIndex repo;
    auto oldlib=pkg("lib","1.0"), midlib=pkg("lib","2.5"), newlib=pkg("lib","3.0");
    auto ssl2=pkg("ssl-provider","2.0"), ssl3=pkg("ssl-provider","3.1");
    ssl2.provides.capabilities={"ssl"}; ssl3.provides.capabilities={"ssl"};
    auto feature1=pkg("feature-provider","1.0"), feature4=pkg("feature-provider","4.0");
    feature1.provides.features={"modern"}; feature4.provides.features={"modern"};
    for (auto p : {oldlib,midlib,newlib,ssl2,ssl3,feature1,feature4,pkg("left"),pkg("right"),pkg("tool"),pkg("bad-provider")}) repo.add(p);

    auto root=pkg("root", "1", {
        dep(dependency::parse_dependency("lib >= 2.0").expression),
        dep(dependency::parse_dependency("lib < 3.0").expression),
        dep(dependency::parse_dependency("capability(ssl) >= 3.0").expression),
        dep(dependency::parse_dependency("feature(modern) {>= 3.0 AND < 5.0}").expression),
        dep(dependency::parse_dependency("missing OR left").expression),
        dep(dependency::parse_dependency("right AND left").expression),
        dep(dependency::parse_dependency("when(platform(target-os), tool)").expression),
        dep(dependency::parse_dependency("when(platform(wrong-os), bad-provider)").expression),
        dep(dependency::parse_dependency("NOT platform(wrong-os)").expression)
    });
    dependency::Environments env{{"host-os","host-arch","host-abi"},{"build-os","build-arch","build-abi"},{"target-os","target-arch","target-abi"}};
    auto policy=dependency::DependencyResolutionContext::for_phase(dependency::DependencyResolutionContext::Phase::RuntimeInstall);
    auto plan=dependency::resolve_plan(root,repo,env,policy);
    assert(has(plan,"lib") && plan.packages()[0].version=="2.5");
    assert(has(plan,"ssl-provider") && plan.packages()[1].version=="3.1");
    assert(has(plan,"feature-provider"));
    assert(has(plan,"left") && has(plan,"right") && has(plan,"tool"));
    assert(!has(plan,"bad-provider") && !has(plan,"missing"));

    auto mixed=dependency::parse_dependency("runtime(target):(left AND build(build):(tool))");
    auto mixed_root=pkg("mixed", "1", {mixed});
    repo.add(mixed_root);
    auto runtime_plan=dependency::resolve_plan(mixed_root,repo,env,policy);
    assert(has(runtime_plan,"left") && !has(runtime_plan,"tool"));
    auto build_policy=dependency::DependencyResolutionContext::for_phase(dependency::DependencyResolutionContext::Phase::Build);
    auto build_plan=dependency::resolve_plan(mixed_root,repo,env,build_policy);
    assert(has(build_plan,"left") && has(build_plan,"tool"));
    auto scoped_or=pkg("scoped-or","1",{dependency::parse_dependency("(optional(target):(missing) OR runtime(target):(right))")});
    repo.add(scoped_or);
    assert(has(dependency::resolve_plan(scoped_or,repo,env,policy),"right"));

    auto scoped_roundtrip=dependency::parse_dependency(dependency::serialize_dependency(mixed));
    assert(scoped_roundtrip.kind==mixed.kind && scoped_roundtrip.context==mixed.context);
    auto conditional=dependency::parse_dependency("development(host):when(platform(host-os), feature(provider-feature) OR false)");
    assert(dependency::serialize_dependency(dependency::parse_dependency(dependency::serialize_dependency(conditional))) == dependency::serialize_dependency(conditional));
    auto conditional_false=dependency::parse_dependency("when(false, tool)");
    auto false_root=pkg("false-root","1",{conditional_false}); repo.add(false_root);
    assert(!has(dependency::resolve_plan(false_root,repo,env,policy),"tool"));
    auto false_atom=pkg("false-atom","1",{dep(dependency::parse_dependency("false").expression)}); repo.add(false_atom);
    bool false_rejected=false; try { (void)dependency::resolve_plan(false_atom,repo,env,policy); } catch (...) { false_rejected=true; }
    assert(false_rejected);
    for (const auto* selector : {"platform(wrong-os)","architecture(wrong-arch)","abi(wrong-abi)"}) {
        auto incompatible=pkg(std::string("selector-")+selector,"1",{dep(dependency::parse_dependency(selector).expression)}); repo.add(incompatible);
        bool rejected=false; try { (void)dependency::resolve_plan(incompatible,repo,env,policy); } catch (...) { rejected=true; }
        assert(rejected);
    }

    auto feature_conditional=pkg("feature-root","1",{dependency::parse_dependency("when(feature(feature-on), tool)")});
    feature_conditional.provides.features={"feature-on"}; repo.add(feature_conditional);
    assert(has(dependency::resolve_plan(feature_conditional,repo,env,policy),"tool"));
    const auto ranged_text=dependency::serialize_dependency(dep(dependency::parse_dependency("feature(modern) {>= 3.0 AND < 5.0}").expression));
    assert(dependency::serialize_dependency(dependency::parse_dependency(ranged_text))==ranged_text);

    auto range=dependency::VersionConstraint::all({
        dependency::VersionConstraint::comparison(dependency::ComparisonOperator::GreaterEqual,"2"),
        dependency::VersionConstraint::not_(dependency::VersionConstraint::comparison(dependency::ComparisonOperator::Equal,"3"))
    });
    assert(dependency::satisfies(range,"2.5") && !dependency::satisfies(range,"3"));

    auto forbidden=pkg("conflict-root","1",{dep(std::make_shared<const dependency::NotExpression>(std::make_shared<const dependency::CapabilityRef>("blocked")))});
    auto provider=pkg("blocked-provider"); provider.provides.capabilities={"blocked"};
    repository::RepositoryIndex conflict_repo; conflict_repo.add(provider); conflict_repo.add(forbidden);
    auto parent=pkg("parent","1",{dep(std::make_shared<const dependency::PackageRef>("blocked-provider")),dep(std::make_shared<const dependency::PackageRef>("conflict-root"))});
    conflict_repo.add(parent);
    bool rejected=false; try { (void)dependency::resolve_plan(parent,conflict_repo,env,policy); } catch (...) { rejected=true; }
    assert(rejected);
    auto compound_not=pkg("compound-not","1",{dep(dependency::parse_dependency("left").expression),dep(dependency::parse_dependency("right").expression),dep(dependency::parse_dependency("NOT (left AND right)").expression)});
    repo.add(compound_not);
    rejected=false; try { (void)dependency::resolve_plan(compound_not,repo,env,policy); } catch (...) { rejected=true; }
    assert(rejected);

    // Explicit feature state overrides package defaults and controls conditional dependencies.
    auto feature_switch=pkg("feature-switch","1",{dependency::parse_dependency("when(feature(extras), tool)")});
    feature_switch.feature_dependencies["extras"]={};
    auto enabled=policy; enabled.feature_states["extras"]=true;
    auto disabled=policy; disabled.feature_states["extras"]=false;
    assert(has(dependency::resolve_plan(feature_switch,repo,env,enabled),"tool"));
    assert(!has(dependency::resolve_plan(feature_switch,repo,env,disabled),"tool"));

    // Capability version is independent of the providing package version.
    auto versioned=pkg("versioned-api","9.0");
    versioned.provides.capabilities={"api"};
    versioned.provides.capability_versions["api"]="2.0";
    repository::RepositoryIndex cap_versions; cap_versions.add(versioned);
    auto wants_api=pkg("wants-api","1",{dep(dependency::parse_dependency("capability(api) >= 3.0").expression)});
    rejected=false; try { (void)dependency::resolve_plan(wants_api,cap_versions,env,policy); } catch (...) { rejected=true; }
    assert(rejected);

    // A conflicting first provider is rolled back and the second provider is selected.
    auto side_effect=pkg("provider-side-effect");
    auto p1=pkg("p1"); p1.provides.capabilities={"choice"};
    p1.dependencies={dep(std::make_shared<const dependency::PackageRef>("provider-side-effect"))};
    p1.conflicts={dep(std::make_shared<const dependency::PackageRef>("blocked"))};
    auto p2=pkg("p2"); p2.provides.capabilities={"choice"};
    auto blocked=pkg("blocked");
    auto choice_root=pkg("choice-root","1",{dep(std::make_shared<const dependency::CapabilityRef>("choice")),dep(std::make_shared<const dependency::PackageRef>("blocked"))});
    repository::RepositoryIndex choices; choices.add(p1); choices.add(p2); choices.add(blocked); choices.add(side_effect);
    auto chosen=dependency::resolve_plan(choice_root,choices,env,policy);
    assert(has(chosen,"p2") && !has(chosen,"p1") && !has(chosen,"provider-side-effect"));

    auto p2_conflicting=p2;
    p2_conflicting.conflicts={dep(std::make_shared<const dependency::PackageRef>("blocked"))};
    repository::RepositoryIndex no_provider_works; no_provider_works.add(p1); no_provider_works.add(p2_conflicting); no_provider_works.add(blocked); no_provider_works.add(side_effect);
    bool providers_failed=false;
    try { (void)dependency::resolve_plan(choice_root,no_provider_works,env,policy); }
    catch (const dependency::ResolutionError& error) {
        providers_failed=true;
        assert(error.code()==dependency::ResolutionErrorCode::ProviderConflict);
    }
    assert(providers_failed);

    auto feature_package=pkg("configured-features");
    feature_package.feature_dependencies["docs"]={dep(std::make_shared<const dependency::PackageRef>("tool"))};
    auto docs_on=policy; docs_on.feature_states["docs"]=true;
    auto docs_off=policy; docs_off.feature_states["docs"]=false;
    assert(has(dependency::resolve_plan(feature_package,repo,env,docs_on),"tool"));
    assert(!has(dependency::resolve_plan(feature_package,repo,env,docs_off),"tool"));

    auto absent_feature=policy; absent_feature.feature_states["does-not-exist"]=true;
    bool feature_error=false;
    try { (void)dependency::resolve_plan(feature_package,repo,env,absent_feature); }
    catch (const dependency::ResolutionError& error) {
        feature_error=true;
        assert(error.code()==dependency::ResolutionErrorCode::FeatureUnsatisfied);
    }
    assert(feature_error);

    auto offered=pkg("offered-feature"); offered.provides.features={"provided-only"};
    repository::RepositoryIndex feature_repo; feature_repo.add(offered);
    auto ask_offered=pkg("ask-offered","1",{dep(std::make_shared<const dependency::FeatureRef>("provided-only"))});
    assert(has(dependency::resolve_plan(ask_offered,feature_repo,env,policy),"offered-feature"));
    auto provided_condition=pkg("provided-condition","1",{dependency::parse_dependency("when(feature(provided-only), tool)")});
    provided_condition.provides.features={"provided-only"};
    assert(has(dependency::resolve_plan(provided_condition,repo,env,policy),"tool"));

    auto composed_conflict=pkg("composed-conflict");
    composed_conflict.conflicts={dep(dependency::parse_dependency("blocked OR capability(choice)").expression)};
    repository::RepositoryIndex compound_repo; compound_repo.add(composed_conflict); compound_repo.add(blocked); compound_repo.add(p1);
    bool conflict_error=false;
    auto selects_conflict=pkg("selects-conflict","1",{dep(std::make_shared<const dependency::PackageRef>("composed-conflict")),dep(std::make_shared<const dependency::PackageRef>("blocked"))});
    try { (void)dependency::resolve_plan(selects_conflict,compound_repo,env,policy); }
    catch (const dependency::ResolutionError& error) {
        conflict_error=true;
        assert(error.code()==dependency::ResolutionErrorCode::Conflict);
    }
    assert(conflict_error);

    auto conflicts_capability=pkg("conflicts-capability","1",{dep(std::make_shared<const dependency::PackageRef>("cap-provider"))});
    conflicts_capability.conflicts={dep(std::make_shared<const dependency::CapabilityRef>("conflicting-api"))};
    auto cap_provider=pkg("cap-provider"); cap_provider.provides.capabilities={"conflicting-api"};
    repository::RepositoryIndex package_capability; package_capability.add(conflicts_capability); package_capability.add(cap_provider);
    bool package_capability_conflict=false;
    try { (void)dependency::resolve_plan(conflicts_capability,package_capability,env,policy); }
    catch (const dependency::ResolutionError& error) {
        package_capability_conflict=true;
        assert(error.code()==dependency::ResolutionErrorCode::Conflict);
    }
    assert(package_capability_conflict);

    auto no_cap_provider=pkg("no-cap-provider","1",{dep(std::make_shared<const dependency::CapabilityRef>("absent-capability"))});
    bool capability_error=false;
    try { (void)dependency::resolve_plan(no_cap_provider,repository::RepositoryIndex{},env,policy); }
    catch (const dependency::ResolutionError& error) {
        capability_error=true;
        assert(error.code()==dependency::ResolutionErrorCode::CapabilityUnsatisfied);
    }
    assert(capability_error);

    auto no_package=pkg("no-package","1",{dep(std::make_shared<const dependency::PackageRef>("missing-package"))});
    bool provider_error=false;
    try { (void)dependency::resolve_plan(no_package,repository::RepositoryIndex{},env,policy); }
    catch (const dependency::ResolutionError& error) {
        provider_error=true;
        assert(error.code()==dependency::ResolutionErrorCode::NoProvider);
    }
    assert(provider_error);

    auto version_conflict=pkg("version-conflict");
    version_conflict.conflicts={dep(dependency::parse_dependency("blocked >= 2.0").expression)};
    auto blocked_v1=pkg("blocked","1.0");
    auto blocked_v2=pkg("blocked","2.0");
    repository::RepositoryIndex version_conflicts; version_conflicts.add(version_conflict); version_conflicts.add(blocked_v1); version_conflicts.add(blocked_v2);
    auto version_conflict_root=pkg("version-conflict-root","1",{dep(std::make_shared<const dependency::PackageRef>("version-conflict")),dep(std::make_shared<const dependency::PackageRef>("blocked"))});
    const auto version_plan=dependency::resolve_plan(version_conflict_root,version_conflicts,env,policy);
    const auto blocked_selected=std::find_if(version_plan.packages().begin(),version_plan.packages().end(),[](const auto& p){return p.name=="blocked";});
    assert(blocked_selected!=version_plan.packages().end() && blocked_selected->version=="1.0");

    const auto deterministic_a=dependency::resolve_plan(choice_root,choices,env,policy);
    const auto deterministic_b=dependency::resolve_plan(choice_root,choices,env,policy);
    assert(deterministic_a.packages().size()==deterministic_b.packages().size());
    for (std::size_t i=0;i<deterministic_a.packages().size();++i) {
        assert(deterministic_a.packages()[i].name==deterministic_b.packages()[i].name);
        assert(deterministic_a.packages()[i].version==deterministic_b.packages()[i].version);
    }
}
