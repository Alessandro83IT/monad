#include "../../include/athena/core/config.hpp"
#include <toml++/toml.hpp>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace athena::core {
namespace {
std::filesystem::path absolute_normal(std::filesystem::path path)
{
    if (!path.is_absolute())
        throw std::invalid_argument("configured paths must be absolute: " + path.string());
    return path.lexically_normal();
}

void mark(Config& c, ConfigSource source)
{
    const auto label = config_source_name(source);
    for (const char* key : {"root", "store", "state", "cache", "build",
                            "repository_snapshots", "repository_state",
                            "generation_state", "target_root"})
        c.provenance[key] = source;
    (void)label;
}

void apply_file(Config& config, const std::filesystem::path& path, ConfigSource source)
{
    auto doc = toml::parse_file(path.string());
    int version = current_schema_version;
    if (const auto v = doc["schema_version"].value<int64_t>()) version = static_cast<int>(*v);
    validate_schema_version(SchemaKind::Configuration, version);
    const auto* paths = doc["paths"].as_table();
    if (!paths) throw std::invalid_argument("missing [paths] table in " + path.string());

    if (const auto root = (*paths)["root"].value<std::string>()) {
        config.paths = PathLayout::from_root(absolute_normal(*root));
        for (const char* key : {"root", "store", "state", "cache", "build",
                                "repository_snapshots", "repository_state",
                                "generation_state", "target_root"})
            config.provenance[key] = source;
    } else if ((*paths)["root"]) {
        throw std::invalid_argument("paths.root must be an absolute string");
    }
    auto read = [&](const char* key, std::filesystem::path& field) {
        if (const auto value = (*paths)[key].value<std::string>()) {
            field = absolute_normal(*value);
            config.provenance[key] = source;
        } else if ((*paths)[key]) {
            throw std::invalid_argument("paths." + std::string(key) + " must be an absolute string");
        }
    };
    read("root", config.paths.root);
    read("store", config.paths.store);
    read("state", config.paths.state);
    read("cache", config.paths.cache);
    read("build", config.paths.build);
    read("repository_snapshots", config.paths.repository_snapshots);
    read("repository_state", config.paths.repository_state);
    read("generation_state", config.paths.generation_state);
    read("target_root", config.paths.target_root);
    config.schema_version = SchemaVersion(version);
}

void apply_overrides(Config& c, const ConfigOverrides& o)
{
    auto set = [&](const auto& value, auto& field, const char* name) {
        if (value) { field = absolute_normal(*value); c.provenance[name] = ConfigSource::CommandLine; }
    };
    set(o.root, c.paths.root, "root");
    set(o.target_root, c.paths.target_root, "target_root");
    set(o.store, c.paths.store, "store");
    set(o.state, c.paths.state, "state");
    set(o.cache, c.paths.cache, "cache");
    set(o.build, c.paths.build, "build");
    if (o.root) {
        const auto derived = PathLayout::from_root(c.paths.root);
        if (!o.store) c.paths.store = derived.store;
        if (!o.state) c.paths.state = derived.state;
        if (!o.cache) c.paths.cache = derived.cache;
        if (!o.build) c.paths.build = derived.build;
        c.paths.repository_snapshots = derived.repository_snapshots;
        c.paths.repository_state = derived.repository_state;
        c.paths.generation_state = derived.generation_state;
        if (!o.target_root) c.paths.target_root = derived.target_root;
        if (!o.store) c.provenance["store"] = ConfigSource::CommandLine;
        if (!o.state) c.provenance["state"] = ConfigSource::CommandLine;
        if (!o.cache) c.provenance["cache"] = ConfigSource::CommandLine;
        if (!o.build) c.provenance["build"] = ConfigSource::CommandLine;
        c.provenance["repository_snapshots"] = ConfigSource::CommandLine;
        c.provenance["repository_state"] = ConfigSource::CommandLine;
        c.provenance["generation_state"] = ConfigSource::CommandLine;
        if (!o.target_root) c.provenance["target_root"] = ConfigSource::CommandLine;
    }
    c.paths.validate();
}
} // namespace

std::string config_source_name(ConfigSource source)
{
    switch (source) {
    case ConfigSource::Default: return "default";
    case ConfigSource::System: return "system";
    case ConfigSource::User: return "user";
    case ConfigSource::ExplicitFile: return "explicit-config";
    case ConfigSource::CommandLine: return "command-line";
    }
    return "unknown";
}

std::filesystem::path ConfigLoader::default_user_config_path()
{
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / "athena" / "config.toml";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".config" / "athena" / "config.toml";
    return {};
}

ConfigLoadResult ConfigLoader::load(const ConfigLoadOptions& options) const noexcept
{
    try {
        Config c;
        c.paths = PathLayout::from_root("/var/lib/athena");
        mark(c, ConfigSource::Default);
        auto optional_file = [&](const std::filesystem::path& p, ConfigSource source) {
            if (p.empty() || !std::filesystem::exists(p)) return;
            apply_file(c, p, source);
        };
        optional_file(options.system_config, ConfigSource::System);
        const auto user = options.user_config.value_or(default_user_config_path());
        optional_file(user, ConfigSource::User);
        if (options.explicit_config) {
            if (!std::filesystem::is_regular_file(*options.explicit_config))
                throw std::invalid_argument("explicit configuration file does not exist or is not a regular file: " + options.explicit_config->string());
            apply_file(c, *options.explicit_config, ConfigSource::ExplicitFile);
        }
        apply_overrides(c, options.overrides);
        return {std::move(c), {}};
    } catch (const std::exception& e) {
        return {std::nullopt, {"config_invalid", e.what(), "configuration"}};
    }
}

} // namespace athena::core
