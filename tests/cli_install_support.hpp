// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "realization_executor_support.hpp"
#include "cli/cli.hpp"
#include "generation/generation.hpp"
#include <athena/core/path_layout.hpp>
#include <sstream>
namespace cli_test {
using namespace execution_test;
inline void manifest(const core::PathLayout& paths, const std::string& name,
                     const fs::path& archive, const std::string& dependencies = "[]",
                     const std::string& backend = "cmake") {
    const auto directory = paths.repository_state / "packages" / name;
    fs::create_directories(directory);
    std::ofstream(directory / "package.toml")
        << "name = \"" << name << "\"\nversion = \"1\"\nsource = \"file://"
        << archive.native() << "\"\nbuild_system = \"" << backend
        << "\"\ndependencies = " << dependencies << '\n';
}
struct Result { int status; std::string output; };
inline Result invoke_command(const core::PathLayout& paths,
                             const std::string& command,
                             std::vector<std::string> args = {}) {
    const auto config = paths.root / "cli.toml";
    std::ofstream(config) << "schema_version = 1\n[paths]\n";
    std::vector<std::string> words{"athena", command, "--config", config.native(),
                                   "--root", paths.root.native()};
    words.insert(words.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& word : words) argv.push_back(word.data());
    std::ostringstream output;
    auto* out = std::cout.rdbuf(output.rdbuf());
    auto* err = std::cerr.rdbuf(output.rdbuf());
    const int status = cli::run(static_cast<int>(argv.size()), argv.data());
    std::cout.rdbuf(out); std::cerr.rdbuf(err);
    return {status, output.str()};
}

inline Result invoke(const core::PathLayout& paths, std::vector<std::string> args) {
    return invoke_command(paths, "install", std::move(args));
}
inline generation::GenerationManager manager(const core::PathLayout& paths) {
    return generation::GenerationManager(paths.generation_state,
        generation::GenerationManager::GenerationDirectoryTag{});
}
inline void no_runtime(const core::PathLayout& paths) {
    require(!fs::exists(paths.generation_state) && !fs::exists(paths.store) &&
            !fs::exists(paths.build) && !fs::exists(paths.target_root),
            "invalid request created runtime state");
}
}
