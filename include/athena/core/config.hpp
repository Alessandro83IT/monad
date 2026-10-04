#pragma once

#include "path_layout.hpp"
#include "schema.hpp"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace athena::core {

enum class ConfigSource { Default, System, User, ExplicitFile, CommandLine };
std::string config_source_name(ConfigSource source);

struct ConfigError {
    std::string code;
    std::string message;
    std::string source;
};

struct ConfigResult {
    bool ok = false;
    ConfigError error;
    explicit operator bool() const noexcept { return ok; }
};

struct ConfigOverrides {
    std::optional<std::filesystem::path> root;
    std::optional<std::filesystem::path> target_root;
    std::optional<std::filesystem::path> store;
    std::optional<std::filesystem::path> state;
    std::optional<std::filesystem::path> cache;
    std::optional<std::filesystem::path> build;
};

struct ConfigLoadOptions {
    std::optional<std::filesystem::path> explicit_config;
    ConfigOverrides overrides;
    std::filesystem::path system_config = "/etc/athena/athena.toml";
    std::optional<std::filesystem::path> user_config;
};

struct Config {
    SchemaVersion schema_version{};
    PathLayout paths;
    std::map<std::string, ConfigSource> provenance;
};

struct ConfigLoadResult {
    std::optional<Config> config;
    ConfigError error;
    explicit operator bool() const noexcept { return config.has_value(); }
};

class ConfigLoader {
public:
    ConfigLoadResult load(const ConfigLoadOptions& options = {}) const noexcept;
    static std::filesystem::path default_user_config_path();
};

} // namespace athena::core
