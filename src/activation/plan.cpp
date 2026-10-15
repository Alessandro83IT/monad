// SPDX-License-Identifier: GPL-3.0-or-later
#include "plan.hpp"
#include "codec_detail.hpp"
#include "../artifact/model.hpp"
#include "../hash/sha256.hpp"
#include <algorithm>
#include <set>
#include <type_traits>

namespace athena::activation {
namespace d = codec_detail;
namespace {
using GenerationDigest = generation::GenerationDocumentDigest;
void artifact_id(const core::ArtifactId& id) {
    if (!sha256_text(id.value())) throw SchemaError(SchemaErrorCode::Identity, "Invalid ArtifactId");
}
core::ArtifactId parse_artifact_id(const athena_toml::table& table) {
    const auto value = d::string(table, "artifact_id");
    if (!sha256_text(value)) throw SchemaError(SchemaErrorCode::Identity, "Invalid ArtifactId");
    return core::ArtifactId(value);
}
void expected(const ActivationId& id, const std::optional<ActivationId>& expected_id) {
    if (expected_id && id != *expected_id) throw SchemaError(SchemaErrorCode::Reference, "Activation ID mismatch");
}
std::string ownership_text(const BaseOwnership& ownership) {
    if (const auto* value = std::get_if<ActivationOwnership>(&ownership)) return "activation:" + value->id.value();
    if (const auto* value = std::get_if<AdoptionOwnership>(&ownership)) return "adoption:" + value->id.value();
    return "empty";
}
BaseOwnership parse_ownership(std::string_view text) {
    if (text == "empty") return EmptyOwnership{};
    if (text.starts_with("activation:")) return ActivationOwnership{ActivationId(std::string(text.substr(11)))};
    if (text.starts_with("adoption:")) return AdoptionOwnership{AdoptionId(std::string(text.substr(9)))};
    throw SchemaError(SchemaErrorCode::Identity, "Invalid base ownership");
}
std::string_view source_kind(const ActivationSource& source) {
    if (std::holds_alternative<ArtifactSource>(source)) return "artifact";
    if (std::holds_alternative<LegacyTreeSource>(source)) return "legacy_tree";
    return "legacy_alias";
}
std::string_view source_identity(const ActivationSource& source) {
    return std::visit([](const auto& value) -> std::string_view {
        if constexpr (std::is_same_v<std::decay_t<decltype(value)>, ArtifactSource>) return value.artifact_id.value();
        else return value.locator;
    }, source);
}
void validate_alias_target(std::string_view target) {
    if (target.size() > max_path_size) throw SchemaError(SchemaErrorCode::Bounds, "Alias target exceeds bound");
    if (target.empty() || target.find('\0') != target.npos)
        throw SchemaError(SchemaErrorCode::Path, "Invalid literal alias target");
    // Literal bytes: empty, dot and parent components keep their existing meaning.
    std::size_t start = 0;
    for (;;) {
        const auto end = target.find('/', start);
        const auto length = (end == target.npos ? target.size() : end) - start;
        if (length > max_component_size)
            throw SchemaError(SchemaErrorCode::Bounds, "Alias target component exceeds bound");
        if (end == target.npos) break;
        start = end + 1;
    }
}
void validate_source(const ActivationSource& source) {
    std::visit([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ArtifactSource>) artifact_id(value.artifact_id);
        else {
            validate_absolute_locator(value.locator);
            value.seal_digest.validate();
            if constexpr (std::is_same_v<T, LegacyAliasSource>) {
                artifact_id(value.artifact_id);
                validate_alias_target(value.alias_target);
            }
        }
    }, source);
}
int operation_group(const ActivationOperation& op) {
    if (std::holds_alternative<RemoveLink>(op.state)) return 0;
    if (std::holds_alternative<EnsureDirectory>(op.state)) return 1;
    return 2;
}
std::string_view operation_kind(const OperationState& state) {
    return std::visit([](const auto& value) -> std::string_view {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, CreateLink>) return "create";
        if constexpr (std::is_same_v<T, ReplaceLink>) return "replace";
        if constexpr (std::is_same_v<T, RemoveLink>) return "remove";
        if constexpr (std::is_same_v<T, KeepLink>) return "keep";
        return "ensure_directory";
    }, state);
}
struct ByteLess {
    bool operator()(std::string_view a, std::string_view b) const { return artifact::path_bytes_less(a, b); }
};
const athena_toml::table& table_node(const athena_toml::node& node) {
    const auto* table = node.as_table();
    if (!table) throw SchemaError(SchemaErrorCode::Schema, "Expected array-of-table entry");
    return *table;
}
const athena_toml::array& array(const athena_toml::table& table, std::string_view key, std::size_t limit) {
    const auto* result = table[key].as_array();
    if (!result) throw SchemaError(SchemaErrorCode::Schema, "Expected array: " + std::string(key));
    if (result->size() > limit) throw SchemaError(SchemaErrorCode::Bounds, "Array exceeds count bound");
    return *result;
}
}

void ActivationIntent::validate() const {
    activation_id.validate(); projection_id.validate(); target_generation_digest.validate();
    validate_absolute_locator(generation_authority);
    validate_absolute_locator(store_authority);
    validate_absolute_locator(projection_root);
    d::generation(target_generation);
    if (const auto* selected = std::get_if<SelectedBase>(&base)) {
        selected->generation_digest.validate();
        std::visit([](const auto& ownership) {
            if constexpr (requires { ownership.id; }) ownership.id.validate();
        }, selected->ownership);
        (void)serialize(selected->current);
        if (const auto* v1 = std::get_if<CurrentV1>(&selected->current)) {
            const auto* ownership = std::get_if<ActivationOwnership>(&selected->ownership);
            if (!ownership || ownership->id != v1->activation_id)
                throw SchemaError(SchemaErrorCode::Reference, "Base v1 ownership mismatch");
        } else if (std::holds_alternative<ActivationOwnership>(selected->ownership))
            throw SchemaError(SchemaErrorCode::Reference, "Decimal base requires adoption or empty ownership");
    }
}
bool source_identity_less(const ActivationSource& left, const ActivationSource& right) {
    if (source_kind(left) != source_kind(right)) return source_kind(left) < source_kind(right);
    return artifact::path_bytes_less(source_identity(left), source_identity(right));
}
std::string serialize(const WitnessReference& witness) {
    d::generation(witness.entry_id);
    return std::visit([&](const auto& id) {
        id.validate();
        using T = std::decay_t<decltype(id)>;
        const auto prefix = std::is_same_v<T, ActivationId> ? "activation:" : "adoption:";
        return std::string(prefix) + id.value() + ":" + std::to_string(witness.entry_id);
    }, witness.owner);
}
WitnessReference parse_witness_reference(std::string_view text) {
    const bool activation = text.starts_with("activation:");
    if (!activation && !text.starts_with("adoption:")) throw SchemaError(SchemaErrorCode::Identity, "Invalid witness namespace");
    text.remove_prefix(activation ? 11 : 9);
    if (text.size() < 33 || text[32] != ':') throw SchemaError(SchemaErrorCode::Identity, "Invalid witness reference");
    const auto entry = d::decimal(text.substr(33));
    const std::string id(text.substr(0, 32));
    if (activation) return {ActivationId(id), entry};
    return {AdoptionId(id), entry};
}
bool operation_less(const ActivationOperation& left, const ActivationOperation& right) {
    const auto group = operation_group(left);
    if (group != operation_group(right)) return group < operation_group(right);
    if (group != 2) {
        const auto ld = std::count(left.path.begin(), left.path.end(), '/');
        const auto rd = std::count(right.path.begin(), right.path.end(), '/');
        if (ld != rd) return group == 0 ? ld > rd : ld < rd;
    }
    return artifact::path_bytes_less(left.path, right.path);
}
void ActivationPlan::validate() const {
    activation_id.validate();
    if (sources.size() > max_sources || operations.size() > max_operations)
        throw SchemaError(SchemaErrorCode::Bounds, "Plan exceeds count bound");
    for (std::size_t i = 0; i < sources.size(); ++i) {
        validate_source(sources[i]);
        if (i && !source_identity_less(sources[i - 1], sources[i]))
            throw SchemaError(SchemaErrorCode::Ordering, "Duplicate/unsorted source identities");
    }
    std::set<std::string_view, ByteLess> old_links, removed_links, desired_links, directories;
    auto source = [&](std::uint64_t index) {
        if (index >= sources.size()) throw SchemaError(SchemaErrorCode::Reference, "Source index out of range");
    };
    auto insert = [](auto& paths, const std::string& path) {
        if (!paths.insert(path).second) throw SchemaError(SchemaErrorCode::Conflict, "Duplicate operation path");
    };
    for (std::size_t i = 0; i < operations.size(); ++i) {
        const auto& op = operations[i];
        if (op.id != i) throw SchemaError(SchemaErrorCode::Ordering, "Noncontiguous operation IDs");
        validate_relative_path(op.path);
        if (op.path == ".athena-activation" || op.path.starts_with(".athena-activation/"))
            throw SchemaError(SchemaErrorCode::Conflict, "Reserved projection administration path");
        if (i && !operation_less(operations[i - 1], op))
            throw SchemaError(SchemaErrorCode::Ordering, "Noncanonical operation ordering");
        std::visit([&](const auto& state) {
            using T = std::decay_t<decltype(state)>;
            if constexpr (std::is_same_v<T, EnsureDirectory>) insert(directories, op.path);
            else {
                if constexpr (!std::is_same_v<T, CreateLink>) {
                    source(state.old_source); (void)serialize(state.old_witness); insert(old_links, op.path);
                }
                if constexpr (std::is_same_v<T, RemoveLink>) insert(removed_links, op.path);
                if constexpr (!std::is_same_v<T, RemoveLink>) {
                    source(state.new_source); (void)serialize(state.new_witness); insert(desired_links, op.path);
                    const auto* owner = std::get_if<ActivationId>(&state.new_witness.owner);
                    if (!owner || *owner != activation_id || state.new_witness.entry_id != op.id)
                        throw SchemaError(SchemaErrorCode::Reference, "New witness does not belong to this operation");
                }
                if constexpr (std::is_same_v<T, KeepLink>)
                    if (state.old_source != state.new_source)
                        throw SchemaError(SchemaErrorCode::Reference, "KEEP requires unchanged source identity");
            }
        }, op.state);
    }
    for (const auto& path : desired_links) {
        if (directories.contains(path)) throw SchemaError(SchemaErrorCode::Conflict, "Desired link/directory collision");
        if (removed_links.contains(path))
            throw SchemaError(SchemaErrorCode::Conflict, "Use REPLACE rather than REMOVE/CREATE");
    }
    auto check_ancestors = [&](std::string_view path) {
        for (auto end = path.find('/'); end != path.npos; end = path.find('/', end + 1))
            if (desired_links.contains(path.substr(0, end)))
                throw SchemaError(SchemaErrorCode::Conflict, "Desired link has a desired descendant");
    };
    for (const auto& path : desired_links) check_ancestors(path);
    for (const auto& path : directories) check_ancestors(path);
}

std::string serialize(const ActivationIntent& intent) {
    intent.validate();
    d::Writer out;
    out.number("schema_version", 1); out.text("activation_id", intent.activation_id.value());
    out.text("generation_authority_hex", encode_hex(intent.generation_authority));
    out.text("store_authority_hex", encode_hex(intent.store_authority));
    out.text("projection_root_hex", encode_hex(intent.projection_root));
    out.text("projection_id", intent.projection_id.value());
    out.number("target_generation", intent.target_generation);
    out.text("target_generation_digest", intent.target_generation_digest.value());
    out.append("\n[base]\n");
    if (const auto* selected = std::get_if<SelectedBase>(&intent.base)) {
        out.text("format", std::holds_alternative<CurrentV1>(selected->current) ? "v1" : "decimal");
        out.text("record_hex", encode_hex(serialize(selected->current)));
        out.number("generation", selected_generation(selected->current));
        out.text("generation_digest", selected->generation_digest.value());
        out.text("ownership", ownership_text(selected->ownership));
    } else out.text("format", "absent");
    return out.bytes();
}
ActivationIntent parse_activation_intent(std::string_view bytes, const std::optional<ActivationId>& expected_id) {
    const auto table = d::parse(bytes, max_document_size, parsing::Profile::Intent);
    d::keys(table, {"schema_version", "activation_id", "generation_authority_hex", "store_authority_hex",
        "projection_root_hex", "projection_id", "target_generation", "target_generation_digest", "base"});
    d::version(table);
    const auto* base = table["base"].as_table();
    if (!base) throw SchemaError(SchemaErrorCode::Schema, "Expected base table");
    const auto format = d::string(*base, "format");
    ActivationBase parsed_base = AbsentBase{};
    if (format == "absent") d::keys(*base, {"format"});
    else {
        if (format != "v1" && format != "decimal") throw SchemaError(SchemaErrorCode::Schema, "Unknown base format");
        d::keys(*base, {"format", "record_hex", "generation", "generation_digest", "ownership"});
        auto current = parse_current_selection(decode_hex(d::string(*base, "record_hex"), max_current_size));
        if ((format == "v1") != std::holds_alternative<CurrentV1>(current) ||
            selected_generation(current) != d::integer(*base, "generation"))
            throw SchemaError(SchemaErrorCode::Reference, "Base record format/generation mismatch");
        parsed_base = SelectedBase{std::move(current), GenerationDigest(d::string(*base, "generation_digest")),
            parse_ownership(d::string(*base, "ownership"))};
    }
    ActivationIntent result{ActivationId(d::string(table, "activation_id")),
        decode_hex(d::string(table, "generation_authority_hex"), max_path_size),
        decode_hex(d::string(table, "store_authority_hex"), max_path_size),
        decode_hex(d::string(table, "projection_root_hex"), max_path_size),
        ProjectionId(d::string(table, "projection_id")), d::integer(table, "target_generation"),
        GenerationDigest(d::string(table, "target_generation_digest")), std::move(parsed_base)};
    expected(result.activation_id, expected_id);
    d::canonical(bytes, serialize(result));
    return result;
}
std::string serialize(const ActivationPlan& plan) {
    plan.validate();
    d::Writer out;
    out.number("schema_version", 1); out.text("activation_id", plan.activation_id.value());
    // Root-level empty arrays must precede all array-of-table headers.
    if (plan.sources.empty()) out.append("sources = []\n");
    if (plan.operations.empty()) out.append("operations = []\n");
    for (const auto& source : plan.sources) {
        out.append("\n[[sources]]\n"); out.text("kind", source_kind(source));
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ArtifactSource>) out.text("artifact_id", value.artifact_id.value());
            else {
                out.text("locator_hex", encode_hex(value.locator));
                if constexpr (std::is_same_v<T, LegacyAliasSource>) {
                    out.text("alias_target_hex", encode_hex(value.alias_target));
                    out.text("artifact_id", value.artifact_id.value());
                }
                out.text("seal_digest", value.seal_digest.value());
            }
        }, source);
    }
    for (const auto& op : plan.operations) {
        out.append("\n[[operations]]\n"); out.number("id", op.id);
        out.text("kind", operation_kind(op.state)); out.text("path_hex", encode_hex(op.path));
        std::visit([&](const auto& state) {
            using T = std::decay_t<decltype(state)>;
            if constexpr (!std::is_same_v<T, EnsureDirectory>) {
                if constexpr (!std::is_same_v<T, CreateLink>) {
                    out.number("old_source", state.old_source); out.text("old_witness", serialize(state.old_witness));
                }
                if constexpr (!std::is_same_v<T, RemoveLink>) {
                    out.number("new_source", state.new_source); out.text("new_witness", serialize(state.new_witness));
                }
            }
        }, op.state);
    }
    return out.bytes();
}
ActivationPlan parse_activation_plan(std::string_view bytes, const std::optional<ActivationId>& expected_id) {
    const auto table = d::parse(bytes, max_document_size, parsing::Profile::Plan);
    d::keys(table, {"schema_version", "activation_id", "sources", "operations"}); d::version(table);
    ActivationPlan result{ActivationId(d::string(table, "activation_id")), {}, {}};
    expected(result.activation_id, expected_id);
    const auto& sources = array(table, "sources", max_sources);
    const auto& operations = array(table, "operations", max_operations);
    result.sources.reserve(sources.size()); result.operations.reserve(operations.size());
    for (const auto& node : sources) {
        const auto& source = table_node(node);
        const auto kind = d::string(source, "kind");
        if (kind == "artifact") {
            d::keys(source, {"kind", "artifact_id"});
            result.sources.push_back(ArtifactSource{parse_artifact_id(source)});
        } else if (kind == "legacy_tree") {
            d::keys(source, {"kind", "locator_hex", "seal_digest"});
            result.sources.push_back(LegacyTreeSource{decode_hex(d::string(source, "locator_hex"), max_path_size), LegacySealDigest(d::string(source, "seal_digest"))});
        } else if (kind == "legacy_alias") {
            d::keys(source, {"kind", "locator_hex", "alias_target_hex", "artifact_id", "seal_digest"});
            result.sources.push_back(LegacyAliasSource{decode_hex(d::string(source, "locator_hex"), max_path_size),
                decode_hex(d::string(source, "alias_target_hex"), max_path_size), parse_artifact_id(source), LegacySealDigest(d::string(source, "seal_digest"))});
        } else throw SchemaError(SchemaErrorCode::Schema, "Unknown source kind");
    }
    for (const auto& node : operations) {
        const auto& op = table_node(node);
        const auto kind = d::string(op, "kind");
        auto old_witness = [&] { return parse_witness_reference(d::string(op, "old_witness")); };
        auto new_witness = [&] { return parse_witness_reference(d::string(op, "new_witness")); };
        OperationState state = EnsureDirectory{};
        if (kind == "create") {
            d::keys(op, {"id", "kind", "path_hex", "new_source", "new_witness"});
            state = CreateLink{d::integer(op, "new_source"), new_witness()};
        } else if (kind == "replace" || kind == "keep") {
            d::keys(op, {"id", "kind", "path_hex", "old_source", "old_witness", "new_source", "new_witness"});
            const auto old = d::integer(op, "old_source"), fresh = d::integer(op, "new_source");
            if (kind == "replace") state = ReplaceLink{old, old_witness(), fresh, new_witness()};
            else state = KeepLink{old, old_witness(), fresh, new_witness()};
        } else if (kind == "remove") {
            d::keys(op, {"id", "kind", "path_hex", "old_source", "old_witness"});
            state = RemoveLink{d::integer(op, "old_source"), old_witness()};
        } else if (kind == "ensure_directory") d::keys(op, {"id", "kind", "path_hex"});
        else throw SchemaError(SchemaErrorCode::Schema, "Unknown operation kind");
        result.operations.push_back({d::integer(op, "id"), decode_hex(d::string(op, "path_hex"), max_path_size), std::move(state)});
    }
    d::canonical(bytes, serialize(result));
    return result;
}
std::string serialize(const ActivationStatus& status) {
    status.activation_id.validate(); status.plan_digest.validate();
    if (status.outcome != TerminalOutcome::Complete && status.outcome != TerminalOutcome::Aborted)
        throw SchemaError(SchemaErrorCode::Schema, "Unknown terminal outcome");
    d::Writer out;
    out.number("schema_version", 1); out.text("activation_id", status.activation_id.value());
    out.text("plan_digest", status.plan_digest.value());
    out.text("outcome", status.outcome == TerminalOutcome::Complete ? "complete" : "aborted");
    return out.bytes();
}
ActivationStatus parse_activation_status(std::string_view bytes, const std::optional<ActivationId>& expected_id) {
    const auto table = d::parse(bytes, max_document_size, parsing::Profile::Status);
    d::keys(table, {"schema_version", "activation_id", "plan_digest", "outcome"}); d::version(table);
    const auto outcome = d::string(table, "outcome");
    if (outcome != "complete" && outcome != "aborted") throw SchemaError(SchemaErrorCode::Schema, "Unknown terminal outcome");
    ActivationStatus result{ActivationId(d::string(table, "activation_id")), PlanDigest(d::string(table, "plan_digest")),
        outcome == "complete" ? TerminalOutcome::Complete : TerminalOutcome::Aborted};
    expected(result.activation_id, expected_id); d::canonical(bytes, serialize(result)); return result;
}
PlanDigest derive_plan_digest(const ActivationIntent& intent, const ActivationPlan& plan) {
    if (intent.activation_id != plan.activation_id) throw SchemaError(SchemaErrorCode::Reference, "Intent/plan activation IDs differ");
    const auto intent_bytes = serialize(intent), plan_bytes = serialize(plan);
    // Bind old witness claims to the immutable base's ownership namespace.
    // Existence/inode verification remains the responsibility of later phases.
    for (const auto& op : plan.operations) std::visit([&](const auto& state) {
        if constexpr (requires { state.old_witness; }) {
            const auto* base = std::get_if<SelectedBase>(&intent.base);
            if (!base) throw SchemaError(SchemaErrorCode::Reference, "Old witness requires selected base ownership");
            const bool matches = std::visit([&](const auto& ownership) {
                using T = std::decay_t<decltype(ownership)>;
                if constexpr (std::is_same_v<T, ActivationOwnership>) {
                    const auto* owner = std::get_if<ActivationId>(&state.old_witness.owner);
                    return owner && *owner == ownership.id;
                } else if constexpr (std::is_same_v<T, AdoptionOwnership>) {
                    const auto* owner = std::get_if<AdoptionId>(&state.old_witness.owner);
                    return owner && *owner == ownership.id;
                } else return false;
            }, base->ownership);
            if (!matches) throw SchemaError(SchemaErrorCode::Reference, "Old witness/base ownership mismatch");
        }
    }, op.state);
    std::string preimage("athena-activation-plan-v1\0", sizeof("athena-activation-plan-v1\0") - 1);
    d::append_u64(preimage, intent_bytes.size()); preimage.append(intent_bytes);
    d::append_u64(preimage, plan_bytes.size()); preimage.append(plan_bytes);
    return PlanDigest(hash::sha256(preimage));
}
PlanDigest digest_plan_documents(std::string_view intent, std::string_view activation) {
    return derive_plan_digest(parse_activation_intent(intent), parse_activation_plan(activation));
}
}
