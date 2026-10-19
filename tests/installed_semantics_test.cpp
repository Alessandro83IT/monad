// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "installed_semantics_support.hpp"
#include "dependency/parser.hpp"
#include "cli_install_support.hpp"
#include "filesystem_fixture_support.hpp"
#include <toml++/toml.hpp>
using namespace semantic_test;
int main() {
    StoreFixture fixture;
    auto base = state(plan({pkg("A", "1", {"B >= 2"}), pkg("B", "2"), pkg("U")}));
    installed::validate_environment(base);
    require(remove(base, {"U"}).realizations.size() == 2, "unrelated removal");
    rejects([&] { remove(base, {"B"}); }, "Unsatisfied dependency");
    require(remove(base, {"A", "B"}).realizations.size() == 1, "batch removal");
    auto transitive = state(plan({pkg("A", "1", {"B"}), pkg("B", "1", {"C"}), pkg("C")}));
    rejects([&] { remove(transitive, {"C"}); });
    rejects([&] { remove(transitive, {"B", "C"}); });
    auto valid = plan({pkg("B", "3")});
    auto downgrade = plan({pkg("B", "1")});
    (void)installed::prepare_install(base, valid);
    rejects([&] { installed::prepare_install(base, downgrade); }, "B >= 2");
    auto conflict = pkg("blocked"); conflict.conflicts = {"A"};
    rejects([&] { installed::prepare_install(base, plan({conflict})); }, "conflict");
    auto negative = state(plan({pkg("A", "1", {"NOT blocked"})}));
    rejects([&] { installed::prepare_install(negative, plan({pkg("blocked")})); });

    auto provider = pkg("provider", "2"); provider.capabilities = {"api"};
    auto cap = state(plan({pkg("consumer", "1", {"capability(api) >= 2"}), provider, pkg("U")}));
    const auto& bindings = member(cap, "consumer").semantics->bindings;
    require(bindings.size() == 1 && bindings[0].provider.package == PackageId("provider") &&
            bindings[0].selector.text.find("capability(api)") != std::string::npos, "selected provider persisted");
    rejects([&] { remove(cap, {"provider"}); });
    auto changed = provider; changed.version = PackageVersion("3");
    (void)installed::prepare_install(cap, plan({changed}));
    changed.capabilities.clear();
    rejects([&] { installed::prepare_install(cap, plan({changed})); });
    auto alternate = provider; alternate.package_id = PackageId("alternate");
    cap.realizations.push_back(state(plan({alternate})).realizations.front());
    require(remove(cap, {"provider"}).realizations.size() == 3, "remaining actual capability provider satisfies requirement");
    alternate.version = PackageVersion("1");
    member(cap, "alternate") = state(plan({alternate})).realizations.front();
    rejects([&] { remove(cap, {"provider"}); });

    auto feature = pkg("gui");
    feature.feature_dependencies["docs"] = {{{"B >= 2"}}};
    auto configured = state(plan({feature, pkg("B", "2"), pkg("U")}, {{"docs", true}}));
    rejects([&] { remove(configured, {"B"}); });
    require(remove(configured, {"U"}).realizations.size() == 2, "feature configuration survives unrelated removal");
    auto disabled = state(plan({feature, pkg("B", "2")}, {{"docs", false}}));
    require(remove(disabled, {"B"}).realizations.size() == 1, "disabled feature does not require dependency");
    auto conditional = state(plan({pkg("A", "1", {"when(platform(linux), B)"}), pkg("B")}));
    rejects([&] { remove(conditional, {"B"}); });
    auto inactive = state(plan({pkg("A", "1", {"when(platform(other), B)"}), pkg("B")}));
    require(remove(inactive, {"B"}).realizations.size() == 1, "inactive conditional body");
    auto guard_feature = pkg("A", "1", {"when(feature(docs), B)"});
    guard_feature.feature_dependencies["docs"] = {};
    auto guard = state(plan({guard_feature, pkg("B")}, {{"docs", true}}));
    rejects([&] { remove(guard, {"B"}); });
    auto scoped = state(plan({pkg("A", "1", {"runtime(host): B"}), pkg("B")}));
    rejects([&] { remove(scoped, {"B"}); });
    auto wrong_host = scoped;
    member(wrong_host, "B").semantics->target.architecture = "aarch64";
    rejects([&] { installed::validate_environment(wrong_host); }, "target environments");
    auto nested = state(plan({pkg("A", "1", {"build(target): (runtime(target):(B))"}), pkg("B")}));
    rejects([&] { remove(nested, {"B"}); });
    auto build = state(plan({pkg("A", "1", {"build(build): B"}), pkg("B")}, {}, {}, {DependencyRole::Build}));
    rejects([&] { remove(build, {"B"}); });
    auto build_inactive = state(plan({pkg("A", "1", {"build(build): B"}), pkg("B")}));
    require(remove(build_inactive, {"B"}).realizations.size() == 1, "inactive build scope preserved but gated");
    auto not_inactive = state(plan({pkg("A", "1", {"NOT (build(build):(B))"})}));
    installed::validate_environment(not_inactive);
    auto offers = pkg("feature-provider"); offers.features = {"graphics"};
    auto feature_dep = state(plan({pkg("A", "1", {"feature(graphics)"}), offers}));
    rejects([&] { remove(feature_dep, {"feature-provider"}); });

    auto policy = SystemPolicyRequest{SystemPolicyId("base"), {{{SelectorKind::Capability, "api", {}}}}};
    auto policy_state = state(plan({provider, pkg("U")}, {}, {policy}));
    rejects([&] { remove(policy_state, {"provider"}); }, "system policy");
    rejects([&] { remove(policy_state, {"provider", "U"}); }, "system policy");
    require(remove(policy_state, {"U"}).policies == policy_state.policies, "policy independent of memberships");
    rejects([&] { installed::prepare_install(policy_state, plan({pkg("provider", "3")})); }, "system policy");

    // Unsupported AST and invalid enums reject before evaluation, including
    // disabled/OR-short-circuited/NOT branches.
    struct Unknown : dependency::Expression { Unknown() : Expression(static_cast<Kind>(99)) {} };
    auto unknown = std::make_shared<Unknown>();
    rejects([&] { installed::validate_expression(std::make_shared<dependency::NotExpression>(unknown)); }, "Unsupported");
    rejects([&] { installed::validate_expression(std::make_shared<dependency::OrExpression>(
        std::vector<dependency::ExpressionPtr>{std::make_shared<dependency::TrueExpression>(), unknown})); });
    auto invalid_scope = std::make_shared<dependency::ScopedExpression>(static_cast<dependency::DependencyKind>(99),
        dependency::DependencyContext::Target, std::make_shared<dependency::TrueExpression>());
    rejects([&] { installed::validate_expression(std::make_shared<dependency::NotExpression>(invalid_scope)); });
    auto unsupported_guard = base;
    member(unsupported_guard, "A").semantics->package.dependencies = {"NOT when(B, true)"};
    rejects([&] { installed::validate_environment(unsupported_guard); }, "Unsupported");
    member(unsupported_guard, "A").semantics->package.dependencies = {"true OR when(B, true)"};
    rejects([&] { installed::validate_environment(unsupported_guard); }, "Unsupported");
    auto invalid_feature = base;
    member(invalid_feature, "A").semantics->feature_states["undeclared"] = true;
    rejects([&] { installed::validate_environment(invalid_feature); }, "Undeclared");

    // Full effective role and environment state survives codecs.
    auto role_state = state(plan({pkg("A", "1", {"test(build): B", "development(host): B",
        "optional(target): B", "propagated(target): B"}), pkg("B")}, {}, {},
        {DependencyRole::Test, DependencyRole::Development, DependencyRole::Optional, DependencyRole::Propagated}));
    rejects([&] { remove(role_state, {"B"}); });
    auto role_loaded = generation::deserialize_generation(generation::serialize(role_state));
    require(role_loaded.realizations == role_state.realizations, "effective roles/env roundtrip");
    auto disabled_optional = state(plan({pkg("A", "1", {"optional-disabled(target): B"}), pkg("B")}, {}, {},
        {DependencyRole::Optional}));
    require(remove(disabled_optional, {"B"}).realizations.size() == 1, "disabled scope is sticky");
    auto host_predicate = state(plan({pkg("A", "1", {"runtime(host): platform(linux) AND abi(glibc)"})}));
    installed::validate_environment(host_predicate);
    member(host_predicate, "A").semantics->environments.host.platform.clear();
    rejects([&] { installed::validate_environment(host_predicate); }, "state is unavailable");
    auto conflict_conditional = pkg("conditional-conflict");
    conflict_conditional.conflicts = {"when(platform(other), B)"};
    auto harmless = state(plan({conflict_conditional, pkg("B")}));
    installed::validate_environment(harmless);
    member(harmless, "conditional-conflict").semantics->package.conflicts = {"when(platform(linux), B)"};
    rejects([&] { installed::validate_environment(harmless); }, "conflict");
    auto scoped_conflict = state(plan({pkg("A"), pkg("B")}, {}, {}, {DependencyRole::Build}));
    member(scoped_conflict, "A").semantics->package.conflicts = {"B"};
    rejects([&] { installed::validate_environment(scoped_conflict); }, "conflict");
    auto or_branch = state(plan({pkg("A", "1", {"B OR C"}), pkg("B"), pkg("C")}));
    require(remove(or_branch, {"B"}).realizations.size() == 2, "alternative branch remains valid");
    rejects([&] { remove(or_branch, {"B", "C"}); });
    auto arch_state = base;
    member(arch_state, "A").semantics->package.dependencies = {"B >= 1:2.0-2"};
    member(arch_state, "B").candidate.version = PackageVersion("1:2.0-3");
    member(arch_state, "B").semantics->package.version = PackageVersion("1:2.0-3");
    member(arch_state, "B").semantics->version_scheme = VersionScheme::Arch;
    installed::validate_environment(arch_state);
    member(arch_state, "B").candidate.version = PackageVersion("1:2.0-1");
    member(arch_state, "B").semantics->package.version = PackageVersion("1:2.0-1");
    rejects([&] { installed::validate_environment(arch_state); });

    // Legacy capture is read-only and explicit. Subsequent validation needs
    // neither the old store's metadata file nor a repository directory.
    const auto legacy = fixture.root / "legacy";
    store::write_metadata(legacy, {"legacy", "1", "", "", "", {"B >= 2"}});
    auto mixed = base; mixed.entries.push_back({"legacy", legacy.native()});
    installed::capture_legacy_semantics(mixed); installed::validate_environment(mixed);
    rejects([&] { remove(mixed, {"B"}); });
    const auto uncertain = fixture.root / "uncertain";
    fs::create_directories(uncertain / ".athena");
    std::ofstream(uncertain / ".athena/metadata.toml") << "name='old'\nversion='1'\n";
    generation::Generation legacy_unknown{0, {{"old", uncertain.native()}}};
    rejects([&] { installed::capture_legacy_semantics(legacy_unknown); }, "dependency state is unavailable");
    store::write_metadata(uncertain, {"old", "1", "", "", "", {"NOT feature(unknown)"}});
    installed::capture_legacy_semantics(legacy_unknown);
    rejects([&] { installed::validate_environment(legacy_unknown); }, "Unsupported");
    store::Metadata legacy_provider{"old", "7", "", "", "", {}};
    legacy_provider.provides.capabilities = {"api"};
    store::write_metadata(uncertain, legacy_provider);
    auto unknown_version = state(plan({pkg("A")}));
    unknown_version.entries = {{"old", uncertain.native()}};
    installed::capture_legacy_semantics(unknown_version);
    member(unknown_version, "A").semantics->package.dependencies = {"NOT capability(api) >= 2"};
    rejects([&] { installed::validate_environment(unknown_version); }, "version state is unavailable");
    // Legacy capture also rejects malformed rich metadata instead of discarding it.
    std::ofstream(uncertain / ".athena/metadata.toml", std::ios::app) << "\ncapability_versions = 'unknown'\n";
    generation::Generation malformed_legacy{0, {{"old", uncertain.native()}}};
    rejects([&] { installed::capture_legacy_semantics(malformed_legacy); }, "Unsupported legacy provider");

    auto modern_doc = generation::serialize(mixed);
    auto loaded = generation::deserialize_generation(modern_doc);
    require(loaded.entries == mixed.entries && loaded.realizations == mixed.realizations, "generation semantic roundtrip");
    fs::remove_all(legacy);
    installed::validate_environment(loaded); // No legacy store/repository I/O.
    auto unknown_old = loaded; unknown_old.realizations[0].semantics.reset();
    auto old_doc = generation::serialize(unknown_old);
    require(!generation::deserialize_generation(old_doc).realizations[0].semantics, "old record not fabricated");
    rejects([&] { installed::validate_environment(unknown_old); }, "dependency state is unavailable");
    auto raw_legacy = generation::deserialize_generation("id=0\nentries=[]\n");
    installed::validate_environment(raw_legacy);
    auto schema2 = generation::deserialize_generation("schema_version=2\nid=0\nmemberships=[]\n");
    installed::validate_environment(schema2);
    for (const auto& doc : {"schema_version=4\nid=0\nmemberships=[]\npolicies=[]",
        "schema_version=3.0\nid=0\nmemberships=[]\npolicies=[]", "schema_version=3\nid=0\nmemberships=[]"})
        rejects([&] { generation::deserialize_generation(doc); });
    auto record_doc = realization::serialize(base.realizations.front());
    auto record_table = toml::parse(record_doc);
    record_table.erase("semantics");
    std::ostringstream malformed; malformed << record_table;
    rejects([&] { realization::deserialize_record(malformed.str()); });
    auto semantic_doc = installed::serialize(*base.realizations.front().semantics);
    rejects([&] { installed::deserialize_semantics("unexpected=true\n" + semantic_doc); });
    auto semantic_table = toml::parse(semantic_doc); semantic_table.insert_or_assign("active_roles", toml::array{99});
    std::ostringstream bad_roles; bad_roles << semantic_table;
    rejects([&] { installed::deserialize_semantics(bad_roles.str()); });
    auto policy_doc = installed::serialize(policy_state.policies.front());
    require(installed::deserialize_policy(policy_doc) == policy_state.policies.front(), "policy roundtrip");

    // Historical generations remain immutable/readable and serve offline validation.
    const auto paths = PathLayout::from_root(fixture.root / "athena");
    fixture_fs::create_directories(paths.store);
    generation::GenerationManager manager(paths.generation_state, generation::GenerationManager::GenerationDirectoryTag{});
    manager.initialize();
    auto historical = manager.create_unprotected_for_test(base.entries, base.realizations);
    std::ofstream(paths.generation_state / "current") << historical.id << '\n';
    installed::validate_environment(manager.get(historical.id));
    const auto current_before = read_bytes(paths.generation_state / "current");
    const auto states_before = snapshot(paths.generation_state);
    std::ofstream(paths.store / "sentinel") << "unchanged";
    const auto objects_before = snapshot(paths.store);
    fs::create_directories(paths.target_root); std::ofstream(paths.target_root / "sentinel") << "unchanged";
    const auto target_before = snapshot(paths.target_root);
    store::ObjectStore objects(paths.store);
    rejects([&] { install::install_plan(downgrade, manager, objects, {fixture.root / "missing"}, paths.target_root); }, "B >= 2");
    rejects([&] { install::remove_packages({"B"}, paths.target_root, paths); }, "B >= 2");
    require(read_bytes(paths.generation_state / "current") == current_before, "failed validation changed current");
    unchanged(paths.generation_state, states_before); unchanged(paths.store, objects_before); unchanged(paths.target_root, target_before);
    fixture_fs::create_directories(objects.object_path(base.realizations.front().artifact_id));
    const ArtifactId orphan("sha256:" + std::string(64, 'b'));
    fixture_fs::create_directories(objects.object_path(orphan));
    const auto collected = store::GarbageCollector(paths.store, paths.generation_state.parent_path()).collect();
    require(collected == std::vector<fs::path>{objects.object_path(orphan)} &&
            fs::exists(objects.object_path(base.realizations.front().artifact_id)), "GC retains semantic records by ArtifactId");

    // The local manifest bridge preserves outer feature dependency scopes.
    const auto bridge_paths = PathLayout::from_root(fixture.root / "bridge");
    cli_test::manifest(bridge_paths, "app", fixture.root / "missing");
    { std::ofstream out(bridge_paths.repository_state / "packages/app/package.toml", std::ios::app);
      out << "[feature_dependencies]\nextras = ['development(host): B']\n"; }
    LocalRepository bridge({RepositoryId("bridge"), bridge_paths.repository_state / "packages"});
    const auto dependency = bridge.load_snapshot().candidates().front().package.feature_dependencies.at("extras").front();
    require(dependency.role == DependencyRole::Development && dependency.context == DependencyTargetContext::Host &&
        dependency.expression.text == "B", "manifest feature scope lost");

    // CLI preflight is before creation of store/build/generation state.
    const auto cli_paths = PathLayout::from_root(fixture.root / "cli");
    cli_test::manifest(cli_paths, "app", fixture.root / "missing", "[\"NOT when(app, true)\"]");
    const auto cli_result = cli_test::invoke(cli_paths, {"app"});
    require(cli_result.status != 0, "CLI accepted unsupported installed semantics");
    cli_test::no_runtime(cli_paths);
    std::cout << "installed semantic state, offline transitions and failure boundaries passed\n";
}
