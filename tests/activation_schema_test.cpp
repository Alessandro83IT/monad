// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation_schema_support.hpp"
#include <algorithm>
#include <charconv>
using namespace d1_test;
using athena::generation::GenerationDocumentDigest;

namespace {
void renumber(ActivationPlan& value) {
    for (std::size_t i = 0; i < value.operations.size(); ++i) {
        value.operations[i].id = i;
        std::visit([&](auto& state) {
            if constexpr (requires { state.new_witness; }) state.new_witness = fresh(i);
        }, value.operations[i].state);
    }
}
void roundtrip(const ActivationPlan& value) {
    const auto bytes = serialize(value);
    require(parse_activation_plan(bytes, value.activation_id) == value, "Plan roundtrip changed values");
    require(serialize(parse_activation_plan(bytes)) == bytes, "Plan canonical bytes changed");
}
void intent_tests() {
    auto value = intent();
    for (const ActivationBase& base : std::vector<ActivationBase>{AbsentBase{},
        SelectedBase{CurrentV1{41, old_id, PlanDigest(digest('c'))}, GenerationDocumentDigest(digest('d')), ActivationOwnership{old_id}},
        SelectedBase{HistoricalCurrent{41, "41"}, GenerationDocumentDigest(digest('d')), EmptyOwnership{}},
        SelectedBase{HistoricalCurrent{41, "41\n"}, GenerationDocumentDigest(digest('d')), AdoptionOwnership{adoption_id}}}) {
        value.base = base;
        const auto bytes = serialize(value);
        require(parse_activation_intent(bytes, id) == value, "Intent/base roundtrip");
        require(serialize(parse_activation_intent(bytes)) == bytes, "Intent bytes changed");
    }
    const auto decimal = serialize(value);
    rejects([&] { (void)parse_activation_intent(replace(decimal, "generation = 41", "generation = 40")); }, SchemaErrorCode::Reference);
    rejects([&] { (void)parse_activation_intent(replace(decimal, "format = \"decimal\"", "format = \"v1\"")); }, SchemaErrorCode::Reference);
    rejects([&] { (void)parse_activation_intent(replace(decimal, "record_hex = \"34310a\"", "record_hex = \"303431\"")); }, SchemaErrorCode::Schema);
    rejects([&] { (void)parse_activation_intent(replace(decimal, "adoption:" + adoption_id.value(), "activation:" + old_id.value())); }, SchemaErrorCode::Reference);
    value.base = SelectedBase{CurrentV1{41, old_id, PlanDigest(digest('c'))}, GenerationDocumentDigest(digest('d')), ActivationOwnership{old_id}};
    const auto v1 = serialize(value);
    rejects([&] { (void)parse_activation_intent(replace(v1, digest('d'), digest('D'))); }, SchemaErrorCode::Identity);
    rejects([&] { (void)parse_activation_intent(replace(v1, "activation:" + old_id.value(), "activation:" + id.value())); }, SchemaErrorCode::Reference);
    rejects([&] { (void)parse_activation_intent(replace(v1, "activation:" + old_id.value(), "empty")); }, SchemaErrorCode::Reference);
    rejects([&] { (void)parse_activation_intent(replace(v1, "format = \"v1\"", "format = \"decimal\"")); }, SchemaErrorCode::Reference);
    const auto record = serialize(std::get<SelectedBase>(value.base).current);
    rejects([&] { (void)parse_activation_intent(replace(v1, encode_hex(record), encode_hex(record + "\n"))); }, SchemaErrorCode::Noncanonical);
    rejects([&] { (void)parse_activation_intent(replace(v1, encode_hex(record), std::string((max_current_size + 1) * 2, '0'))); }, SchemaErrorCode::Bounds);
    value.base = AbsentBase{};
    const auto bytes = serialize(value);
    for (const auto& node : {"1.0", "\"1\"", "true", "[1]", "{x = 1}"})
        rejects([&] { (void)parse_activation_intent(replace(bytes, "target_generation = 42", std::string("target_generation = ") + node)); }, SchemaErrorCode::Schema);
    rejects([&] { (void)parse_activation_intent(bytes, old_id); }, SchemaErrorCode::Reference);
    rejects([&] { (void)parse_activation_intent(bytes + "generation = 0\n"); }, SchemaErrorCode::Schema);
    rejects([&] { (void)parse_activation_intent(replace(bytes, "[base]\nformat = \"absent\"", "base = 1")); }, SchemaErrorCode::Schema);
    rejects([&] { (void)parse_activation_intent(replace(bytes, "format = \"absent\"", "format = \"unknown\"")); }, SchemaErrorCode::Schema);
    rejects([&] { (void)parse_activation_intent(replace(bytes, "target_generation = 42", "target_generation = -1")); }, SchemaErrorCode::Schema);
    for (const auto& field : {"activation_id", "projection_id", "target_generation_digest"}) {
        const auto original = std::string(field) == "activation_id" ? id.value() : std::string(field) == "projection_id" ? projection_id.value() : digest('b');
        rejects([&] { (void)parse_activation_intent(replace(bytes, original, "bad")); }, SchemaErrorCode::Identity);
    }
    for (const auto& locator : {"relative", "/a/", "//a", "/a//b", "/a/./b", "/a/../b"})
        rejects([&] { (void)parse_activation_intent(replace(bytes, encode_hex("/store"), encode_hex(locator))); }, SchemaErrorCode::Path);
    for (const auto& hex : {"2F", "2", "gg", "2f00"})
        rejects([&] { (void)parse_activation_intent(replace(bytes, encode_hex("/store"), hex)); }, std::string(hex) == "2f00" ? SchemaErrorCode::Path : SchemaErrorCode::Schema);
    for (const auto& field : {"generation_authority_hex", "store_authority_hex", "projection_root_hex"}) {
        const auto locator = std::string(field) == "generation_authority_hex" ? "/state/generations" : std::string(field) == "store_authority_hex" ? "/store" : "/target";
        rejects([&] { (void)parse_activation_intent(replace(bytes, encode_hex(locator), encode_hex("relative"))); }, SchemaErrorCode::Path);
    }
    value.generation_authority = "/";
    value.store_authority = std::string("/raw-\xff space\t\n", 14);
    value.projection_root = "/unicode-\xc3\xa9";
    require(parse_activation_intent(serialize(value)) == value, "Arbitrary absolute bytes changed");
}
void plan_tests() {
    auto value = plan();
    roundtrip(value);
    roundtrip({id, {}, {}});
    roundtrip({id, value.sources, {}});
    roundtrip({id, {}, {{0, "empty", EnsureDirectory{}}}});
    value.sources.push_back(LegacyAliasSource{"/store/alias", "../objects/v1/sha256/x", athena::core::ArtifactId(digest('a')), LegacySealDigest(digest('b'))});
    value.sources.push_back(LegacyTreeSource{"/store/tree", LegacySealDigest(digest('b'))});
    // Includes all five operation variants and both old witness namespaces.
    value.operations = {{0, "old/deep", RemoveLink{0, old(3)}}, {1, "old", RemoveLink{0, {adoption_id, 4}}},
        {2, "bin", EnsureDirectory{}}, {3, "bin/deep", EnsureDirectory{}},
        {4, "bin/create", CreateLink{1, fresh(4)}}, {5, "bin/keep", KeepLink{0, old(5), 0, fresh(5)}},
        {6, "bin/replace", ReplaceLink{0, {adoption_id, 6}, 2, fresh(6)}}};
    roundtrip(value);
    const auto bytes = serialize(value);
    rejects([&] { (void)parse_activation_plan(bytes, old_id); }, SchemaErrorCode::Reference);
    for (const auto& node : {"0.0", "\"0\"", "true", "[0]", "{x = 0}"}) {
        rejects([&] { (void)parse_activation_plan(replace(bytes, "id = 0", std::string("id = ") + node)); }, SchemaErrorCode::Schema);
        rejects([&] { (void)parse_activation_plan(replace(bytes, "old_source = 0", std::string("old_source = ") + node)); }, SchemaErrorCode::Schema);
    }
    rejects([&] { (void)parse_activation_plan(replace(bytes, "sources", "unexpected")); }, SchemaErrorCode::Schema);
    rejects([&] { (void)parse_activation_plan(replace(bytes, "kind = \"artifact\"", "kind = \"other\"")); }, SchemaErrorCode::Schema);
    rejects([&] { (void)parse_activation_plan(replace(bytes, "kind = \"create\"", "kind = \"other\"")); }, SchemaErrorCode::Schema);
    rejects([&] { (void)parse_activation_plan(replace(bytes, "kind = \"artifact\"", "kind = \"artifact\"\nextra = 1")); }, SchemaErrorCode::Schema);
    rejects([&] { (void)parse_activation_plan(replace(bytes, "id = 0", "id = 0\nextra = 1")); }, SchemaErrorCode::Schema);
    rejects([&] { (void)parse_activation_plan(replace(bytes, digest('a'), "")); }, SchemaErrorCode::Identity);
    rejects([&] { (void)parse_activation_plan(replace(bytes, digest('b'), digest('B'))); }, SchemaErrorCode::Identity);
    rejects([&] { (void)parse_activation_plan(replace(bytes, "old_source = 0", "old_source = -1")); }, SchemaErrorCode::Schema);
    rejects([&] { (void)parse_activation_plan(replace(bytes, "new_source = 1", "new_source = 3")); }, SchemaErrorCode::Reference);
    rejects([&] { (void)parse_activation_plan(replace(bytes, "old_source = 0", "old_source = 3")); }, SchemaErrorCode::Reference);
    rejects([&] { (void)parse_activation_plan(replace(bytes, "id = 0", "id = 1")); }, SchemaErrorCode::Ordering);
    rejects([&] { (void)parse_activation_plan(replace(bytes, "activation:" + id.value() + ":4", "activation:" + old_id.value() + ":4")); }, SchemaErrorCode::Reference);
    rejects([&] { (void)parse_activation_plan(replace(bytes, "activation:" + id.value() + ":4", "activation:" + id.value() + ":5")); }, SchemaErrorCode::Reference);
    rejects([&] { (void)parse_activation_plan(replace(bytes, "activation:" + id.value() + ":4", "adoption:" + adoption_id.value() + ":4")); }, SchemaErrorCode::Reference);
    for (const auto& witness : {"activation:x:0", "unknown:0123456789abcdef0123456789abcdef:0", "activation:0123456789abcdef0123456789abcdef", "activation:0123456789ABCDEF0123456789abcdef:0"})
        rejects([&] { (void)parse_witness_reference(witness); }, SchemaErrorCode::Identity);
    for (const auto& entry : {"", "00", "01", "+1", "-1", "1x", "1\n"})
        rejects([&] { (void)parse_witness_reference("activation:" + id.value() + ":" + entry); }, SchemaErrorCode::Schema);
    rejects([&] { (void)parse_witness_reference("activation:" + id.value() + ":9223372036854775808"); }, SchemaErrorCode::Bounds);
    require(serialize(parse_witness_reference("adoption:" + adoption_id.value() + ":0")) == "adoption:" + adoption_id.value() + ":0", "Adoption witness roundtrip");
    auto bad = value;
    bad.sources.push_back(bad.sources.back());
    rejects([&] { (void)serialize(bad); }, SchemaErrorCode::Ordering);
    const auto single_source = serialize(ActivationPlan{id, {value.sources[0]}, {}});
    const auto duplicate_source = single_source + "\n[[sources]]\nkind = \"artifact\"\nartifact_id = \"" + digest('a') + "\"\n";
    rejects([&] { (void)parse_activation_plan(duplicate_source); }, SchemaErrorCode::Ordering);
    const auto inline_source = "schema_version = 1\nactivation_id = \"" + id.value() + "\"\nsources = [{ kind = \"artifact\", artifact_id = \"" + digest('a') + "\" }]\noperations = []\n";
    rejects([&] { (void)parse_activation_plan(inline_source); }, SchemaErrorCode::Noncanonical);
    rejects([&] { (void)parse_activation_plan(replace(bytes, encode_hex("/store/tree"), encode_hex("/store/../tree"))); }, SchemaErrorCode::Path);
    rejects([&] { (void)parse_activation_plan(replace(bytes, encode_hex("../objects/v1/sha256/x"), "00")); }, SchemaErrorCode::Path);
    bad = value; std::swap(bad.sources[0], bad.sources[1]);
    rejects([&] { (void)serialize(bad); }, SchemaErrorCode::Ordering);
    bad = value; bad.sources.push_back(LegacyTreeSource{"/store/tree", LegacySealDigest(digest('c'))});
    rejects([&] { (void)serialize(bad); }, SchemaErrorCode::Ordering);
    auto duplicates = plan();
    duplicates.operations = {{0, "x", CreateLink{0, fresh(0)}}, {1, "x", CreateLink{0, fresh(1)}}};
    rejects([&] { (void)serialize(duplicates); }, SchemaErrorCode::Ordering);
    bad = value; std::swap(bad.operations[0], bad.operations[1]); renumber(bad);
    rejects([&] { (void)serialize(bad); }, SchemaErrorCode::Ordering);
    bad = value; std::swap(bad.operations[2], bad.operations[3]); renumber(bad);
    rejects([&] { (void)serialize(bad); }, SchemaErrorCode::Ordering);
    bad = value; std::swap(bad.operations[4], bad.operations[5]); renumber(bad);
    rejects([&] { (void)serialize(bad); }, SchemaErrorCode::Ordering);
    bad = value; std::swap(bad.operations[1], bad.operations[2]); renumber(bad);
    rejects([&] { (void)serialize(bad); }, SchemaErrorCode::Ordering);
    bad = value; std::get<KeepLink>(bad.operations[5].state).new_source = 1;
    rejects([&] { (void)serialize(bad); }, SchemaErrorCode::Reference);
    auto contradiction = plan();
    contradiction.operations[1].path = "bin";
    rejects([&] { (void)serialize(contradiction); }, SchemaErrorCode::Conflict);
    contradiction.operations = {{0, "a", CreateLink{0, fresh(0)}}, {1, "a/child", CreateLink{0, fresh(1)}}};
    rejects([&] { (void)serialize(contradiction); }, SchemaErrorCode::Conflict);
    contradiction.operations = {{0, "a/dir", EnsureDirectory{}}, {1, "a", CreateLink{0, fresh(1)}}};
    rejects([&] { (void)serialize(contradiction); }, SchemaErrorCode::Conflict);
    contradiction.operations = {{0, "a", RemoveLink{0, old(0)}}, {1, "a", CreateLink{0, fresh(1)}}};
    rejects([&] { (void)serialize(contradiction); }, SchemaErrorCode::Conflict);
    // Same-path REMOVE/ENSURE_DIRECTORY is the supported structural conversion.
    contradiction.operations = {{0, "a", RemoveLink{0, old(0)}}, {1, "a", EnsureDirectory{}}};
    roundtrip(contradiction);
    for (const auto& invalid : {"", "/a", "a/", "a//b", "a/./b", "a/../b"}) {
        contradiction.operations = {{0, invalid, EnsureDirectory{}}};
        rejects([&] { (void)serialize(contradiction); }, SchemaErrorCode::Path);
    }
    contradiction.operations = {{0, std::string("a\0b", 3), EnsureDirectory{}}};
    rejects([&] { (void)serialize(contradiction); }, SchemaErrorCode::Path);
    contradiction.operations = {{0, ".athena-activation/owned", EnsureDirectory{}}};
    rejects([&] { (void)serialize(contradiction); }, SchemaErrorCode::Conflict);
    require(serialize(value) == bytes, "Rejected inputs mutated canonical plan");
}
void alias_target_bounds() {
    ActivationPlan value{id, {LegacyAliasSource{"/store/alias", "target",
        athena::core::ArtifactId(digest('a')), LegacySealDigest(digest('b'))}},
        {{0, "tool", CreateLink{0, fresh(0)}}}};
    const auto canonical = serialize(value);
    const auto persisted = [&](const std::string& target) {
        return replace(canonical, "alias_target_hex = \"" + encode_hex("target") + "\"",
            "alias_target_hex = \"" + encode_hex(target) + "\"");
    };
    const auto accept = [&](const std::string& target) {
        std::get<LegacyAliasSource>(value.sources[0]).alias_target = target;
        const auto bytes = serialize(value);
        require(bytes == persisted(target), "Alias target writer changed literal bytes");
        const auto parsed = parse_activation_plan(bytes);
        require(parsed == value, "Alias target roundtrip changed values");
        require(std::get<LegacyAliasSource>(parsed.sources[0]).alias_target == target,
            "Alias target bytes were normalized");
    };
    const auto reject = [&](const std::string& target, SchemaErrorCode code) {
        std::get<LegacyAliasSource>(value.sources[0]).alias_target = target;
        rejects([&] { (void)serialize(value); }, code);
        // Bypass the validating writer to exercise canonical-looking durable input.
        rejects([&] { (void)parse_activation_plan(persisted(target)); }, code);
    };
    const std::string component(255, 'x'), oversized(256, 'x');
    const std::string high(255, '\xff'), high_oversized(256, '\xff');
    for (const auto& target : {component, component + '/' + component, high,
        high + '/' + component, "../" + component, component + "/..",
        '/' + component, component + '/', '/' + component + "//" + component + '/'})
        accept(target);
    for (const auto& target : {std::string("."), std::string(".."), std::string("../objects/v1/sha256/x"),
        std::string("/"), std::string("//"), std::string("./a//../b/"), std::string("space \t\n")})
        accept(target);
    for (const auto& target : {oversized, component + '/' + oversized, oversized + '/' + component,
        "a/" + oversized + "/b", '/' + oversized + '/', high_oversized,
        component + '/' + high_oversized})
        reject(target, SchemaErrorCode::Bounds);
    std::string maximum;
    for (int i = 0; i < 16; ++i) { if (i) maximum += '/'; maximum += component; }
    require(maximum.size() == max_path_size, "Alias total-length fixture");
    accept(maximum);
    // All components remain <=255; only the independent total bound is exceeded.
    reject(maximum + '/', SchemaErrorCode::Bounds);
    reject(std::string("a\0/b", 4), SchemaErrorCode::Path);
    reject("", SchemaErrorCode::Path);
}
void bounds_and_bytes() {
    auto value = plan();
    value.operations = {{0, "space \t\n", EnsureDirectory{}}, {1, std::string("raw-\xff", 5), EnsureDirectory{}}};
    std::sort(value.operations.begin(), value.operations.end(), operation_less); renumber(value);
    roundtrip(value);
    const auto bytes = serialize(value);
    require(bytes.find('\xff') == bytes.npos && bytes.find("7261772dff") != bytes.npos, "Native high bytes escaped incorrectly");
    auto bad = value;
    bad.operations = {{0, std::string(256, 'a'), EnsureDirectory{}}};
    rejects([&] { (void)serialize(bad); }, SchemaErrorCode::Bounds);
    bad.operations = {{0, std::string(255, 'a'), EnsureDirectory{}}}; roundtrip(bad);
    std::string maximum;
    for (int i = 0; i < 16; ++i) { if (i) maximum += '/'; maximum += std::string(255, 'a'); }
    require(maximum.size() == max_path_size, "Maximum path fixture");
    bad.operations[0].path = maximum; roundtrip(bad);
    bad.operations[0].path = maximum + 'a';
    rejects([&] { (void)serialize(bad); }, SchemaErrorCode::Bounds);
    require(decode_hex(encode_hex(std::string("\x80\xff \t\n", 5)), 5) == std::string("\x80\xff \t\n", 5), "Byte hex roundtrip");
    rejects([&] { (void)decode_hex("00", 0); }, SchemaErrorCode::Bounds);
    value.operations = {{0, "unicode-\xc3\xa9", EnsureDirectory{}}, {1, "unicode-e\xcc\x81", EnsureDirectory{}}};
    std::sort(value.operations.begin(), value.operations.end(), operation_less); renumber(value); roundtrip(value);
    require(value.operations[0].path != value.operations[1].path, "Unicode byte forms normalized");
}
void document_limits() {
    const std::string oversized(max_document_size + 1, ' ');
    rejects([&] { (void)parse_activation_intent(oversized); }, SchemaErrorCode::Bounds);
    rejects([&] { (void)parse_activation_plan(oversized); }, SchemaErrorCode::Bounds);
    rejects([&] { (void)parse_activation_status(oversized); }, SchemaErrorCode::Bounds);
    ActivationPlan limit{id, {}, {}};
    for (std::size_t i = 0; i <= max_operations; ++i)
        limit.operations.push_back({i, "p" + std::to_string(i), EnsureDirectory{}});
    rejects([&] { (void)serialize(limit); }, SchemaErrorCode::Bounds);
    // Count checks happen before entry decoding, including malformed entry tables.
    std::string too_many = "schema_version = 1\nactivation_id = \"" + id.value() + "\"\nsources = []\n";
    for (std::size_t i = 0; i <= max_operations; ++i) too_many += "[[operations]]\nx = 0\n";
    rejects([&] { (void)parse_activation_plan(too_many); }, SchemaErrorCode::Bounds);
    too_many = "schema_version = 1\nactivation_id = \"" + id.value() + "\"\noperations = []\n";
    for (std::size_t i = 0; i <= max_sources; ++i) too_many += "[[sources]]\nx = 0\n";
    rejects([&] { (void)parse_activation_plan(too_many); }, SchemaErrorCode::Bounds);
    // Accept the operation limit, with deterministic ordering and actual parsing.
    limit.operations.pop_back();
    std::sort(limit.operations.begin(), limit.operations.end(), operation_less); renumber(limit);
    roundtrip(limit);
    ActivationPlan sources{id, {}, {}};
    for (std::size_t i = 0; i < max_sources; ++i) {
        char buffer[16]; const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), i, 16);
        require(error == std::errc{}, "Hex fixture conversion");
        const std::string suffix(buffer, end);
        sources.sources.push_back(ArtifactSource{athena::core::ArtifactId("sha256:" + std::string(64 - suffix.size(), '0') + suffix)});
    }
    roundtrip(sources);
    sources.sources.push_back(sources.sources.back());
    rejects([&] { (void)serialize(sources); }, SchemaErrorCode::Bounds);
    // Valid individual paths/counts, but aggregate canonical output exceeds 16 MiB.
    ActivationPlan output_limit{id, {}, {}};
    std::string prefix;
    for (int i = 0; i < 15; ++i) { if (i) prefix += '/'; prefix += std::string(255, 'a'); }
    for (std::size_t i = 0; i < 4096; ++i)
        output_limit.operations.push_back({i, std::to_string(10000 + i) + '/' + prefix, EnsureDirectory{}});
    // Already byte-ordered: avoid sorting thousands of near-PATH_MAX fixtures.
    output_limit.validate();
    rejects([&] { (void)serialize(output_limit); }, SchemaErrorCode::Bounds);
}
void strict_toml() {
    const auto i = serialize(intent()), p = serialize(plan());
    const auto s = serialize(ActivationStatus{id, PlanDigest(digest('a')), TerminalOutcome::Complete});
    const auto check = [&](const auto& parse, const std::string& bytes) {
        for (const auto& type : {"1.0", "\"1\"", "true", "[1]", "{x = 1}", "2", "-1"})
            rejects([&] { (void)parse(replace(bytes, "schema_version = 1", std::string("schema_version = ") + type)); }, SchemaErrorCode::Schema);
        rejects([&] { (void)parse("schema_version = 1\n" + bytes); }, SchemaErrorCode::Schema);
        rejects([&] { (void)parse("unknown = 1\n" + bytes); }, SchemaErrorCode::Schema);
        rejects([&] { (void)parse(replace(bytes, "schema_version = 1\n", "") + "\n[schema_version]\nx = 1\n"); }, SchemaErrorCode::Schema);
        rejects([&] { (void)parse(bytes + "# comment\n"); }, SchemaErrorCode::Noncanonical);
        rejects([&] { (void)parse(bytes + "\n"); }, SchemaErrorCode::Noncanonical);
        rejects([&] { (void)parse(bytes.substr(0, bytes.size() - 1)); }, SchemaErrorCode::Noncanonical);
        rejects([&] { (void)parse(replace(bytes, "schema_version = 1", "schema_version=1")); }, SchemaErrorCode::Noncanonical);
        rejects([&] { (void)parse(replace(bytes, "schema_version = 1", "schema_version = 0x1")); }, SchemaErrorCode::Noncanonical);
        rejects([&] { (void)parse(bytes + "junk\n"); }, SchemaErrorCode::Schema);
    };
    check([](const auto& bytes) { return parse_activation_intent(bytes); }, i);
    check([](const auto& bytes) { return parse_activation_plan(bytes); }, p);
    check([](const auto& bytes) { return parse_activation_status(bytes); }, s);
}
}
int main() { return run([] { intent_tests(); plan_tests(); alias_target_bounds(); bounds_and_bytes(); document_limits(); strict_toml(); }); }
