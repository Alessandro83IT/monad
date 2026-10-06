#include "generation.hpp"
#include <algorithm>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <toml++/toml.hpp>

namespace athena::generation {
namespace {
void keys(const toml::table& t, std::initializer_list<std::string_view> allowed) {
    for (const auto& [k, v] : t) {
        (void)v;
        if (std::find(allowed.begin(), allowed.end(), k.str()) == allowed.end())
            throw core::TypeError("Unknown generation field: " + std::string(k.str()));
    }
}
std::string string(const toml::table& t, const char* key) {
    auto value = t[key].value<std::string>();
    if (!value) throw core::TypeError(std::string("Invalid generation field: ") + key);
    return *value;
}
std::string format(const toml::table& t) {
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
    }
    for (const auto& record : realizations) {
        record.validate();
        if (!names.insert(record.candidate.package.value()).second)
            throw core::TypeError("Duplicate generation package selection");
    }
}
Generation make_generation(std::uint64_t id, std::vector<GenerationEntry> legacy,
    std::vector<realization::RealizationRecord> records) {
    Generation result{id, std::move(legacy), std::move(records)};
    result.validate();
    std::sort(result.realizations.begin(), result.realizations.end());
    return result;
}
std::string serialize(const Generation& generation) {
    auto g = make_generation(generation.id, generation.entries, generation.realizations);
    toml::array members;
    for (const auto& entry : g.entries)
        members.push_back(toml::table{{"kind", "legacy"}, {"package", entry.package}, {"store", entry.store_path}});
    for (const auto& record : g.realizations)
        members.push_back(toml::table{{"kind", "realization"},
            {"record", toml::parse(realization::serialize(record))}});
    return format(toml::table{{"schema_version", 2}, {"id", static_cast<std::int64_t>(g.id)},
        {"memberships", std::move(members)}});
}
Generation deserialize_generation(const std::string& document) {
    try {
        auto t = toml::parse(document);
        const bool modern = t.contains("schema_version");
        if (modern && (!t["schema_version"].is_integer() ||
            t["schema_version"].value<std::int64_t>() != 2))
            throw core::TypeError("Unsupported generation schema version");
        if (modern) keys(t, {"schema_version", "id", "memberships"});
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
                if (modern) keys(*member, {"kind", "package", "store"});
                else keys(*member, {"package", "store"});
                g.entries.push_back({string(*member, "package"), string(*member, "store")});
            } else if (kind == "realization") {
                keys(*member, {"kind", "record"});
                const auto* record = (*member)["record"].as_table();
                if (!record) throw core::TypeError("Invalid realization membership record");
                g.realizations.push_back(realization::deserialize_record(format(*record)));
            } else throw core::TypeError("Unknown generation membership kind");
        }
        return make_generation(g.id, std::move(g.entries), std::move(g.realizations));
    } catch (const toml::parse_error& error) { throw core::TypeError(error.what()); }
}
}
