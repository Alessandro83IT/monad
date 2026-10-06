// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cli_install_support.hpp"
#include <algorithm>
using namespace cli_test;
int main() {
    StoreFixture fixture;
    const auto paths = core::PathLayout::from_root(fixture.root);
    manifest(paths, "app", fixture.root / "missing", "[\"dependency\"]");
    for (const auto& args : std::vector<std::vector<std::string>>{
             {}, {"../escape"}, {"app", "extra"}, {"absent"}, {"app"}}) {
        const auto result = invoke(paths, args);
        require(result.status != 0 && !result.output.empty(), "missing CLI failure diagnostic");
        try { no_runtime(paths); } catch (const std::exception&) {
            throw std::runtime_error("premature runtime state: " + result.output);
        }
    }
    manifest(paths, "dependency", fixture.root / "missing");
    const auto dependency_manifest = paths.repository_state / "packages/dependency/package.toml";
    { std::ofstream out(paths.repository_state / "packages/app/package.toml", std::ios::app);
      out << "conflicts = [\"dependency\"]\n"; }
    const auto conflict_result = invoke(paths, {"app"});
    require(conflict_result.status != 0, "manifest conflict lost in repository bridge");
    try { no_runtime(paths); } catch (const std::exception&) {
        throw std::runtime_error("conflict created runtime state: " + conflict_result.output);
    }
    manifest(paths, "app", fixture.root / "missing", "[\"dependency\"]");
    manifest(paths, "dependency", fixture.root / "missing");
    {
        std::ofstream out(dependency_manifest, std::ios::app);
        out << "[feature_dependencies]\nextra = [\"absent\"]\n";
    }
    core::LocalRepository repository({core::RepositoryId("local"),
                                      paths.repository_state / "packages"});
    const auto snapshot = repository.load_snapshot();
    const auto found = std::find_if(snapshot.candidates().begin(), snapshot.candidates().end(),
        [](const auto& candidate) { return candidate.package.package_id == core::PackageId("dependency"); });
    require(found != snapshot.candidates().end() &&
            found->package.feature_dependencies.at("extra").front().expression.text == "absent",
            "feature dependency lost in repository bridge");
    // A missing source is only reached after public dependency resolution succeeds.
    auto result = invoke(paths, {"app"});
    if (result.status == 0 || result.output.find("Source") == std::string::npos)
        throw std::runtime_error("realization failure not reported: " + result.output);
    auto generations = manager(paths);
    const auto baseline = generations.current();
    const auto count = generations.list().size();
    manifest(paths, "unsupported", fixture.root / "missing", "[]", "autotools");
    result = invoke(paths, {"unsupported"});
    require(result.status != 0 && result.output.find("CMake") != std::string::npos,
            "unsupported recipe not reported");
    manifest(paths, "broken", fixture.root / "missing", "[\"absent\"]");
    require(invoke(paths, {"broken"}).status != 0, "resolution failure accepted");
    require(generation::serialize(generations.current()) == generation::serialize(baseline) && generations.list().size() == count,
            "failure changed active generation");
    require(!fs::exists(paths.target_root), "failure activated target");
    require(!fs::exists(paths.store / "app-1") &&
            !fs::exists(paths.store / "unsupported-1"), "legacy fallback");
    { std::ofstream out(dependency_manifest, std::ios::app);
      out << "[provides.capability_versions]\napi = \"2\"\n"; }
    result = invoke(paths, {"dependency"});
    require(result.status != 0 && result.output.find("capability-specific") != std::string::npos,
            "unrepresentable capability version silently discarded");
}
