// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "record.hpp"
#include "identity_codec.hpp"
#include "../core/resolution_order.hpp"
#include <algorithm>
#include <locale>
#include <sstream>
#include <toml++/toml.hpp>

namespace athena::realization {
using namespace core;
namespace {
void digest(const std::string& text) {
    if (text.size() != 71 || !text.starts_with("sha256:") ||
        !std::all_of(text.begin() + 7, text.end(), [](unsigned char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        })) throw TypeError("Expected full lowercase sha256 identity");
}
void keys(const toml::table& t, std::initializer_list<std::string_view> allowed) {
    for (const auto& [k, v] : t) {
        (void)v;
        if (std::find(allowed.begin(), allowed.end(), k.str()) == allowed.end())
            throw TypeError("Unknown realization field: " + std::string(k.str()));
    }
}
std::string string(const toml::table& t, const char* k) {
    auto v = t[k].value<std::string>();
    if (!v || v->empty()) throw TypeError(std::string("Missing/invalid realization field: ") + k);
    return *v;
}
std::optional<std::string> optional(const toml::table& t, const char* k) {
    if (!t.contains(k)) return {};
    return string(t, k);
}
const toml::table& table(const toml::table& t, const char* k) {
    auto* v = t[k].as_table();
    if (!v) throw TypeError(std::string("Missing/invalid realization table: ") + k);
    return *v;
}
void version(const toml::table& t) {
    if (!t["schema_version"].is_integer() || t["schema_version"].value<int>() != 1)
        throw TypeError("Unsupported snapshot schema version");
}
toml::table snapshot_table(const RepositorySnapshotRef& s) {
    toml::table t{{"repository_id", s.repository_id.value()}, {"revision", s.revision.value()},
                  {"schema_version", s.schema_version.value}};
    if (s.metadata_digest) t.insert("metadata_digest", s.metadata_digest->value());
    if (s.snapshot_id) t.insert("snapshot_id", s.snapshot_id->value());
    return t;
}
RepositorySnapshotRef snapshot(const toml::table& t) {
    keys(t, {"repository_id", "revision", "schema_version", "metadata_digest", "snapshot_id"});
    version(t);
    RepositorySnapshotRef s{RepositoryId(string(t, "repository_id")), RepositoryRevision(string(t, "revision"))};
    if (auto v = optional(t, "metadata_digest")) s.metadata_digest = ContentDigest(*v);
    if (auto v = optional(t, "snapshot_id")) s.snapshot_id = SnapshotId(*v);
    return s;
}
std::string origin_name(ArtifactOrigin o) {
    switch (o) {
    case ArtifactOrigin::BuiltFromAthenaIR: return "built_from_athena_ir";
    case ArtifactOrigin::AmbientSourceBuild: return "ambient_source_build";
    case ArtifactOrigin::ImportedBinary: return "imported_binary";
    }
    throw TypeError("Unknown realization origin");
}
ArtifactOrigin origin(const std::string& s) {
    for (auto o : {ArtifactOrigin::BuiltFromAthenaIR, ArtifactOrigin::AmbientSourceBuild, ArtifactOrigin::ImportedBinary})
        if (origin_name(o) == s) return o;
    throw TypeError("Unknown realization origin");
}
}
toml::table encode_candidate(const CandidateIdentity& c) {
    toml::table p{{"repository_id", c.provenance.repository_id.value()}};
    if (c.provenance.revision) p.insert("revision", c.provenance.revision->value());
    if (c.provenance.metadata_reference) p.insert("metadata_reference", c.provenance.metadata_reference->value());
    if (c.provenance.snapshot) p.insert("snapshot", snapshot_table(*c.provenance.snapshot));
    return toml::table{{"package", c.package.value()}, {"version", c.version.value()}, {"repository", c.repository.value()},
        {"snapshot", snapshot_table(c.snapshot)}, {"provenance", std::move(p)}};
}
CandidateIdentity decode_candidate(const toml::table& c) {
    keys(c, {"package", "version", "repository", "snapshot", "provenance"});
    const auto& p = table(c, "provenance");
    keys(p, {"repository_id", "revision", "metadata_reference", "snapshot"});
    RepositoryProvenance provenance{RepositoryId(string(p, "repository_id"))};
    if (auto v = optional(p, "revision")) provenance.revision = RepositoryRevision(*v);
    if (auto v = optional(p, "metadata_reference")) provenance.metadata_reference = MetadataReference(*v);
    if (p.contains("snapshot")) provenance.snapshot = snapshot(table(p, "snapshot"));
    return {PackageId(string(c, "package")), PackageVersion(string(c, "version")),
        RepositoryId(string(c, "repository")), snapshot(table(c, "snapshot")), std::move(provenance)};
}
void validate_candidate(const CandidateIdentity& candidate) {
    // Reuse package/version and repository validators without a candidate copy.
    PackageMetadata{candidate.package, candidate.version}.validate();
    candidate.snapshot.validate();
    candidate.provenance.validate();
    if (candidate.snapshot.revision.value().empty() ||
        (candidate.provenance.revision && candidate.provenance.revision->value().empty()) ||
        (candidate.provenance.metadata_reference && candidate.provenance.metadata_reference->value().empty()))
        throw TypeError("Empty realization candidate identity field");
    if (candidate.repository.empty() || candidate.repository != candidate.snapshot.repository_id ||
        candidate.repository != candidate.provenance.repository_id || !candidate.snapshot.snapshot_id ||
        (candidate.provenance.revision && *candidate.provenance.revision != candidate.snapshot.revision) ||
        (candidate.provenance.snapshot && *candidate.provenance.snapshot != candidate.snapshot))
        throw TypeError("Contradictory or incomplete realization candidate identity");
}
void RealizationRecord::validate() const {
    validate_candidate(candidate);
    digest(artifact_id.value());
    if (build_input_id) digest(build_input_id->value());
    (void)origin_name(origin);
    if ((origin == ArtifactOrigin::BuiltFromAthenaIR) != build_input_id.has_value())
        throw TypeError("Invalid realization origin/BuildInputId combination");
    if (semantics) {
        semantics->validate();
        if (semantics->legacy_restricted || semantics->package.package_id != candidate.package ||
            semantics->package.version != candidate.version)
            throw TypeError("Realization semantic identity mismatch");
    }
}
std::string RealizationRecord::object_reference() const {
    digest(artifact_id.value());
    return "objects/v1/sha256/" + artifact_id.value().substr(7);
}
bool RealizationRecord::operator<(const RealizationRecord& other) const {
    auto key = [](const RealizationRecord& r) {
        auto k = core::detail::key(r.candidate);
        core::detail::append(k, {r.artifact_id.value(), r.build_input_id ? "1" : "0",
            r.build_input_id ? r.build_input_id->value() : "", origin_name(r.origin)});
        core::detail::append(k, {r.semantics ? "1" : "0", r.semantics ? installed::serialize(*r.semantics) : ""});
        return k;
    };
    return key(*this) < key(other);
}
std::string serialize(const RealizationRecord& r) {
    r.validate();
    toml::table t{{"schema_version", r.semantics ? 2 : 1}, {"artifact_id", r.artifact_id.value()},
        {"origin", origin_name(r.origin)}, {"candidate", encode_candidate(r.candidate)}};
    if (r.semantics) t.insert("semantics", toml::parse(installed::serialize(*r.semantics)));
    if (r.build_input_id) t.insert("build_input_id", r.build_input_id->value());
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << t;
    return out.str();
}
RealizationRecord deserialize_record(const std::string& document) {
    try {
        auto t = toml::parse(document);
        keys(t, {"schema_version", "artifact_id", "origin", "candidate", "build_input_id", "semantics"});
        const auto v = t["schema_version"].value<int>();
        if (!t["schema_version"].is_integer() || (v != 1 && v != 2)) throw TypeError("Unsupported realization schema version");
        if ((v == 2) != t.contains("semantics")) throw TypeError("Missing/unexpected realization semantics");
        RealizationRecord r{decode_candidate(table(t, "candidate")),
            ArtifactId(string(t, "artifact_id")), {}, origin(string(t, "origin"))};
        if (v == 2) {
            std::ostringstream document; document.imbue(std::locale::classic()); document << table(t, "semantics");
            r.semantics = installed::deserialize_semantics(document.str());
        }
        if (auto v = optional(t, "build_input_id")) r.build_input_id = BuildInputId(*v);
        r.validate();
        return r;
    } catch (const toml::parse_error& e) { throw TypeError(e.what()); }
}
}
