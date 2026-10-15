// SPDX-License-Identifier: GPL-3.0-or-later
#include "operational_request_codec.hpp"
#include "codec_detail.hpp"
#include <algorithm>

namespace athena::activation {
namespace {
namespace c = codec_detail;
void fail(SchemaErrorCode code, const char* message) { throw SchemaError(code, message); }
std::size_t hex_size(std::size_t size, std::size_t limit) {
    if (size > limit || size > max_document_size ||
        size > std::numeric_limits<std::size_t>::max() / 2)
        fail(SchemaErrorCode::Bounds, "Request hex input exceeds bound");
    return size * 2;
}
// The enclosing B1.1 document has its own larger bound. Do not use the shared
// encode_hex(), whose expanded output must fit a single 16 MiB D1 document.
// Counting and writing follow exactly the same layout, including punctuation.
class EnvelopeWriter {
public:
    explicit EnvelopeWriter(bool counting) : counting_(counting), out_(max_operational_request_size) {}
    void append(std::string_view bytes) {
        account(bytes.size());
        if (!counting_) out_.append(bytes);
    }
    void text(std::string_view key, std::string_view value) {
        append(key); append(" = \""); append(value); append("\"\n");
    }
    void hex(std::string_view bytes, std::size_t limit) {
        account(hex_size(bytes.size(), limit));
        if (counting_) return;
        constexpr char digits[] = "0123456789abcdef";
        char buffer[4096];
        while (!bytes.empty()) {
            const auto n = std::min(bytes.size(), sizeof(buffer) / 2);
            for (std::size_t i = 0; i < n; ++i) {
                const auto value = static_cast<unsigned char>(bytes[i]);
                buffer[2 * i] = digits[value >> 4]; buffer[2 * i + 1] = digits[value & 15];
            }
            out_.append(std::string_view(buffer, 2 * n));
            bytes.remove_prefix(n);
        }
    }
    void hex_text(std::string_view key, std::string_view bytes, std::size_t limit = max_document_size) {
        append(key); append(" = \""); hex(bytes, limit); append("\"\n");
    }
    const std::string& bytes() const { return out_.bytes(); }
private:
    void account(std::size_t size) {
        if (size > max_operational_request_size - size_)
            fail(SchemaErrorCode::Bounds, "Request aggregate envelope exceeds bound");
        size_ += size;
    }
    bool counting_;
    std::size_t size_ = 0;
    c::Writer out_;
};
struct SourceFields {
    std::string_view tables;
    std::size_t count = 0;
};
struct EnvelopeFields {
    std::string_view intent, plan, digest, base_document, target_document;
    SourceFields base, target;
};
// Fixed-size state and borrowed slices only. This reader never enters general
// TOML syntax: comments, escapes, inline tables, dotted keys, arbitrary arrays
// and nested values cannot reach an AST allocator. Unknown/duplicate/reordered
// keys are rejected when the next exact canonical token is expected.
class EnvelopeReader {
public:
    EnvelopeReader(std::string_view bytes, const request_codec_detail::EnvelopeLimits& limits = {})
        : remaining_(bytes), limits_(limits) {
        const request_codec_detail::EnvelopeLimits defaults;
        if (limits.envelope_bytes > defaults.envelope_bytes || limits.nodes > defaults.nodes ||
            limits.sources_per_generation > defaults.sources_per_generation ||
            limits.paths_per_generation > defaults.paths_per_generation ||
            limits.path_bytes_per_generation > defaults.path_bytes_per_generation ||
            limits.document_bytes > defaults.document_bytes || limits.path_bytes > defaults.path_bytes ||
            limits.decoded_bytes > defaults.decoded_bytes || bytes.size() > limits.envelope_bytes)
            fail(SchemaErrorCode::Bounds, "Request envelope preflight limit exceeded");
    }
    EnvelopeFields envelope() {
        charge(nodes_, 9, limits_.nodes); // Root, six fields, two source arrays.
        expect("schema_version = ");
        if (!take("1\n")) fail(SchemaErrorCode::Schema, "Unsupported request schema version");
        EnvelopeFields fields;
        fields.intent = field("intent_hex");
        fields.plan = field("plan_hex");
        expect("plan_digest = \"sha256:");
        const auto digest_tail = hex_chars(64);
        if (digest_tail.size() != 64) fail(SchemaErrorCode::Identity, "Invalid request digest");
        fields.digest = {digest_tail.data() - 7, 71};
        expect("\n");
        fields.base_document = field("base_document_hex");
        fields.target_document = field("target_document_hex");
        // Exactly the four layouts emitted by write_request(): empty arrays
        // precede all table arrays, and nonempty base tables precede target.
        if (take("base_sources = []\n")) {
            if (!take("target_sources = []\n")) fields.target = group("\n[[target_sources]]\n");
        } else if (take("target_sources = []\n")) {
            fields.base = group("\n[[base_sources]]\n");
        } else {
            fields.base = group("\n[[base_sources]]\n");
            fields.target = group("\n[[target_sources]]\n");
        }
        if (!remaining_.empty()) fail(SchemaErrorCode::Noncanonical, "Unexpected request envelope suffix");
        return fields;
    }
    // Used only after the full envelope has passed preflight. Decode one source
    // at a time; no vector of hex strings or envelope AST is retained.
    std::vector<OperationalSourceProjection> materialize(const SourceFields& fields, const ActivationId& id,
                                                        std::string_view header) {
        std::vector<OperationalSourceProjection> result;
        result.reserve(fields.count); // Exact count already authorized.
        for (std::size_t i = 0; i < fields.count; ++i) {
            expect(header);
            const auto encoded = field("source_hex");
            const auto source = parse_activation_plan(decode_hex(encoded, max_document_size), id);
            if (source.sources.size() != 1 || !source.operations.empty())
                fail(SchemaErrorCode::Reference, "Expected one source without operations");
            std::vector<std::string> links, directories;
            path_array("links_hex", &links);
            path_array("directories_hex", &directories);
            result.push_back({source.sources.front(), std::move(links), std::move(directories)});
        }
        if (!remaining_.empty()) fail(SchemaErrorCode::Noncanonical, "Unexpected source group suffix");
        return result;
    }
private:
    static void charge(std::size_t& used, std::size_t amount, std::size_t limit) {
        if (amount > limit - used) fail(SchemaErrorCode::Bounds, "Request envelope budget exceeded");
        used += amount;
    }
    bool take(std::string_view token) {
        if (!remaining_.starts_with(token)) return false;
        remaining_.remove_prefix(token.size());
        return true;
    }
    void expect(std::string_view token) {
        if (!take(token)) fail(SchemaErrorCode::Noncanonical, "Expected canonical request token");
    }
    std::string_view hex_chars(std::size_t encoded_limit) {
        std::size_t length = 0;
        while (length < remaining_.size() && remaining_[length] != '"') {
            if (length == encoded_limit) fail(SchemaErrorCode::Bounds, "Request hex field exceeds expanded bound");
            const char ch = remaining_[length];
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
                fail(SchemaErrorCode::Schema, "Expected canonical lowercase hex");
            ++length;
        }
        if (length == remaining_.size()) fail(SchemaErrorCode::Noncanonical, "Truncated request string");
        const auto result = remaining_.substr(0, length);
        remaining_.remove_prefix(length + 1); // Closing quote.
        return result;
    }
    std::string_view hex(std::size_t raw_limit) {
        expect("\"");
        const auto result = hex_chars(hex_size(raw_limit, raw_limit));
        if (result.size() % 2) fail(SchemaErrorCode::Schema, "Expected even-length request hex");
        charge(decoded_bytes_, result.size() / 2, limits_.decoded_bytes);
        return result;
    }
    std::string_view field(std::string_view key) {
        expect(key); expect(" = ");
        const auto result = hex(limits_.document_bytes);
        expect("\n");
        return result;
    }
    void path_array(std::string_view key, std::vector<std::string>* output = nullptr) {
        expect(key); expect(" = [");
        if (!take("]\n")) {
            for (;;) {
                // Charge each leaf before examining/decoding it. No untrusted
                // element count ever determines a reserve or recursion depth.
                charge(paths_, 1, limits_.paths_per_generation);
                charge(nodes_, 1, limits_.nodes);
                const auto value = hex(limits_.path_bytes);
                charge(path_bytes_, value.size() / 2, limits_.path_bytes_per_generation);
                if (output) output->push_back(decode_hex(value, max_path_size));
                if (take("]\n")) break;
                expect(", ");
            }
        }
    }
    SourceFields group(std::string_view header) {
        const auto start = remaining_;
        SourceFields result;
        paths_ = 0; path_bytes_ = 0; // Per-generation budgets, as in H2.
        do {
            charge(result.count, 1, limits_.sources_per_generation);
            charge(nodes_, 4, limits_.nodes); // Table, source scalar, two arrays.
            expect(header);
            field("source_hex");
            path_array("links_hex");
            path_array("directories_hex");
        } while (remaining_.starts_with(header));
        result.tables = start.substr(0, start.size() - remaining_.size());
        return result;
    }
    std::string_view remaining_;
    request_codec_detail::EnvelopeLimits limits_;
    std::size_t nodes_ = 0, decoded_bytes_ = 0, paths_ = 0, path_bytes_ = 0;
};
void paths(EnvelopeWriter& out, std::string_view key, const std::vector<std::string>& values) {
    out.append(key); out.append(" = [");
    bool first = true;
    for (const auto& value : values) {
        if (!first) out.append(", ");
        first = false; out.append("\""); out.hex(value, max_path_size); out.append("\"");
    }
    out.append("]\n");
}
void sources(EnvelopeWriter& out, std::string_view key, const OperationalGeneration* g, const ActivationId& id) {
    if (!g || g->sources.empty()) { out.append(key); out.append(" = []\n"); return; }
    for (const auto& s : g->sources) {
        out.append("\n[["); out.append(key); out.append("]]\n");
        // Reuse the canonical source union codec; no parallel source schema.
        out.hex_text("source_hex", serialize(ActivationPlan{id, {s.source}, {}}));
        paths(out, "links_hex", s.links); paths(out, "directories_hex", s.directories);
    }
}
void write_request(EnvelopeWriter& out, const OperationalPlanInput& input, const OperationalPlanResult& plan) {
    static_assert(PreparedOperationalRequest::format_version == 1);
    out.append("schema_version = 1\n");
    out.hex_text("intent_hex", serialize(plan.intent));
    out.hex_text("plan_hex", serialize(plan.plan));
    out.text("plan_digest", plan.digest.value());
    out.hex_text("base_document_hex", input.base_generation ? std::string_view(input.base_generation->document) : "");
    out.hex_text("target_document_hex", input.target.document);
    // Empty arrays must precede arrays of tables (TOML table scope).
    if (!input.base_generation || input.base_generation->sources.empty()) sources(out, "base_sources", nullptr, input.activation_id);
    if (input.target.sources.empty()) sources(out, "target_sources", nullptr, input.activation_id);
    if (input.base_generation && !input.base_generation->sources.empty()) sources(out, "base_sources", &*input.base_generation, input.activation_id);
    if (!input.target.sources.empty()) sources(out, "target_sources", &input.target, input.activation_id);
}
}
void request_codec_detail::validate_encoding(const OperationalPlanInput& input, const OperationalPlanResult& plan) {
    EnvelopeWriter size(true);
    write_request(size, input, plan);
}
void request_codec_detail::validate_envelope(std::string_view bytes, const EnvelopeLimits& limits) {
    EnvelopeReader(bytes, limits).envelope();
}
std::string serialize_operational_request(const PreparedOperationalRequest& request) {
    const auto& input = request.input();
    const auto plan = build_operational_plan(input);
    const PlanIdentity rebuilt{plan.intent.activation_id, plan.digest};
    if (rebuilt.activation_id != request.identity().activation_id ||
        rebuilt.digest != request.identity().digest)
        fail(SchemaErrorCode::Reference, "Frozen request identity differs from reconstructed plan");
    request_codec_detail::validate_encoding(input, plan);
    EnvelopeWriter out(false);
    write_request(out, input, plan);
    return out.bytes();
}
PreparedOperationalRequest parse_operational_request(std::string_view bytes) {
    // Authorize the entire envelope before any document decode, inventory
    // allocation or call into a shared TOML parser. This pass uses O(1) storage.
    const auto fields = EnvelopeReader(bytes).envelope();
    const auto intent = parse_activation_intent(decode_hex(fields.intent, max_document_size));
    const auto plan = parse_activation_plan(decode_hex(fields.plan, max_document_size), intent.activation_id);
    const PlanDigest digest{std::string(fields.digest)};
    if (derive_plan_digest(intent, plan) != digest) fail(SchemaErrorCode::Reference, "Stored request plan digest mismatch");
    auto target = generation::validate_generation_document(
        decode_hex(fields.target_document, max_document_size), intent.target_generation);
    if (target.digest != intent.target_generation_digest) fail(SchemaErrorCode::Reference, "Target document digest mismatch");
    const auto base_bytes = decode_hex(fields.base_document, max_document_size);
    auto base_sources = EnvelopeReader(fields.base.tables).materialize(fields.base, intent.activation_id, "\n[[base_sources]]\n");
    auto target_sources = EnvelopeReader(fields.target.tables).materialize(fields.target, intent.activation_id, "\n[[target_sources]]\n");
    std::optional<OperationalGeneration> base;
    if (const auto* selected = std::get_if<SelectedBase>(&intent.base)) {
        auto doc = generation::validate_generation_document(base_bytes, selected_generation(selected->current));
        if (doc.digest != selected->generation_digest) fail(SchemaErrorCode::Reference, "Base document digest mismatch");
        base = OperationalGeneration{doc.generation.id, doc.digest, std::move(doc.bytes), std::move(base_sources)};
    } else if (!base_bytes.empty() || !base_sources.empty()) fail(SchemaErrorCode::Reference, "Absent base has payload");
    std::vector<OperationalWitness> witnesses;
    for (const auto& op : plan.operations) std::visit([&](const auto& value) {
        if constexpr (requires { value.old_witness; }) witnesses.push_back({op.path, value.old_witness});
    }, op.state);
    auto result = PreparedOperationalRequest::create({intent.activation_id,
        {intent.projection_id, intent.generation_authority, intent.store_authority, intent.projection_root},
        intent.base, std::move(base), {target.generation.id, target.digest, std::move(target.bytes), std::move(target_sources)},
        std::move(witnesses)}, digest);
    const auto rebuilt = build_operational_plan(result.input());
    if (rebuilt.intent != intent || rebuilt.plan != plan) fail(SchemaErrorCode::Reference, "Request does not reproduce stored plan");
    c::canonical(bytes, serialize_operational_request(result)); return result;
}
}
