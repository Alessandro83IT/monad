// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation/operational_request_codec.hpp"
#include "activation/codec_detail.hpp"
#include "activation_schema_support.hpp"
#include <algorithm>
#include <cstdlib>
#include <new>
#include <tuple>
#include <type_traits>
// Observe ordinary heap allocations only while running the envelope preflight.
// Fixtures and assertions are constructed outside this window. No memory-stress
// probe or machine-wide limit is needed to detect accidental AST materialization.
namespace envelope_allocations {
thread_local bool enabled = false;
thread_local std::size_t calls = 0, bytes = 0;
}
void* operator new(std::size_t size) {
    if (envelope_allocations::enabled) {
        ++envelope_allocations::calls;
        envelope_allocations::bytes += size;
    }
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
using namespace athena::activation;
using namespace d1_test;
namespace {
OperationalGeneration gen(std::uint64_t n, std::vector<OperationalSourceProjection> sources = {}) {
    athena::generation::Generation g{n, {}};
    for (const auto& s : sources) {
        if (const auto* object = std::get_if<ArtifactSource>(&s.source)) {
            using namespace athena::core;
            RepositorySnapshotRef snapshot{RepositoryId("repo"), RepositoryRevision("rev"), SchemaVersion(1),
                ContentDigest(digest('a')), SnapshotId(digest('b'))};
            g.realizations.push_back({{PackageId("object"), PackageVersion("1"), snapshot.repository_id, snapshot,
                RepositoryProvenance{snapshot.repository_id, snapshot.revision, MetadataReference("recipe"), snapshot}},
                object->artifact_id, {}, ArtifactOrigin::AmbientSourceBuild});
        } else {
            auto locator = std::visit([](const auto& value) -> std::string {
                if constexpr (requires { value.locator; }) return value.locator;
                else return {};
            }, s.source);
            g.entries.push_back({"pkg" + std::to_string(g.entries.size()), locator, {}});
        }
    }
    auto doc = athena::generation::validate_generation_document(athena::generation::serialize(g), n);
    return {n, doc.digest, doc.bytes, std::move(sources)};
}
OperationalPlanInput sample() {
    OperationalSourceProjection s{LegacyTreeSource{"/store/tree", LegacySealDigest(digest('a'))}, {"bin/z", "bin/a"}, {}};
    auto base = gen(1, {s});
    return {id, {projection_id, "/state/generations", "/store", "/target"},
        SelectedBase{CurrentV1{1, old_id, PlanDigest(digest('b'))}, base.digest, ActivationOwnership{old_id}},
        base, gen(2, {s, {LegacyTreeSource{"/store/zero", LegacySealDigest(digest('c'))}, {}, {}}}),
        {{"bin/z", old(7)}, {"bin/a", old(4)}}};
}
void rejected(auto action) {
    try { action(); }
    catch (const std::bad_alloc&) { throw; }
    catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid request accepted");
}
void envelope_budgets() {
    using namespace request_codec_detail;
    envelope_allocations::calls = 0; envelope_allocations::bytes = 0;
    envelope_allocations::enabled = true;
    try { const auto ast = codec_detail::parse("values = [1, 2, 3]\n"); }
    catch (...) { envelope_allocations::enabled = false; throw; }
    envelope_allocations::enabled = false;
    require(envelope_allocations::calls > 0 && envelope_allocations::bytes > 0,
        "allocation observer detects the shared TOML AST allocator");
    const std::string root = "schema_version = 1\nintent_hex = \"\"\nplan_hex = \"\"\nplan_digest = \"" +
        digest('a') + "\"\nbase_document_hex = \"\"\ntarget_document_hex = \"\"\n";
    const std::string empty = root + "base_sources = []\ntarget_sources = []\n";
    const std::string source = "\n[[target_sources]]\nsource_hex = \"\"\nlinks_hex = [\"61\", \"6263\"]\ndirectories_hex = [\"64\"]\n";
    const std::string inventory = root + "base_sources = []\n" + source;
    // This intentionally contains opaque, semantically invalid empty documents:
    // preflight must inspect the entire envelope without decoding or using TOML.
    auto observed = [](std::string_view bytes, const EnvelopeLimits& limits) {
        envelope_allocations::calls = 0; envelope_allocations::bytes = 0;
        envelope_allocations::enabled = true;
        bool passed = false;
        SchemaErrorCode code = SchemaErrorCode::Schema;
        try { validate_envelope(bytes, limits); passed = true; }
        catch (const SchemaError& error) { code = error.code(); }
        catch (...) { envelope_allocations::enabled = false; throw; }
        envelope_allocations::enabled = false;
        return std::tuple{passed, code, envelope_allocations::calls, envelope_allocations::bytes};
    };
    const auto [passed, code, calls, allocated] = observed(inventory, {});
    require(passed && calls == 0 && allocated == 0, "complete envelope preflight allocates no heap storage");
    EnvelopeLimits limits;
    limits.nodes = 16; limits.sources_per_generation = 1; limits.paths_per_generation = 3;
    limits.path_bytes_per_generation = 4; limits.decoded_bytes = 4;
    limits.path_bytes = 2; limits.document_bytes = 0; limits.envelope_bytes = inventory.size();
    validate_envelope(inventory, limits); // Every tight boundary is inclusive.
    auto limited = [&](auto change) {
        auto tighter = limits; change(tighter);
        const auto [ok, failure, allocations, memory] = observed(inventory, tighter);
        require(!ok && failure == SchemaErrorCode::Bounds, "exclusive envelope budget boundary");
        require(allocations <= 4 && memory <= 1024, "budget rejection allocates only bounded diagnostics");
    };
    limited([](auto& l) { --l.nodes; });
    limited([](auto& l) { --l.sources_per_generation; });
    limited([](auto& l) { --l.paths_per_generation; });
    limited([](auto& l) { --l.path_bytes_per_generation; });
    limited([](auto& l) { --l.decoded_bytes; });
    limited([](auto& l) { --l.path_bytes; });
    limited([](auto& l) { --l.envelope_bytes; });
    auto field_limits = EnvelopeLimits{}; field_limits.document_bytes = 1;
    validate_envelope(replace(empty, "intent_hex = \"\"", "intent_hex = \"61\""), field_limits);
    rejects([&] { validate_envelope(replace(empty, "intent_hex = \"\"", "intent_hex = \"6162\""), field_limits); },
        SchemaErrorCode::Bounds);
    field_limits = {}; field_limits.envelope_bytes = max_operational_request_size + 1;
    rejects([&] { validate_envelope(empty, field_limits); }, SchemaErrorCode::Bounds);
    // Small hostile arrays exercise the same checks as production, with a path
    // allowance of three. Increasing rejected elements cannot grow heap usage.
    for (const std::size_t n : {64, 4096}) {
        std::string elements = "\"\"";
        for (std::size_t i = 1; i < n; ++i) elements += ", \"\"";
        const auto hostile = replace(inventory, "[\"61\", \"6263\"]", "[" + elements + "]");
        auto tight = EnvelopeLimits{}; tight.paths_per_generation = 3;
        const auto [ok, failure, allocations, memory] = observed(hostile, tight);
        require(!ok && failure == SchemaErrorCode::Bounds && allocations <= 4 && memory <= 1024,
            "hostile arrays rejected with constant diagnostic allocation");
    }
    auto two_sources = inventory + source;
    auto source_limits = EnvelopeLimits{}; source_limits.sources_per_generation = 1;
    rejects([&] { validate_envelope(two_sources, source_limits); }, SchemaErrorCode::Bounds);
    // Paths/bytes accumulate across both arrays and source tables, then reset
    // only at the generation boundary. The node and decoded-byte budgets do not.
    source_limits = {}; source_limits.paths_per_generation = 5;
    rejects([&] { validate_envelope(two_sources, source_limits); }, SchemaErrorCode::Bounds);
    source_limits = {}; source_limits.path_bytes_per_generation = 7;
    rejects([&] { validate_envelope(two_sources, source_limits); }, SchemaErrorCode::Bounds);
    const auto both = root + replace(source, "target_sources", "base_sources") + source;
    limits.envelope_bytes = both.size(); limits.nodes = 23; limits.decoded_bytes = 8;
    validate_envelope(both, limits);
    --limits.decoded_bytes;
    rejects([&] { validate_envelope(both, limits); }, SchemaErrorCode::Bounds);
    limits.decoded_bytes = 8; --limits.nodes;
    rejects([&] { validate_envelope(both, limits); }, SchemaErrorCode::Bounds);
    for (const auto& hostile : {
        replace(empty, "base_sources = []", "base_sources = [1, 2, 3]"),
        replace(empty, "intent_hex = \"\"", "intent_hex = [[[[1]]]]"),
        replace(empty, "intent_hex = \"\"", "intent_hex = { x = [1, 2] }"),
        replace(inventory, "links_hex = [\"61\", \"6263\"]", "links_hex = [[[[]]]]"),
        replace(inventory, "source_hex = \"\"", "unknown = [1, 2, 3]"),
        replace(empty, "intent_hex", "unknown"),
        replace(empty, "plan_hex", "intent_hex"),
        empty + "[unexpected.table]\nx = [1, 2]\n",
        replace(inventory, "[[target_sources]]", "[[target_sources.nested]]"),
        replace(inventory, "\"6263\"", "\"6263\", "),
        replace(inventory, "\"6263\"", "'6263'"),
        replace(empty, "intent_hex = \"\"", "intent_hex = \"\\u0061\""),
        replace(empty, "schema_version = 1", "schema_version=1"),
        "# comment\n" + empty,
        empty + "\n"}) {
        const auto [ok, failure, allocations, memory] = observed(hostile, {});
        require(!ok && allocations <= 4 && memory <= 1024, "hostile syntax cannot materialize an AST");
        rejected([&] { parse_operational_request(hostile); });
    }
    for (std::size_t i = 0; i < inventory.size(); ++i)
        rejected([&] { validate_envelope(std::string_view(inventory).substr(0, i)); });
    rejects([&] { validate_envelope(replace(empty, "intent_hex = \"\"", "intent_hex = \"6\"")); }, SchemaErrorCode::Schema);
    rejects([&] { validate_envelope(replace(empty, "intent_hex = \"\"", "intent_hex = \"AF\"")); }, SchemaErrorCode::Schema);
    // Production entry point must finish preflight before decoding the invalid
    // intent, even when the offending node is at the end of the envelope.
    rejects([&] { parse_operational_request(inventory + "unknown = []\n"); }, SchemaErrorCode::Noncanonical);
    const auto nested = replace(inventory, "[\"61\", \"6263\"]",
        std::string(256, '[') + "0" + std::string(256, ']'));
    const auto [nested_ok, nested_code, nested_calls, nested_memory] = observed(nested, {});
    require(!nested_ok && nested_calls <= 4 && nested_memory <= 1024, "nesting rejected without recursive parsing");
    for (unsigned mask = 0; mask < 4; ++mask) {
        const OperationalSourceProjection s{LegacyTreeSource{"/store/tree", LegacySealDigest(digest('a'))}, {"bin/a"}, {}};
        auto base = gen(1, mask & 1 ? std::vector{s} : std::vector<OperationalSourceProjection>{});
        const BaseOwnership ownership = mask & 1 ? BaseOwnership{AdoptionOwnership{adoption_id}} : BaseOwnership{EmptyOwnership{}};
        OperationalPlanInput input{id, sample().binding,
            SelectedBase{HistoricalCurrent{1, "1\n"}, base.digest, ownership}, base,
            gen(2, mask & 2 ? std::vector{s} : std::vector<OperationalSourceProjection>{}), {}};
        if (mask & 1) input.old_witnesses.push_back({"bin/a", {adoption_id, 1}});
        const auto canonical = serialize_operational_request(PreparedOperationalRequest::create(input));
        const auto [ok, unused, count, memory] = observed(canonical, {});
        require(ok && count == 0 && memory == 0, "all canonical empty/nonempty source layouts allocate no preflight storage");
        require(serialize_operational_request(parse_operational_request(canonical)) == canonical,
            "all four canonical source layouts roundtrip");
    }
}
void document_limits() {
    constexpr std::size_t half = max_document_size / 2;
    for (const auto size : {half, half + 1, max_document_size}) {
        auto padded = [size](std::uint64_t n) {
            auto g = gen(n);
            if (n == 1) g.document = "id = 1\nentries = []\n"; // Original historical schema.
            g.document += "# historical TOML comment: ";
            require(g.document.size() < size, "padding fits document boundary");
            g.document.append(size - g.document.size() - 1, 'x');
            g.document += '\n';
            const auto validated = athena::generation::validate_generation_document(g.document, n);
            require(validated.bytes == g.document && validated.bytes.size() == size,
                "generation contract includes exact document boundary");
            require(validated.generation.compatibility == (n == 1 ?
                athena::generation::GenerationCompatibility::Historical :
                athena::generation::GenerationCompatibility::Modern),
                "historical and modern generation schemas exercised");
            g.digest = validated.digest;
            return g;
        };
        auto base = padded(1);
        OperationalPlanInput input{id, sample().binding,
            SelectedBase{HistoricalCurrent{1, "1\n"}, base.digest, EmptyOwnership{}},
            base, padded(2), {}};
        const auto frozen = PreparedOperationalRequest::create(input);
        const auto bytes = serialize_operational_request(frozen);
        const auto parsed = parse_operational_request(bytes);
        require(parsed.input().base_generation->document == input.base_generation->document &&
            parsed.input().target.document == input.target.document,
            "base and target preserve all historical comment bytes");
        require(parsed.input().base_generation->digest == input.base_generation->digest &&
            parsed.input().target.digest == input.target.digest &&
            parsed.identity().digest == frozen.identity().digest,
            "exact generation and plan digests at document boundary");
        require(serialize_operational_request(parsed) == bytes, "large canonical roundtrip");
        rejects([&] { parse_operational_request("# noncanonical envelope\n" + bytes); },
            SchemaErrorCode::Noncanonical);
        if (size == half)
            require(encode_hex(input.target.document).size() == 2 * half,
                "shared hex encoder includes 8 MiB input boundary");
        if (size == half + 1) {
            // The shared encoder retains its original 8 MiB inclusive limit.
            rejects([&] { encode_hex(input.target.document); }, SchemaErrorCode::Bounds);
            auto changed = input.target.document;
            changed[changed.size() - 2] = 'y';
            require(athena::generation::validate_generation_document(changed, 2).digest != input.target.digest,
                "historical comment bytes participate in digest");
            auto altered = bytes;
            const auto offset = altered.find("target_document_hex = \"") +
                std::string_view("target_document_hex = \"").size();
            altered.replace(offset + 2 * (size - 2), 2, "79");
            rejects([&] { parse_operational_request(altered); }, SchemaErrorCode::Reference);
        }
        if (size == max_document_size) {
            input.target.document += ' ';
            rejects([&] { PreparedOperationalRequest::create(input); }, SchemaErrorCode::Bounds);
            input.target.document.pop_back();
            input.base_generation->document += ' ';
            rejects([&] { PreparedOperationalRequest::create(input); }, SchemaErrorCode::Bounds);
            auto oversized = bytes;
            const auto start = oversized.find("target_document_hex = \"");
            const auto end = oversized.find('"', start + std::string_view("target_document_hex = \"").size());
            oversized.insert(end, "20");
            rejects([&] { parse_operational_request(oversized); }, SchemaErrorCode::Bounds);
            oversized = bytes;
            const auto base_start = oversized.find("base_document_hex = \"");
            const auto base_end = oversized.find('"', base_start + std::string_view("base_document_hex = \"").size());
            oversized.insert(base_end, "20");
            rejects([&] { parse_operational_request(oversized); }, SchemaErrorCode::Bounds);
        }
    }
}
void plan_limits() {
    auto base = gen(1);
    OperationalSourceProjection source{LegacyTreeSource{"/store/tree", LegacySealDigest(digest('a'))}, {}, {}};
    for (std::size_t i = 0; i < 18000; ++i)
        source.links.push_back(std::string(200, 'x') + std::to_string(i));
    OperationalPlanInput input{id, sample().binding,
        SelectedBase{HistoricalCurrent{1, "1\n"}, base.digest, EmptyOwnership{}},
        base, gen(2, {source}), {}};
    const auto plan = build_operational_plan(input);
    const auto plan_bytes = serialize(plan.plan);
    require(plan_bytes.size() > max_document_size / 2 && plan_bytes.size() <= max_document_size,
        "valid D8.1 plan above shared hex input bound");
    const auto frozen = PreparedOperationalRequest::create(input);
    const auto bytes = serialize_operational_request(frozen);
    const auto parsed = parse_operational_request(bytes);
    require(parsed.identity().digest == plan.digest &&
        serialize(build_operational_plan(parsed.input()).plan) == plan_bytes,
        "large plan exact bytes and digest");
    require(serialize_operational_request(parsed) == bytes, "large plan canonical roundtrip");
    for (std::size_t i = 18000; i < 36000; ++i)
        input.target.sources[0].links.push_back(std::string(200, 'x') + std::to_string(i));
    rejects([&] { PreparedOperationalRequest::create(input); }, SchemaErrorCode::Bounds);
}
void embedded_generation_compatibility() {
    // Cardinality is intentionally not tightened here: existing generation
    // contracts accept repeated policies. These small fixtures qualify the
    // compatibility requirement for a future bounded TOML front end.
    const std::string policy = "\r\n[[policies]]\r\nschema_version=1\r\npolicy='p'\r\nselector='pkg'\r\n"
        "[policies.target]\r\nplatform=''\r\narchitecture=''\r\nabi=''\r\ntriple=''\r\n";
    for (const unsigned count : {1, 32}) {
        std::string document = "# exact historical formatting\r\n"
            "schema_version = 3\r\n\"\\u0069d\" = 2\r\nmemberships=[]\r\n";
        for (unsigned i = 0; i < count; ++i) {
            document += policy;
        }
        document += "# no final newline";
        const auto validated = athena::generation::validate_generation_document(document, 2);
        require(validated.generation.policies.size() == count && validated.bytes == document,
            "existing generation contract admits repeated policy tables and escaped keys");
        auto base = gen(1);
        OperationalPlanInput input{id, sample().binding,
            SelectedBase{HistoricalCurrent{1, "1\n"}, base.digest, EmptyOwnership{}},
            base, {2, validated.digest, document, {}}, {}};
        const auto frozen = PreparedOperationalRequest::create(input);
        const auto parsed = parse_operational_request(serialize_operational_request(frozen));
        require(parsed.input().target.document == document &&
            parsed.input().target.digest == validated.digest, "exact policy document roundtrip");
    }
}
void embedded_exposure_diagnostic() {
    // Regression: a 32-element unknown embedded field must be rejected
    // without constructing its array or scalar AST nodes.
    constexpr unsigned count = 32;
    std::string inner = "unexpected = [";
    for (unsigned i = 0; i < count; ++i) { if (i) inner += ','; inner += '0'; }
    inner += "]\n";
    auto bytes = serialize_operational_request(PreparedOperationalRequest::create(sample()));
    const auto prefix = std::string_view("intent_hex = \"");
    const auto begin = bytes.find(prefix) + prefix.size();
    const auto end = bytes.find('"', begin);
    bytes.replace(begin, end - begin, encode_hex(inner));
    request_codec_detail::validate_envelope(bytes);
    envelope_allocations::calls = 0; envelope_allocations::bytes = 0;
    envelope_allocations::enabled = true;
    bool rejected = false;
    try { (void)parse_operational_request(bytes); }
    catch (const SchemaError& error) { rejected = error.code() == SchemaErrorCode::Schema; }
    catch (...) { envelope_allocations::enabled = false; throw; }
    envelope_allocations::enabled = false;
    require(rejected && envelope_allocations::calls < count,
        "H1-R1 regression: unknown embedded field rejected before descendant AST allocation");
    std::cout << "H1-R1 guard rejected embedded input containing " << count << " scalar nodes: "
        << envelope_allocations::calls << " ordinary allocations, "
        << envelope_allocations::bytes << " requested bytes (not peak RSS).\n";
}
void retained_aliases() {
    auto input = sample();
    // Explicit legacy directories repeat link parents; both shared inventories
    // must agree. Long link names exercise heap-backed nested string buffers.
    const std::string long_link = "bin/" + std::string(80, 'x');
    input.target.sources[0].links.push_back(long_link);
    input.base_generation->sources[0].links.push_back(long_link);
    input.target.sources[0].directories = {"bin"};
    input.base_generation->sources[0].directories = {"bin"};
    input.old_witnesses.push_back({long_link, old(8)});
    auto& target_inventory = input.target.sources[0];
    auto& base_inventory = input.base_generation->sources[0];
    auto& link = target_inventory.links.back();
    auto& base_link = base_inventory.links.back();
    auto& directory = target_inventory.directories[0];
    auto& base_directory = base_inventory.directories[0];
    char* link_buffer = link.data();
    auto& witness = input.old_witnesses.back();
    char* witness_buffer = witness.path.data();
    char* target_document = input.target.document.data();
    char* base_document = input.base_generation->document.data();
    auto& source = std::get<LegacyTreeSource>(target_inventory.source);
    auto& binding = input.binding;
    auto& selected = std::get<SelectedBase>(input.base);
    auto& current = std::get<CurrentV1>(selected.current);
    auto& activation = input.activation_id;
    auto& target_digest = input.target.digest;
    auto& base_digest = input.base_generation->digest;
    const auto before = PreparedOperationalRequest::create(input);
    const auto bytes = serialize_operational_request(before);
    auto frozen = PreparedOperationalRequest::create(std::move(input));
    require(serialize_operational_request(frozen) == bytes, "rvalue snapshot identical");
    require(input.target.document == before.input().target.document && input.activation_id == id,
        "public rvalue boundary does not consume caller storage");
    require(target_document != frozen.input().target.document.data() &&
        base_document != frozen.input().base_generation->document.data(), "document buffers detached");
    link_buffer[0] = 'X'; witness_buffer[0] = 'X';
    target_document[0] = '#'; base_document[0] = '#';
    link.assign("changed/link"); base_link.assign("changed/base");
    directory.assign("changed-directory"); base_directory.assign("changed-base-directory");
    source.locator.assign("/store/changed"); source.seal_digest = LegacySealDigest(digest('f'));
    witness.witness.owner = adoption_id; witness.witness.entry_id = 99;
    current.generation = 99; current.activation_id = ActivationId("33333333333333333333333333333333");
    current.plan_digest = PlanDigest(digest('f'));
    selected.generation_digest = athena::generation::GenerationDocumentDigest(digest('f'));
    selected.ownership = EmptyOwnership{};
    activation = current.activation_id;
    target_digest = athena::generation::GenerationDocumentDigest(digest('f'));
    base_digest = target_digest;
    binding.projection_id = ProjectionId("33333333333333333333333333333333");
    binding.generation_authority.assign("/changed/generations");
    binding.store_authority.assign("/changed/store"); binding.projection_root.assign("/changed/root");
    input.target.id = 99; input.base_generation->id = 99;
    // Destroy caller inventories only after all retained element aliases were used.
    target_inventory.links.clear(); target_inventory.directories.clear();
    base_inventory.links.clear(); base_inventory.directories.clear();
    input.target.sources.clear(); input.base_generation->sources.clear(); input.old_witnesses.clear();
    input.target.document.clear(); input.base_generation.reset();
    require(frozen.identity().activation_id == before.identity().activation_id &&
        frozen.identity().digest == before.identity().digest, "identity stable after alias mutations");
    require(frozen.input().base == before.input().base && frozen.input().binding == before.input().binding,
        "base and binding stable after alias mutations");
    require(frozen.input().target.id == before.input().target.id &&
        frozen.input().target.digest == before.input().target.digest &&
        frozen.input().base_generation->digest == before.input().base_generation->digest,
        "generation identities stable after alias mutations");
    const auto same_generation = [](const OperationalGeneration& actual,
                                    const OperationalGeneration& expected) {
        require(actual.id == expected.id && actual.digest == expected.digest &&
            actual.document == expected.document, "generation document and identity unchanged");
        require(actual.sources.size() == expected.sources.size(), "inventory membership unchanged");
        for (std::size_t i = 0; i < actual.sources.size(); ++i) {
            require(actual.sources[i].source == expected.sources[i].source &&
                actual.sources[i].links == expected.sources[i].links &&
                actual.sources[i].directories == expected.sources[i].directories,
                "source, links and directories unchanged");
        }
    };
    same_generation(frozen.input().target, before.input().target);
    same_generation(*frozen.input().base_generation, *before.input().base_generation);
    require(frozen.input().activation_id == before.input().activation_id,
        "input ActivationId unchanged");
    require(frozen.input().old_witnesses.size() == before.input().old_witnesses.size(),
        "witness inventory unchanged");
    for (std::size_t i = 0; i < frozen.input().old_witnesses.size(); ++i) {
        require(frozen.input().old_witnesses[i].path == before.input().old_witnesses[i].path &&
            frozen.input().old_witnesses[i].witness == before.input().old_witnesses[i].witness,
            "witness path, owner and entry unchanged");
    }
    require(serialize_operational_request(frozen) == bytes, "all claims stable after alias mutations");
    require(serialize_operational_request(parse_operational_request(bytes)) == bytes, "detached snapshot roundtrip");

    // Historical current and the alternative source payload also own strings.
    auto historical = sample();
    auto& base = std::get<SelectedBase>(historical.base);
    base.current = HistoricalCurrent{1, "1\n"}; base.ownership = AdoptionOwnership{adoption_id};
    for (auto& w : historical.old_witnesses) w.witness.owner = adoption_id;
    historical.target = gen(3, {{LegacyAliasSource{"/store/alias", "objects/" + std::string(64, 'd'),
        athena::core::ArtifactId(digest('d')), LegacySealDigest(digest('e'))}, {"bin/tool"}, {}}});
    auto& historical_bytes = std::get<HistoricalCurrent>(base.current).bytes;
    auto& alias = std::get<LegacyAliasSource>(historical.target.sources[0].source);
    char* alias_target = alias.alias_target.data();
    auto historical_frozen = PreparedOperationalRequest::create(std::move(historical));
    const auto historical_snapshot = serialize_operational_request(historical_frozen);
    historical_bytes.assign("99\n"); alias_target[0] = 'X';
    alias.locator.assign("/store/changed"); alias.alias_target.clear();
    alias.artifact_id = athena::core::ArtifactId(digest('f')); alias.seal_digest = LegacySealDigest(digest('f'));
    require(serialize_operational_request(historical_frozen) == historical_snapshot,
        "historical bytes and alias source strings detached");

    auto artifact = sample();
    artifact.target = gen(4, {{ArtifactSource{athena::core::ArtifactId(digest('d'))}, {long_link},
        {"empty/" + std::string(80, 'd')}}});
    auto& artifact_id = std::get<ArtifactSource>(artifact.target.sources[0].source).artifact_id;
    char* directory_buffer = artifact.target.sources[0].directories[0].data();
    auto artifact_frozen = PreparedOperationalRequest::create(std::move(artifact));
    const auto artifact_snapshot = serialize_operational_request(artifact_frozen);
    artifact_id = athena::core::ArtifactId(digest('f')); directory_buffer[0] = 'X';
    artifact.target.sources[0].directories.clear();
    require(serialize_operational_request(artifact_frozen) == artifact_snapshot,
        "artifact identity and heap-backed directory buffer detached");

    // Deliberate test-only invariant corruption: no mutable production accessor.
    auto corrupted = before;
    const_cast<PlanIdentity&>(corrupted.identity()).activation_id = ActivationId("33333333333333333333333333333333");
    rejects([&] { serialize_operational_request(corrupted); }, SchemaErrorCode::Reference);
    corrupted = before;
    const_cast<PlanIdentity&>(corrupted.identity()).digest = PlanDigest(digest('f'));
    rejects([&] { serialize_operational_request(corrupted); }, SchemaErrorCode::Reference);
}
void tests() {
    envelope_budgets();
    document_limits();
    plan_limits();
    embedded_generation_compatibility();
    embedded_exposure_diagnostic();
    retained_aliases();
    static_assert(std::is_same_v<decltype(std::declval<PreparedOperationalRequest&>().input()), const OperationalPlanInput&>);
    auto input = sample();
    auto frozen = PreparedOperationalRequest::create(input);
    auto bytes = serialize_operational_request(frozen);
    auto parsed = parse_operational_request(bytes);
    require(serialize_operational_request(parsed) == bytes, "canonical roundtrip");
    require(parsed.identity().activation_id == id && parsed.identity().digest == frozen.identity().digest, "stable identity");
    require(parsed.input().base == input.base && parsed.input().base_generation->document == input.base_generation->document &&
        parsed.input().target.document == input.target.document, "original base and exact documents");
    require(parsed.input().old_witnesses.front().witness == old(4), "old witness preserved");
    require(parsed.input().target.sources.size() == 2 && parsed.input().target.sources[1].links.empty(), "zero-link membership");
    std::reverse(input.old_witnesses.begin(), input.old_witnesses.end());
    std::reverse(input.target.sources.begin(), input.target.sources.end());
    std::reverse(input.base_generation->sources[0].links.begin(), input.base_generation->sources[0].links.end());
    require(serialize_operational_request(PreparedOperationalRequest::create(input)) == bytes, "canonical input ordering");
    input.activation_id = ActivationId("33333333333333333333333333333333");
    require(frozen.identity().activation_id == id, "owned snapshot independent of caller");
    require(serialize_operational_request(PreparedOperationalRequest::create(input)) != bytes, "identity never regenerated");
    rejects([&] { PreparedOperationalRequest::create(sample(), PlanDigest(digest('f'))); }, SchemaErrorCode::Reference);
    rejects([&] { parse_operational_request(replace(bytes, "schema_version = 1", "schema_version = 2")); }, SchemaErrorCode::Schema);
    rejected([&] { parse_operational_request(bytes + "schema_version = 1\n"); });
    rejects([&] { parse_operational_request("# comment\n" + bytes); }, SchemaErrorCode::Noncanonical);
    rejects([&] { parse_operational_request(replace(bytes, "schema_version = 1", "schema_version=1")); }, SchemaErrorCode::Noncanonical);
    rejected([&] { parse_operational_request(bytes.substr(0, bytes.size() / 2)); });
    rejects([&] { parse_operational_request(bytes.substr(0, bytes.size() - 1)); }, SchemaErrorCode::Noncanonical);
    rejected([&] { parse_operational_request(bytes + "unexpected = 1\n"); });
    rejects([&] { parse_operational_request(replace(bytes, "62696e2f61\", \"62696e2f7a", "62696e2f7a\", \"62696e2f61")); }, SchemaErrorCode::Noncanonical);
    const auto target_table = bytes.find("[[target_sources]]");
    rejected([&] { parse_operational_request(bytes + bytes.substr(target_table)); });
    rejected([&] { parse_operational_request(replace(bytes, frozen.identity().digest.value(), digest('f'))); });
    rejected([&] { parse_operational_request(replace(bytes, "target_document_hex = \"", "target_document_hex = \"ff")); });
    auto bad = sample(); bad.target.digest = athena::generation::GenerationDocumentDigest(digest('f'));
    rejected([&] { PreparedOperationalRequest::create(bad); });
    bad = sample(); bad.target.sources[0].links.push_back("bin/a");
    rejected([&] { PreparedOperationalRequest::create(bad); });
    bad = sample(); bad.old_witnesses.push_back(bad.old_witnesses[0]);
    rejected([&] { PreparedOperationalRequest::create(bad); });
    bad = sample(); bad.target.sources.pop_back();
    rejected([&] { PreparedOperationalRequest::create(bad); });
    bad = sample(); bad.target.sources.push_back(bad.target.sources[0]);
    rejected([&] { PreparedOperationalRequest::create(bad); });
    bad = sample(); bad.base_generation->document += "\n";
    rejected([&] { PreparedOperationalRequest::create(bad); });
    bad = sample(); bad.target.sources[0].links = {std::string(max_path_size + 1, 'a')};
    rejected([&] { PreparedOperationalRequest::create(bad); });
    bad = sample(); bad.target.sources.resize(max_sources + 1, bad.target.sources[0]);
    rejected([&] { PreparedOperationalRequest::create(bad); });
    OperationalPlanInput absent{id, sample().binding, AbsentBase{}, {}, gen(0), {}};
    auto zero = PreparedOperationalRequest::create(absent);
    require(serialize_operational_request(parse_operational_request(serialize_operational_request(zero))) == serialize_operational_request(zero), "absent empty roundtrip");
    auto empty = gen(9);
    absent.base = SelectedBase{HistoricalCurrent{9, "9\n"}, empty.digest, EmptyOwnership{}};
    absent.base_generation = empty;
    auto historical = PreparedOperationalRequest::create(absent);
    require(parse_operational_request(serialize_operational_request(historical)).input().base == absent.base, "historical current bytes");
    auto adoption = sample();
    std::get<SelectedBase>(adoption.base).current = HistoricalCurrent{1, "1"};
    std::get<SelectedBase>(adoption.base).ownership = AdoptionOwnership{adoption_id};
    for (auto& w : adoption.old_witnesses) w.witness.owner = adoption_id;
    auto adopted = PreparedOperationalRequest::create(adoption);
    require(parse_operational_request(serialize_operational_request(adopted)).input().base == adoption.base, "adoption ownership retained");
    OperationalSourceProjection object{ArtifactSource{athena::core::ArtifactId(digest('d'))}, {}, {"empty"}};
    OperationalSourceProjection alias{LegacyAliasSource{"/store/alias", "objects/" + std::string(64, 'd'),
        athena::core::ArtifactId(digest('d')), LegacySealDigest(digest('e'))}, {"bin/tool"}, {}};
    auto variants = sample(); variants.target = gen(3, {object, alias});
    auto variant_request = PreparedOperationalRequest::create(variants);
    require(serialize_operational_request(parse_operational_request(serialize_operational_request(variant_request))) ==
        serialize_operational_request(variant_request), "artifact directories and alias source roundtrip");
    auto native = sample();
    const std::string raw_path = "bin/" + std::string(1, static_cast<char>(0xff));
    native.target.sources[0].links.push_back(raw_path);
    native.base_generation->sources[0].links.push_back(raw_path);
    native.old_witnesses.push_back({raw_path, old(8)});
    auto native_request = PreparedOperationalRequest::create(native);
    const auto native_parsed = parse_operational_request(serialize_operational_request(native_request));
    require(native_parsed.input().target.sources[0].links.back() == raw_path, "native non-UTF8 bytes retained");
    // Existing 16 MiB nested-document gate, without allocating a maximum envelope.
    auto oversized = replace(bytes, "target_document_hex = \"", "target_document_hex = \"" + std::string(2 * max_document_size + 2, 'a'));
    rejects([&] { parse_operational_request(oversized); }, SchemaErrorCode::Bounds);
}
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "embedded-contracts") return run(embedded_generation_compatibility);
    if (argc == 2 && std::string_view(argv[1]) == "embedded-exposure-diagnostic") return run(embedded_exposure_diagnostic);
    if (argc == 2 && std::string_view(argv[1]) == "envelope-budgets") return run(envelope_budgets);
    if (argc == 2 && std::string_view(argv[1]) == "document-limits") return run(document_limits);
    if (argc == 2 && std::string_view(argv[1]) == "plan-limits") return run(plan_limits);
    return run(tests);
}
