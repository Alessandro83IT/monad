// SPDX-License-Identifier: GPL-3.0-or-later
#include "legacy.hpp"
#include "codec_detail.hpp"
#include <set>
namespace athena::activation {
namespace c = codec_detail;
namespace {
void source(const ActivationSource& s) {
    std::visit([](const auto& v) {
        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, ArtifactSource>) {
            if (!sha256_text(v.artifact_id.value())) throw SchemaError(SchemaErrorCode::Identity, "Invalid artifact identity");
        } else {
        validate_absolute_locator(v.locator); v.seal_digest.validate();
        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, LegacyAliasSource>) {
            if (v.alias_target.empty() || v.alias_target.size() > max_path_size || v.alias_target.find('\0') != v.alias_target.npos)
                throw SchemaError(SchemaErrorCode::Path, "Invalid alias literal");
            if (!sha256_text(v.artifact_id.value()))
                throw SchemaError(SchemaErrorCode::Identity, "Expected v1 artifact identity");
        }
        }
    }, s);
}
}
std::string adoption_source_locator(const ProjectionBinding& b, const ActivationSource& s) {
    return std::visit([&](const auto& v) {
        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, ArtifactSource>)
            return b.store_authority + "/objects/v1/sha256/" + v.artifact_id.value().substr(7);
        else return v.locator;
    }, s);
}
void AdoptionRecord::validate() const {
    id.validate(); binding.validate(); generation_digest.validate();
    (void)serialize(CurrentSelection{current});
    if (sources.size() > max_sources || entries.size() > max_operations || directories.size() > max_operations)
        throw SchemaError(SchemaErrorCode::Bounds, "Adoption inventory bound");
    std::set<std::string> locators;
    for (const auto& s : sources) { source(s); if (!locators.insert(adoption_source_locator(binding, s)).second)
        throw SchemaError(SchemaErrorCode::Conflict, "Duplicate legacy source"); }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i]; validate_projection_path(e.path);
        if (e.id != i || e.source >= sources.size() ||
            e.witness != WitnessReference{id, i} || e.target != adoption_source_locator(binding, sources[e.source]) + '/' + e.path ||
            e.target.size() > max_path_size ||
            (i && !artifact::path_bytes_less(entries[i - 1].path, e.path)))
            throw SchemaError(SchemaErrorCode::Reference, "Invalid adoption entry identity/order/destination");
    }
    std::set<std::string> link_paths;
    for (const auto& e : entries) link_paths.insert(e.path);
    std::set<std::string> directory_paths(directories.begin(), directories.end());
    for (const auto& e : entries) {
        auto path = e.path;
        for (auto slash = path.rfind('/'); slash != path.npos; slash = path.rfind('/')) {
            path.resize(slash);
            if (!directory_paths.contains(path)) throw SchemaError(SchemaErrorCode::Reference, "Missing expected projection ancestor");
        }
    }
    for (std::size_t i = 0; i < directories.size(); ++i) {
        validate_projection_path(directories[i]);
        if (i && !artifact::path_bytes_less(directories[i - 1], directories[i]))
            throw SchemaError(SchemaErrorCode::Ordering, "Unsorted/duplicate adoption directories");
        auto path = directories[i];
        for (;;) {
            if (link_paths.contains(path)) throw SchemaError(SchemaErrorCode::Conflict, "Directory descends through adopted link");
            const auto slash = path.rfind('/'); if (slash == path.npos) break; path.resize(slash);
        }
    }
}
std::string serialize(const AdoptionRecord& r) {
    r.validate(); c::Writer w;
    w.number("schema_version", 1); w.text("adoption_id", r.id.value());
    w.text("projection_id", r.binding.projection_id.value());
    w.text("generation_authority_hex", encode_hex(r.binding.generation_authority));
    w.text("store_authority_hex", encode_hex(r.binding.store_authority));
    w.text("projection_root_hex", encode_hex(r.binding.projection_root));
    w.number("generation", r.current.generation);
    w.text("historical_current_hex", encode_hex(r.current.bytes));
    w.text("generation_digest", r.generation_digest.value());
    w.append("directories_hex = [\n");
    for (const auto& path : r.directories) { w.append("    \""); w.append(encode_hex(path)); w.append("\",\n"); }
    w.append("]\n");
    if (r.sources.empty()) w.append("sources = []\n");
    if (r.entries.empty()) w.append("entries = []\n");
    for (const auto& s : r.sources) {
        w.append("\n[[sources]]\n");
        std::visit([&](const auto& v) {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, ArtifactSource>) {
                w.text("kind", "artifact"); w.text("artifact_id", v.artifact_id.value());
            } else {
            constexpr bool alias = std::is_same_v<std::decay_t<decltype(v)>, LegacyAliasSource>;
            w.text("kind", alias ? "legacy_alias" : "legacy_tree");
            w.text("locator_hex", encode_hex(v.locator));
            if constexpr (alias) { w.text("alias_target_hex", encode_hex(v.alias_target)); w.text("artifact_id", v.artifact_id.value()); }
            w.text("seal_digest", v.seal_digest.value());
            }
        }, s);
    }
    for (const auto& e : r.entries) {
        w.append("\n[[entries]]\n"); w.number("id", e.id); w.number("source", e.source);
        w.text("path_hex", encode_hex(e.path)); w.text("target_hex", encode_hex(e.target));
        w.text("witness", serialize(e.witness));
    }
    return w.bytes();
}
AdoptionRecord parse_adoption_record(std::string_view bytes) {
    auto t = c::parse(bytes, max_document_size, parsing::Profile::Adoption); c::version(t);
    c::keys(t, {"schema_version", "adoption_id", "projection_id", "generation_authority_hex", "store_authority_hex",
        "projection_root_hex", "generation", "historical_current_hex", "generation_digest", "directories_hex", "sources", "entries"});
    auto path = [&](std::string_view key) { return decode_hex(c::string(t, key), max_path_size); };
    AdoptionRecord r{AdoptionId(c::string(t, "adoption_id")),
        {ProjectionId(c::string(t, "projection_id")), path("generation_authority_hex"), path("store_authority_hex"), path("projection_root_hex")},
        {c::integer(t, "generation"), decode_hex(c::string(t, "historical_current_hex"), max_current_size)},
        generation::GenerationDocumentDigest(c::string(t, "generation_digest")), {}, {}};
    const auto* directories = t["directories_hex"].as_array();
    if (!directories || directories->size() > max_operations) throw SchemaError(SchemaErrorCode::Bounds, "Invalid directories array");
    for (const auto& node : *directories) {
        if (!node.is_string()) throw SchemaError(SchemaErrorCode::Schema, "Expected directory hex string");
        r.directories.push_back(decode_hex(*node.value<std::string>(), max_path_size));
    }
    const auto* sources = t["sources"].as_array(); const auto* entries = t["entries"].as_array();
    if (!sources || !entries || sources->size() > max_sources || entries->size() > max_operations)
        throw SchemaError(SchemaErrorCode::Bounds, "Invalid adoption arrays");
    for (const auto& node : *sources) {
        const auto* s = node.as_table(); if (!s) throw SchemaError(SchemaErrorCode::Schema, "Source table required");
        const auto kind = c::string(*s, "kind");
        if (kind == "artifact") {
            c::keys(*s, {"kind", "artifact_id"}); r.sources.push_back(ArtifactSource{core::ArtifactId(c::string(*s, "artifact_id"))}); continue;
        }
        const auto loc = decode_hex(c::string(*s, "locator_hex"), max_path_size);
        const auto digest = LegacySealDigest(c::string(*s, "seal_digest"));
        if (kind == "legacy_tree") {
            c::keys(*s, {"kind", "locator_hex", "seal_digest"}); r.sources.push_back(LegacyTreeSource{loc, digest});
        } else if (kind == "legacy_alias") {
            c::keys(*s, {"kind", "locator_hex", "alias_target_hex", "artifact_id", "seal_digest"});
            r.sources.push_back(LegacyAliasSource{loc, decode_hex(c::string(*s, "alias_target_hex"), max_path_size),
                core::ArtifactId(c::string(*s, "artifact_id")), digest});
        } else throw SchemaError(SchemaErrorCode::Schema, "Unknown legacy source kind");
    }
    for (const auto& node : *entries) {
        const auto* e = node.as_table(); if (!e) throw SchemaError(SchemaErrorCode::Schema, "Entry table required");
        c::keys(*e, {"id", "source", "path_hex", "target_hex", "witness"});
        r.entries.push_back({c::integer(*e, "id"), c::integer(*e, "source"), decode_hex(c::string(*e, "path_hex"), max_path_size),
            decode_hex(c::string(*e, "target_hex"), max_path_size), parse_witness_reference(c::string(*e, "witness"))});
    }
    r.validate(); c::canonical(bytes, serialize(r)); return r;
}
}
