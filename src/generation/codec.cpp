// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../parsing/bounded_toml.hpp"
#include "generation.hpp"
#include <algorithm>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <athena_toml/athena_toml.hpp>

namespace athena::generation {
namespace {
void keys(const athena_toml::table& t, std::initializer_list<std::string_view> allowed) {
    for (const auto& [k, v] : t) {
        (void)v;
        if (std::find(allowed.begin(), allowed.end(), k.str()) == allowed.end())
            throw core::TypeError("Unknown generation field: " + std::string(k.str()));
    }
}
std::string string(const athena_toml::table& t, const char* key) {
    auto value = t[key].value<std::string>();
    if (!value) throw core::TypeError(std::string("Invalid generation field: ") + key);
    return *value;
}
std::string format(const athena_toml::table& t) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << t;
    return out.str();
}
void legacy(const GenerationEntry& entry) {
    // Preserve historical string semantics, including relative paths. No I/O.
    const auto path = std::filesystem::path(entry.store_path).lexically_normal();
    auto it = path.begin();
    for (; it != path.end(); ++it) {
        if (*it != "objects") continue;
        auto next = it;
        if (++next != path.end() && *next == "v1" &&
            ++next != path.end() && *next == "sha256")
            throw core::TypeError("ObjectStore namespace cannot be a legacy reference");
    }
    if (entry.store_path.starts_with("sha256:"))
        throw core::TypeError("ArtifactId cannot be a legacy store path");
}
}
void Generation::validate() const {
    if (id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        throw core::TypeError("Generation identifier exceeds TOML integer range");
    std::set<std::string> names;
    for (const auto& entry : entries) {
        legacy(entry);
        names.insert(entry.package);
        if (entry.semantics) {
            entry.semantics->validate();
            if (!entry.semantics->legacy_restricted || entry.semantics->package.package_id.value() != entry.package)
                throw core::TypeError("Legacy semantic identity mismatch");
        }
    }
    for (const auto& record : realizations) {
        record.validate();
        if (!names.insert(record.candidate.package.value()).second)
            throw core::TypeError("Duplicate generation package selection");
    }
    for (const auto& policy : policies) (void)installed::serialize(policy);
}
Generation make_generation(std::uint64_t id, std::vector<GenerationEntry> legacy,
    std::vector<realization::RealizationRecord> records, std::vector<installed::PolicyRequirement> policies) {
    Generation result{id, std::move(legacy), std::move(records), std::move(policies)};
    result.validate();
    std::sort(result.realizations.begin(), result.realizations.end());
    return result;
}
std::string serialize(const Generation& generation) {
    auto g = make_generation(generation.id, generation.entries, generation.realizations, generation.policies);
    athena_toml::array members, policies;
    for (const auto& entry : g.entries) {
        athena_toml::table member{{"kind", "legacy"}, {"package", entry.package}, {"store", entry.store_path}};
        if (entry.semantics) member.insert("semantics", parsing::parse_model(installed::serialize(*entry.semantics), parsing::Profile::Semantics));
        members.push_back(std::move(member));
    }
    for (const auto& record : g.realizations)
        members.push_back(athena_toml::table{{"kind", "realization"},
            {"record", parsing::parse_model(realization::serialize(record), parsing::Profile::Record)}});
    for (const auto& policy : g.policies) policies.push_back(parsing::parse_model(installed::serialize(policy), parsing::Profile::Policy));
    return format(athena_toml::table{{"schema_version", 3}, {"id", static_cast<std::int64_t>(g.id)},
        {"memberships", std::move(members)}, {"policies", std::move(policies)}});
}
Generation deserialize_generation(const std::string& document) {
    try {
        auto t = parsing::parse_model(document, parsing::Profile::Generation);
        const bool modern = t.contains("schema_version");
        const auto version = t["schema_version"].value<std::int64_t>();
        if (modern && (!t["schema_version"].is_integer() || (version != 2 && version != 3)))
            throw core::TypeError("Unsupported generation schema version");
        if (version == 3) keys(t, {"schema_version", "id", "memberships", "policies"});
        else if (modern) keys(t, {"schema_version", "id", "memberships"});
        else keys(t, {"id", "entries"});
        auto id = t["id"].value<std::int64_t>();
        if (!t["id"].is_integer() || !id || *id < 0) throw core::TypeError("Invalid generation identifier");
        Generation g{static_cast<std::uint64_t>(*id), {}};
        const char* field = modern ? "memberships" : "entries";
        const auto* members = t[field].as_array();
        if (!members && (modern || t.contains(field)))
            throw core::TypeError("Invalid generation membership array");
        if (members) for (const auto& item : *members) {
            const auto* member = item.as_table();
            if (!member) throw core::TypeError("Invalid generation membership table");
            const auto kind = modern ? string(*member, "kind") : "legacy";
            if (kind == "legacy") {
                if (version == 3) keys(*member, {"kind", "package", "store", "semantics"});
                else if (modern) keys(*member, {"kind", "package", "store"});
                else keys(*member, {"package", "store"});
                g.entries.push_back({string(*member, "package"), string(*member, "store")});
                if (member->contains("semantics")) {
                    const auto* s = (*member)["semantics"].as_table();
                    if (!s) throw core::TypeError("Invalid legacy semantic table");
                    g.entries.back().semantics = installed::deserialize_semantics(format(*s));
                }
            } else if (kind == "realization") {
                keys(*member, {"kind", "record"});
                const auto* record = (*member)["record"].as_table();
                if (!record) throw core::TypeError("Invalid realization membership record");
                g.realizations.push_back(realization::deserialize_record(format(*record)));
            } else throw core::TypeError("Unknown generation membership kind");
        }
        if (version == 3) {
            const auto* policies = t["policies"].as_array();
            if (!policies) throw core::TypeError("Invalid installed policy array");
            for (const auto& item : *policies) {
                const auto* p = item.as_table();
                if (!p) throw core::TypeError("Invalid installed policy table");
                g.policies.push_back(installed::deserialize_policy(format(*p)));
            }
        }
        auto result = make_generation(g.id, std::move(g.entries), std::move(g.realizations), std::move(g.policies));
        result.compatibility = version == 3 ? GenerationCompatibility::Modern : GenerationCompatibility::Historical;
        return result;
    } catch (const athena_toml::parse_error& error) { throw core::TypeError(error.what()); }
}
}
