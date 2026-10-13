// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation/operational_plan.hpp"
#include "activation/plan_retention.hpp"
#include "activation_schema_support.hpp"
#include <algorithm>
#include <random>

using namespace athena::activation;
using namespace d1_test;
namespace {
OperationalSourceProjection tree(std::string locator, std::vector<std::string> links = {},
                                 std::vector<std::string> directories = {}) {
    return {LegacyTreeSource{std::move(locator), LegacySealDigest(digest('a'))},
            std::move(links), std::move(directories)};
}
OperationalGeneration generation(std::uint64_t number,
                                 std::vector<OperationalSourceProjection> sources = {}) {
    using namespace athena::core;
    athena::generation::Generation g{number, {}};
    unsigned n = 0;
    for (const auto& inventory : sources) {
        if (auto object = std::get_if<ArtifactSource>(&inventory.source)) {
            RepositorySnapshotRef snapshot{RepositoryId("repo"), RepositoryRevision("rev"),
                SchemaVersion(1), ContentDigest(digest('a')), SnapshotId(digest('b'))};
            g.realizations.push_back({{PackageId("pkg" + std::to_string(n++)), PackageVersion("1"),
                snapshot.repository_id, snapshot, RepositoryProvenance{snapshot.repository_id,
                    snapshot.revision, MetadataReference("recipe"), snapshot}},
                object->artifact_id, {}, ArtifactOrigin::AmbientSourceBuild});
        } else {
            auto locator = std::visit([](const auto& s) -> std::string {
                if constexpr (requires { s.locator; }) return s.locator;
                else return {};
            }, inventory.source);
            g.entries.push_back({"pkg" + std::to_string(n++), locator, {}});
        }
    }
    auto bytes = athena::generation::serialize(g);
    auto doc = athena::generation::validate_generation_document(bytes, number);
    return {number, doc.digest, std::move(bytes), std::move(sources)};
}
OperationalPlanInput request(OperationalGeneration base, OperationalGeneration target) {
    auto selected = SelectedBase{CurrentV1{base.id, old_id, PlanDigest(digest('c'))},
                                 base.digest, ActivationOwnership{old_id}};
    std::vector<OperationalWitness> witnesses;
    std::vector<std::string> links;
    for (const auto& s : base.sources) links.insert(links.end(), s.links.begin(), s.links.end());
    std::sort(links.begin(), links.end());
    for (const auto& p : links) witnesses.push_back({p, old(witnesses.size())});
    return {id, {projection_id, "/nonexistent/state/generations", "/store", "/nonexistent/target"},
            selected, std::move(base), std::move(target), std::move(witnesses)};
}
void roundtrip(const OperationalPlanResult& result) {
    require(parse_activation_intent(serialize(result.intent)) == result.intent, "intent roundtrip");
    require(parse_activation_plan(serialize(result.plan)) == result.plan, "plan roundtrip");
    require(digest_plan_documents(serialize(result.intent), serialize(result.plan)) == result.digest,
            "combined canonical digest");
    for (const auto& op : result.plan.operations) std::visit([&](const auto& state) {
        if constexpr (requires { state.new_witness; })
            require(state.new_witness == WitnessReference{id, op.id}, "successor witness follows canonical ID");
    }, op.state);
}
template<class T> std::size_t count(const ActivationPlan& plan) {
    return std::count_if(plan.operations.begin(), plan.operations.end(),
                         [](const auto& op) { return std::holds_alternative<T>(op.state); });
}
void transitions() {
    auto a = tree("/store/a", {"bin/tool"});
    auto b = tree("/store/b", {"bin/tool"});
    auto initial = request(generation(0), generation(1, {a}));
    auto created = build_operational_plan(initial); roundtrip(created);
    require(count<CreateLink>(created.plan) == 1 && count<EnsureDirectory>(created.plan) == 1,
            "initial creation derives implicit directory");
    require(created.plan.operations[0].path == "bin", "directories before creates");
    auto kept = build_operational_plan(request(generation(1, {a}), generation(2, {a})));
    roundtrip(kept);
    require(count<KeepLink>(kept.plan) == 1 && kept.plan.sources.size() == 1, "KEEP uses shared source");
    require(std::get<KeepLink>(kept.plan.operations.back().state).old_witness == old(0), "preserve old witness ID");
    auto replaced = build_operational_plan(request(generation(1, {a}), generation(2, {b})));
    roundtrip(replaced);
    require(count<ReplaceLink>(replaced.plan) == 1, "changed source requires REPLACE");
    auto removed = build_operational_plan(request(generation(1, {a}), generation(2)));
    roundtrip(removed);
    require(count<RemoveLink>(removed.plan) == 1 && count<EnsureDirectory>(removed.plan) == 0,
            "removal does not authorize directory deletion");

    OperationalSourceProjection directories{ArtifactSource{athena::core::ArtifactId(digest('e'))},
                                            {}, {"bin", "empty/deep"}};
    auto shared = build_operational_plan(request(generation(0), generation(1, {a, directories})));
    roundtrip(shared);
    require(count<EnsureDirectory>(shared.plan) == 3 && shared.plan.sources.size() == 2,
            "shared directories and directory-only membership");
    auto zero = tree("/store/zero");
    auto no_links = build_operational_plan(request(generation(1, {zero}), generation(2, {zero})));
    require(no_links.plan.operations.empty() && no_links.plan.sources.size() == 1, "zero-link source retained");
    roundtrip(no_links);

    auto leaf = tree("/store/leaf", {"data"});
    auto nested = tree("/store/nested", {"data/sub/tool"});
    auto converted = build_operational_plan(request(generation(1, {leaf}), generation(2, {nested})));
    roundtrip(converted);
    require(std::holds_alternative<RemoveLink>(converted.plan.operations[0].state) &&
            converted.plan.operations[1].path == "data" &&
            std::holds_alternative<EnsureDirectory>(converted.plan.operations[1].state),
            "link-to-directory REMOVE precedes ENSURE");
    rejects([&] { build_operational_plan(request(generation(1, {nested}), generation(2, {leaf}))); },
            SchemaErrorCode::Conflict);

    auto fresh = request(generation(0), generation(0));
    fresh.base = AbsentBase{}; fresh.base_generation.reset();
    auto bootstrap_claim = build_operational_plan(fresh); roundtrip(bootstrap_claim);
    require(bootstrap_claim.plan.operations.empty(), "empty absent-base construction without bootstrap effects");
    fresh.target = generation(1, {a});
    rejects([&] { build_operational_plan(fresh); }, SchemaErrorCode::Reference);
    auto historical = initial;
    historical.base = SelectedBase{HistoricalCurrent{0, "0\n"}, historical.base_generation->digest, EmptyOwnership{}};
    roundtrip(build_operational_plan(historical));
    historical = request(generation(1, {a}), generation(2, {a}));
    historical.base = SelectedBase{HistoricalCurrent{1, "1\n"}, historical.base_generation->digest,
                                  AdoptionOwnership{adoption_id}};
    historical.old_witnesses[0].witness = {adoption_id, 17};
    roundtrip(build_operational_plan(historical));
}
void determinism() {
    auto a = tree("/store/a", {"z/old", "a/deep/old", "keep"}, {"z"});
    auto b = tree("/store/b", {"new", "keep", "x/y/new"}, {"x"});
    auto zero = tree("/store/zero");
    auto input = request(generation(1, {a, zero}), generation(2, {b, zero}));
    auto expected = build_operational_plan(input); roundtrip(expected);
    require(expected.plan.operations[0].path == "a/deep/old" &&
            expected.plan.operations[1].path == "z/old", "REMOVE deepest first");
    std::mt19937 random(42);
    for (unsigned n = 0; n < 40; ++n) {
        auto shuffle = [&](auto& g) {
            std::shuffle(g.sources.begin(), g.sources.end(), random);
            for (auto& s : g.sources) {
                std::shuffle(s.links.begin(), s.links.end(), random);
                std::shuffle(s.directories.begin(), s.directories.end(), random);
            }
        };
        shuffle(*input.base_generation); shuffle(input.target);
        std::shuffle(input.old_witnesses.begin(), input.old_witnesses.end(), random);
        auto result = build_operational_plan(input);
        require(result.intent == expected.intent && result.plan == expected.plan && result.digest == expected.digest,
                "all input permutations produce identical structures and digest");
        require(serialize(result.plan) == serialize(expected.plan), "identical serialized output");
    }
    auto high = tree("/store/high", {std::string(1, char(0xff)), std::string(1, char(0x80)), "z"});
    auto result = build_operational_plan(request(generation(0), generation(1, {high})));
    require(result.plan.operations[0].path == "z" &&
            result.plan.operations[1].path == std::string(1, char(0x80)), "unsigned raw path byte ordering");
    roundtrip(result);
}
void invalid() {
    auto a = tree("/store/a", {"bin/tool", "bin/second"});
    auto valid = request(generation(1, {a}), generation(2, {a}));
    auto bad = [&](auto mutate, SchemaErrorCode code) {
        auto input = valid; mutate(input);
        rejects([&] { build_operational_plan(input); }, code);
    };
    bad([](auto& i) { i.target.sources.push_back(i.target.sources[0]); }, SchemaErrorCode::Conflict);
    bad([](auto& i) { i.target.sources[0].links.push_back("bin/tool"); }, SchemaErrorCode::Conflict);
    bad([](auto& i) { i.target.sources[0].directories = {"extra", "extra"}; }, SchemaErrorCode::Conflict);
    bad([](auto& i) { i.target.sources.clear(); }, SchemaErrorCode::Reference);
    bad([](auto& i) { i.target.sources[0].links.pop_back(); }, SchemaErrorCode::Conflict);
    bad([](auto& i) { i.target.sources[0].directories = {"bin/tool"}; }, SchemaErrorCode::Conflict);
    bad([](auto& i) { i.target.sources[0].links.push_back("bin/tool/child"); }, SchemaErrorCode::Conflict);
    bad([](auto& i) { i.target.sources[0].links = {".athena-activation/foreign"}; }, SchemaErrorCode::Conflict);
    bad([](auto& i) { i.target.sources[0].links = {"../escape"}; }, SchemaErrorCode::Path);
    bad([](auto& i) { i.old_witnesses.pop_back(); }, SchemaErrorCode::Reference);
    bad([](auto& i) { i.old_witnesses.push_back(i.old_witnesses[0]); }, SchemaErrorCode::Conflict);
    bad([](auto& i) { i.old_witnesses[1].witness = i.old_witnesses[0].witness; }, SchemaErrorCode::Conflict);
    bad([](auto& i) { i.old_witnesses[0].path = "foreign"; }, SchemaErrorCode::Reference);
    bad([](auto& i) { i.old_witnesses[0].witness = fresh(0); }, SchemaErrorCode::Reference);
    bad([](auto& i) { i.old_witnesses[0].witness = {adoption_id, 0}; }, SchemaErrorCode::Reference);
    bad([](auto& i) { i.old_witnesses[0].witness.entry_id = UINT64_MAX; }, SchemaErrorCode::Bounds);
    bad([](auto& i) { i.base_generation.reset(); }, SchemaErrorCode::Reference);
    bad([](auto& i) { i.base = AbsentBase{}; }, SchemaErrorCode::Reference);
    bad([](auto& i) { i.target.digest = athena::generation::GenerationDocumentDigest(digest('f')); }, SchemaErrorCode::Reference);
    bad([](auto& i) { i.target.id = 19; }, SchemaErrorCode::Reference);
    bad([](auto& i) { i.base_generation->digest = i.target.digest; }, SchemaErrorCode::Reference);
    bad([](auto& i) { i.activation_id = old_id; }, SchemaErrorCode::Reference);
    bad([](auto& i) { std::get<LegacyTreeSource>(i.target.sources[0].source).seal_digest = LegacySealDigest(digest('f')); },
        SchemaErrorCode::Conflict);
    bad([](auto& i) { i.target.sources[0].links.resize(max_retention_references + 1, "leaf"); }, SchemaErrorCode::Bounds);
    bad([](auto& i) { i.old_witnesses.resize(max_operations + 1, i.old_witnesses[0]); }, SchemaErrorCode::Bounds);

    auto other = tree("/store/other", {"bin/tool"});
    rejects([&] { build_operational_plan(request(generation(0), generation(1, {a, other}))); }, SchemaErrorCode::Conflict);
    auto zero = tree("/store/zero");
    auto missing = request(generation(0), generation(1, {zero})); missing.target.sources.clear();
    rejects([&] { build_operational_plan(missing); }, SchemaErrorCode::Reference);
    auto extraneous = request(generation(0), generation(1)); extraneous.target.sources.push_back(zero);
    rejects([&] { build_operational_plan(extraneous); }, SchemaErrorCode::Reference);
    auto legacy_empty_directory = request(generation(0), generation(1, {tree("/store/legacy", {}, {"empty"})}));
    rejects([&] { build_operational_plan(legacy_empty_directory); }, SchemaErrorCode::Conflict);
    auto outside = request(generation(0), generation(1, {tree("/different/store/member")}));
    rejects([&] { build_operational_plan(outside); }, SchemaErrorCode::Reference);
    auto object_namespace = request(generation(0), generation(1, {tree("/store/objects/member")}));
    rejects([&] { build_operational_plan(object_namespace); }, SchemaErrorCode::Conflict);
    auto nonempty_zero = request(generation(0), generation(0, {zero}));
    rejects([&] { build_operational_plan(nonempty_zero); }, SchemaErrorCode::Reference);
    auto empty_ownership = valid;
    empty_ownership.base = SelectedBase{HistoricalCurrent{1, "1"}, empty_ownership.base_generation->digest,
                                       EmptyOwnership{}};
    rejects([&] { build_operational_plan(empty_ownership); }, SchemaErrorCode::Reference);
    auto reused_generation = valid; reused_generation.target.id = reused_generation.base_generation->id;
    rejects([&] { build_operational_plan(reused_generation); }, SchemaErrorCode::Reference);
    auto ambiguous = generation(1, {a});
    auto g = athena::generation::deserialize_generation(ambiguous.document);
    g.entries.push_back({"different-package", "/store/a", {}});
    ambiguous.document = athena::generation::serialize(g);
    ambiguous.digest = athena::generation::validate_generation_document(ambiguous.document, 1).digest;
    rejects([&] { build_operational_plan(request(generation(0), ambiguous)); }, SchemaErrorCode::Conflict);
    std::vector<std::string> large;
    for (std::size_t n = 0; n <= max_operations / 2; ++n)
        large.push_back("parent" + std::to_string(n) + "/leaf");
    auto derived_bound = request(generation(0), generation(1, {tree("/store/large", large)}));
    rejects([&] { build_operational_plan(derived_bound); }, SchemaErrorCode::Bounds);
}
void artifacts_and_aliases() {
    OperationalSourceProjection artifact{ArtifactSource{athena::core::ArtifactId(digest('d'))}, {"bin/object"}, {}};
    auto result = build_operational_plan(request(generation(0), generation(1, {artifact})));
    require(std::holds_alternative<ArtifactSource>(result.plan.sources[0]), "artifact membership"); roundtrip(result);
    auto missing = request(generation(0), generation(1, {artifact})); missing.target.sources.clear();
    rejects([&] { build_operational_plan(missing); }, SchemaErrorCode::Reference);
    auto duplicate = generation(1, {artifact});
    auto g = athena::generation::deserialize_generation(duplicate.document);
    auto record = g.realizations[0]; record.candidate.package = athena::core::PackageId("other");
    g.realizations.push_back(record);
    duplicate.document = athena::generation::serialize(g);
    duplicate.digest = athena::generation::validate_generation_document(duplicate.document, 1).digest;
    require(build_operational_plan(request(generation(0), duplicate)).plan.sources.size() == 1,
            "multiple records of same artifact project once");
    OperationalSourceProjection alias{LegacyAliasSource{"/store/alias", "objects/v1/sha256/target",
        athena::core::ArtifactId(digest('d')), LegacySealDigest(digest('a'))}, {"bin/alias"}, {}};
    roundtrip(build_operational_plan(request(generation(1, {alias}), generation(2, {alias}))));
    auto conflicting = request(generation(1, {alias}), generation(2, {tree("/store/alias", {"bin/alias"})}));
    rejects([&] { build_operational_plan(conflicting); }, SchemaErrorCode::Conflict);
}
}
int main() {
    return d1_test::run([] { transitions(); determinism(); invalid(); artifacts_and_aliases();
        std::cout << "PASS operational plan: transitions, membership, witnesses, canonical ordering, invalid inputs\n"; });
}
