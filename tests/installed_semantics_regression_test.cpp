#include "installed_semantics_support.hpp"
#include "store/manifest.hpp"
#include <toml++/toml.hpp>
#include <functional>
#include <sstream>

using namespace semantic_test;
namespace {
template<class E, class F> void rejected(F action, const std::string& diagnostic) {
    try { action(); }
    catch (const E& e) { require(std::string(e.what()).find(diagnostic) != std::string::npos, e.what()); return; }
    throw std::runtime_error("Expected typed rejection: " + diagnostic);
}
std::string format(const toml::table& t) { std::ostringstream out; out << t; return out.str(); }
void append(generation::Generation& a, const generation::Generation& b) {
    a.realizations.insert(a.realizations.end(), b.realizations.begin(), b.realizations.end());
}
void h1() {
    auto gui = pkg("gui"); gui.features = {"docs"};
    gui.feature_dependencies["docs"] = {{{"B"}}};
    auto g = state(plan({gui, pkg("B")}, {{"docs", true}}));
    const auto& semantics = *member(g, "gui").semantics;
    installed::validate_environment(g);
    rejected<installed::ValidationError>([&] { remove(g, {"B"}); }, "B");
    for (int variant = 0; variant < 5; ++variant) {
        auto doc = toml::parse(generation::serialize(g));
        auto* memberships = doc["memberships"].as_array();
        for (auto& item : *memberships) {
            auto* record = item.as_table()->get_as<toml::table>("record");
            auto* s = record->get_as<toml::table>("semantics");
            auto* p = s->get_as<toml::table>("package");
            if ((*p)["package_id"].value<std::string>() != "gui") continue;
            if (variant == 0) p->insert_or_assign("feature_dependencies", false);
            if (variant == 1) p->insert_or_assign("feature_dependencies", "bad");
            if (variant == 2) p->insert_or_assign("feature_dependencies", toml::array{});
            if (variant == 3) p->insert_or_assign("feature_dependencies", toml::table{{"docs", false}});
            if (variant == 4) p->insert_or_assign("feature_dependencies", toml::table{{"docs", toml::array{
                toml::table{{"expression", "B"}, {"role", 9}}}}});
        }
        rejected<TypeError>([&] {
            auto loaded = generation::deserialize_generation(format(doc));
            (void)remove(loaded, {"B"});
        }, variant < 3 ? "Feature dependencies must be a table" : variant == 3 ? "Feature dependencies must be arrays" : "role");
    }
    auto doc = toml::parse(installed::serialize(semantics));
    auto* p = doc.get_as<toml::table>("package");
    p->erase("feature_dependencies");
    require(installed::deserialize_semantics(format(doc)).package.feature_dependencies.empty(), "absent default");
    p->insert("feature_dependencies", toml::table{});
    require(installed::deserialize_semantics(format(doc)).package.feature_dependencies.empty(), "empty table");
    require(installed::deserialize_semantics(installed::serialize(semantics)) == semantics, "populated table");
}
void h2() {
    auto provider = pkg("provider", "2"); provider.capabilities = {"api"}; provider.features = {"graphics"};
    auto g = state(plan({provider}));
    for (const auto& text : {"provider >= 2", "capability(api) >= 2", "feature(graphics) >= 2"}) {
        installed::PolicyRequirement p{SystemPolicyId("base"), {text}, member(g, "provider").semantics->target};
        require(installed::deserialize_policy(installed::serialize(p)) == p, "atomic policy roundtrip");
        g.policies = {p}; installed::validate_environment(g);
        rejected<installed::ValidationError>([&] { remove(g, {"provider"}); }, "system policy");
    }
    const auto valid = installed::serialize(g.policies.front());
    for (const auto& text : {"optional-disabled(target): missing", "build(build): missing", "runtime(host): missing",
                            "runtime(target): missing", "test(target): missing", "optional(target): missing",
                            "(runtime(target):(missing))", "true OR missing", "NOT missing"}) {
        auto policy = g.policies.front(); policy.selector.text = text;
        rejected<TypeError>([&] { installed::serialize(policy); }, "selector");
        auto doc = toml::parse(valid); doc.insert_or_assign("selector", text);
        rejected<TypeError>([&] { installed::deserialize_policy(format(doc)); }, "selector");
    }
}
void h3() {
    auto p2 = pkg("P2", "2"); p2.capabilities = {"api"}; p2.features = {"graphics"};
    auto p1 = p2; p1.package_id = PackageId("P1"); p1.version = PackageVersion("1");
    auto a = pkg("A", "1", {"capability(api) >= 2"});
    auto c = pkg("C", "1", {"capability(api) < 2"});
    auto invalid = state(plan({a, p2})); append(invalid, state(plan({c, p1})));
    rejected<ResolutionError>([&] { plan({a, c, p1, p2}); }, "");
    for (int order = 0; order < 2; ++order) {
        rejected<installed::ValidationError>([&] { installed::validate_environment(invalid); }, "coherent");
        std::reverse(invalid.realizations.begin(), invalid.realizations.end());
    }
    c.dependencies = {"capability(api) >= 1"};
    auto coherent = state(plan({a, c, p2}));
    installed::validate_environment(coherent);
    auto alternate = p2; alternate.package_id = PackageId("alternate");
    append(coherent, state(plan({alternate})));
    for (int order = 0; order < 2; ++order) {
        installed::validate_environment(coherent);
        auto reassigned = remove(coherent, {"P2"}); installed::validate_environment(reassigned);
        std::reverse(coherent.realizations.begin(), coherent.realizations.end());
    }
    auto replacement = p2; replacement.version = PackageVersion("1");
    auto without_alternate = remove(coherent, {"alternate"});
    rejected<installed::ValidationError>([&] { installed::prepare_install(without_alternate, plan({replacement})); }, "coherent");
    // Transaction preflight rejects the combined environment before acquisition
    // or publication, independently of the CLI and scanner capabilities.
    StoreFixture fixture;
    const auto paths = PathLayout::from_root(fixture.root / "athena");
    fs::create_directories(paths.store); fs::create_directories(paths.build); fs::create_directories(paths.target_root);
    std::ofstream(paths.store / "sentinel") << "unchanged";
    std::ofstream(paths.target_root / "sentinel") << "unchanged";
    generation::GenerationManager manager(paths.generation_state, generation::GenerationManager::GenerationDirectoryTag{});
    manager.initialize(); auto prior = state(plan({a, p2}));
    const auto seed = manager.create({}, prior.realizations);
    std::ofstream(paths.generation_state / "current") << seed.id << '\n';
    store::ObjectStore objects(paths.store);
    const auto before_state = snapshot(paths.generation_state), before_objects = snapshot(paths.store), before_target = snapshot(paths.target_root);
    auto contradictory = c; contradictory.dependencies = {"capability(api) < 2"};
    rejected<installed::ValidationError>([&] {
        install::install_plan(plan({contradictory, p1}), manager, objects, {paths.build}, paths.target_root);
    }, "coherent");
    unchanged(paths.generation_state, before_state); unchanged(paths.store, before_objects); unchanged(paths.target_root, before_target);
    // Feature provider selectors share the same assignment invariant.
    auto fa = pkg("FA", "1", {"feature(graphics) >= 2"});
    auto fc = pkg("FC", "1", {"feature(graphics) < 2"});
    auto features = state(plan({fa, p2})); append(features, state(plan({fc, p1})));
    rejected<installed::ValidationError>([&] { installed::validate_environment(features); }, "coherent");
    // Search must support boolean alternatives rather than greedily binding.
    auto alternatives = state(plan({pkg("A", "1", {"capability(api) >= 2 OR U"}), p2, pkg("U")}));
    c.dependencies = {"capability(api) < 2"};
    append(alternatives, state(plan({c, p1})));
    installed::validate_environment(alternatives); // Both consumers use P1; A uses U.
    rejected<installed::ValidationError>([&] { remove(alternatives, {"U"}); }, "coherent");
    // NOT checks every installed provider, even an unselected alternative.
    member(alternatives, "A").semantics->package.dependencies = {"NOT capability(api) >= 2"};
    rejected<installed::ValidationError>([&] { installed::validate_environment(alternatives); }, "Unsatisfied");
}
void h4() {
    auto g = state(plan({pkg("A"), pkg("B")}));
    installed::validate_environment(g);
    member(g, "B").semantics->target.architecture = "aarch64";
    rejected<installed::ValidationError>([&] { installed::validate_environment(g); }, "target environments");
    member(g, "A").semantics->package.conflicts = {"B"};
    rejected<installed::ValidationError>([&] { installed::validate_environment(g); }, "target environments");
    member(g, "B").semantics->target.architecture = "x86_64";
    rejected<installed::ValidationError>([&] { installed::validate_environment(g); }, "conflict");
    member(g, "A").semantics->package.conflicts.clear();
    member(g, "A").semantics->package.dependencies = {"runtime(host): B", "build(build): B"};
    member(g, "A").semantics->active_roles = {DependencyRole::Runtime, DependencyRole::Build};
    installed::validate_environment(g);
    member(g, "A").semantics->environments.host.architecture = "aarch64";
    rejected<installed::ValidationError>([&] { installed::validate_environment(g); }, "runtime(host)");
}
void h5() {
    auto a = pkg("A"); a.features = {"docs"};
    auto b = pkg("B"); b.feature_dependencies["docs"] = {{{"D"}}};
    auto g = state(plan({a}, {{"docs", true}}));
    auto additional = state(plan({b}));
    member(g, "A").semantics->profile = "P";
    member(additional, "B").semantics->profile = "P";
    append(g, additional);
    rejected<installed::ValidationError>([&] { installed::validate_environment(g); }, "feature dependency");
    append(g, state(plan({pkg("D")})));
    installed::validate_environment(g);
    auto loaded = generation::deserialize_generation(generation::serialize(g));
    installed::validate_environment(loaded);
    auto inherited_removal = remove(loaded, {"A"});
    rejected<installed::ValidationError>([&] { remove(inherited_removal, {"D"}); }, "feature dependency");
    installed::inherit_profile_features(loaded);
    require(member(loaded, "B").semantics->feature_states.at("docs"), "omission did not inherit");
    auto survivors = remove(loaded, {"A"});
    rejected<installed::ValidationError>([&] { remove(survivors, {"D"}); }, "feature dependency");
    // A replacement omitting docs inherits it even when the last carrier is
    // replaced. The prepared/final record path must retain the same contract.
    auto replacement = b;
    auto replace_plan = plan({replacement});
    auto baseline = state(plan({b, pkg("D")}, {{"docs", true}}));
    auto prepared = installed::prepare_install(baseline, replace_plan);
    append(prepared, state(replace_plan));
    installed::inherit_profile_features(prepared, &baseline);
    require(member(prepared, "B").semantics->feature_states.at("docs"), "replacement erased profile configuration");
    rejected<installed::ValidationError>([&] { remove(prepared, {"D"}); }, "feature dependency");
    rejected<installed::ValidationError>([&] { installed::prepare_install(baseline, plan({b}, {{"docs", false}})); }, "Conflicting");
    member(g, "B").semantics->feature_states["docs"] = false;
    rejected<installed::ValidationError>([&] { installed::validate_environment(g); }, "Conflicting");
    member(g, "B").semantics->profile = "Q";
    installed::validate_environment(g); // Explicit false in another profile is independent.
    member(g, "B").semantics->feature_states.clear();
    installed::validate_environment(remove(g, {"D"})); // Omission in Q does not inherit P.
    // Guard configuration also inherits within a profile.
    member(g, "B").semantics->profile = "P";
    member(g, "B").semantics->package.feature_dependencies.clear();
    member(g, "B").semantics->package.dependencies = {"when(feature(docs), missing)"};
    rejected<installed::ValidationError>([&] { installed::validate_environment(g); }, "missing");
}
void m1() {
    StoreFixture f;
    generation::GenerationManager manager(f.root / "state"); manager.initialize();
    const auto historical_path = f.root / "state/generations/41/generation.toml";
    fs::create_directories(historical_path.parent_path());
    const auto old_store = f.root / "historical-store";
    fs::create_directories(old_store); std::ofstream(old_store / "payload") << "historical";
    store::write_manifest(old_store, {{"payload"}});
    // Genuine schema-2 legacy activation retains its original contract even
    // without dependency metadata. It is not a newly written schema-3 value.
    std::ofstream(historical_path) << "schema_version=2\nid=41\n[[memberships]]\nkind='legacy'\npackage='old'\nstore='"
        << old_store.native() << "'\n";
    const auto old = manager.get(41);
    require(old.compatibility == generation::GenerationCompatibility::Historical, "lost historical mode");
    auto unversioned = generation::deserialize_generation("id=9\nentries=[]\n");
    require(unversioned.compatibility == generation::GenerationCompatibility::Historical, "lost unversioned mode");
    manager.switch_to(41, f.root / "target");
    require(manager.current().id == 41 && fs::is_symlink(f.root / "target/payload"), "legitimate historical switch failed");
    auto unknown = state(plan({pkg("unknown")})).realizations.front(); unknown.semantics.reset();
    const auto modern = manager.create({}, {unknown});
    require(manager.get(modern.id).compatibility == generation::GenerationCompatibility::Modern, "schema 3 downgraded");
    fs::create_directories(f.root / "target"); std::ofstream(f.root / "target/sentinel") << "unchanged";
    const auto before_state = snapshot(f.root / "state"); const auto before_target = snapshot(f.root / "target");
    rejected<installed::ValidationError>([&] { manager.switch_to(modern.id, f.root / "target"); }, "dependency state is unavailable");
    unchanged(f.root / "state", before_state); unchanged(f.root / "target", before_target);
    require(manager.current().id == 41, "modern unknown switch changed current");
}
}
int main(int argc, char** argv) {
    const std::map<std::string, std::function<void()>> cases{{"H1", h1}, {"H2", h2}, {"H3", h3}, {"H4", h4}, {"H5", h5}, {"M1", m1}};
    require(argc == 1 || (argc == 2 && cases.contains(argv[1])), "unknown regression case");
    for (const auto& [name, test] : cases) if (argc == 1 || name == argv[1]) {
        test(); std::cout << name << " regression passed\n";
    }
}
