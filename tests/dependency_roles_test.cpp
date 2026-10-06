// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/dependency/resolver.hpp"
#include "../src/dependency/parser.hpp"
#include "../src/dependency/serializer.hpp"
#include "../src/repository/repository.hpp"
#include <cassert>
#include <algorithm>

using namespace athena;
int main() {
    auto pkg = [](std::string n, std::vector<dependency::Dependency> d = {}) {
        return package::Package{n,"1","","","","",std::move(d)};
    };
    auto edge = [](std::string n, dependency::DependencyKind k, dependency::DependencyContext c) {
        return dependency::Dependency{std::make_shared<const dependency::PackageRef>(n),k,c};
    };
    auto root = pkg("root", {
        edge("runtime",dependency::DependencyKind::Runtime,dependency::DependencyContext::Target),
        edge("build",dependency::DependencyKind::Build,dependency::DependencyContext::Build),
        edge("test",dependency::DependencyKind::Test,dependency::DependencyContext::Build),
        edge("dev",dependency::DependencyKind::Development,dependency::DependencyContext::Host),
        edge("prop",dependency::DependencyKind::Propagated,dependency::DependencyContext::Target),
        edge("optional",dependency::DependencyKind::Optional,dependency::DependencyContext::Target)
    });
    repository::Repository repo;
    repo.add(root);
    for (auto n : {"runtime","build","test","dev","prop","optional"}) repo.add(pkg(n));
    dependency::Environments env{{"host-os","host-arch","host-abi"},{"build-os","build-arch","build-abi"},{"target-os","target-arch","target-abi"}};
    auto runtime = dependency::DependencyResolutionContext::for_phase(dependency::DependencyResolutionContext::Phase::RuntimeInstall);
    auto p = dependency::resolve_plan(root, repo.index(), env, runtime);
    auto has = [](const install::InstallPlan& plan, const std::string& n) { return std::any_of(plan.packages().begin(),plan.packages().end(),[&](const auto& x){return x.name==n;}); };
    assert(has(p,"runtime") && has(p,"prop"));
    assert(!has(p,"build") && !has(p,"test") && !has(p,"dev") && !has(p,"optional"));
    runtime.optional_enabled = true;
    p = dependency::resolve_plan(root,repo.index(),env,runtime);
    assert(has(p,"optional"));
    auto build = dependency::DependencyResolutionContext::for_phase(dependency::DependencyResolutionContext::Phase::Build);
    p = dependency::resolve_plan(root,repo.index(),env,build);
    assert(has(p,"build") && !has(p,"test"));
    auto tests = dependency::DependencyResolutionContext::for_phase(dependency::DependencyResolutionContext::Phase::Test);
    p = dependency::resolve_plan(root,repo.index(),env,tests);
    assert(has(p,"test"));
    auto dev = dependency::DependencyResolutionContext::for_phase(dependency::DependencyResolutionContext::Phase::Development);
    p = dependency::resolve_plan(root,repo.index(),env,dev);
    assert(has(p,"dev") && !has(p,"test"));
    auto d = edge("zlib",dependency::DependencyKind::Build,dependency::DependencyContext::Build);
    auto encoded = dependency::serialize_dependency(d);
    auto decoded = dependency::parse_dependency(encoded);
    assert(decoded.kind==d.kind && decoded.context==d.context);
    assert(dependency::serialize_dependency(dependency::parse_dependency(encoded))==encoded);
    auto logic = dependency::parse_dependency("test(build):(alpha AND (beta OR NOT gamma))");
    assert(logic.kind==dependency::DependencyKind::Test && logic.context==dependency::DependencyContext::Build);
    assert(dependency::serialize_dependency(dependency::parse_dependency(dependency::serialize_dependency(logic)))==dependency::serialize_dependency(logic));
    bool bad=false; try { (void)dependency::parse_dependency("build(os):foo"); } catch (...) { bad=true; }
    assert(bad);
}
