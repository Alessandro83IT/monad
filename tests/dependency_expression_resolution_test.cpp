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
}
