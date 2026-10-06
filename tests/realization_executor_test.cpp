// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "realization_executor_support.hpp"
#include <array>
#include <cstdio>
using namespace execution_test;
int main() {
    StoreFixture fixture; setup(fixture);
    store::ObjectStore objects(fixture.root / "objects");
    realization::ExecutionOptions options{fixture.root / "workspace"};
    auto run = [&](const core::InstallPlan& p) { return realization::realize(p, objects, options); };
    require(run(plan({})).empty(), "empty plan result");
    require(fs::is_empty(fixture.root / "objects"), "empty plan published");
    auto r = recipe(fixture.root / "missing");
    using Code = realization::ExecutionErrorCode;
    rejects(Code::Acquisition, [&] { run(plan({r})); }); boundary(fixture);
    r.sources.push_back(r.sources[0]);
    rejects(Code::UnsupportedRecipe, [&] { run(plan({r})); });
    r.sources.clear();
    bool invalid = false;
    try { (void)plan({r}); } catch (const std::exception&) { invalid = true; }
    require(invalid, "zero-source recipe accepted at public plan boundary");
    r = recipe(fixture.root / "missing");
    r.build_parameters["flag"] = "value";
    rejects(Code::UnsupportedRecipe, [&] { run(plan({r})); });
    r.build_parameters.clear(); r.build_backend = "unknown";
    rejects(Code::UnsupportedRecipe, [&] { run(plan({r})); });
    r.build_backend = "autotools";
    rejects(Code::UnsupportedRecipe, [&] { run(plan({r})); });
    r.build_backend = "cmake"; r.build_ir_reference = core::MetadataReference("ir-key");
    rejects(Code::UnsupportedKind, [&] { run(plan({r})); });
    r.build_ir_reference.reset(); r.sources[0].reference = core::MetadataReference("relative/archive");
    rejects(Code::UnsupportedRecipe, [&] { run(plan({r})); });
    r.sources[0].reference = core::MetadataReference("https://example.invalid/source.tar.gz");
    rejects(Code::UnsupportedRecipe, [&] { run(plan({r})); });
    rejects(Code::UnsupportedKind, [&] { run(plan({r}, false, true)); });
    rejects(Code::UnsupportedKind, [&] { run(plan({r}, false, false, true)); });
    rejects(Code::UnsupportedKind, [&] { run(plan({r}, false)); });
    const auto archive = archive_fixture(fixture, "one");
    // Preserve literal header names: archive writers append '/' to directories.
    const auto raw = fixture.root / "aliases.tar";
    std::ofstream tar(raw, std::ios::binary);
    for (const char* name : {"project", "project/."}) {
        std::array<char, 512> header{};
        std::snprintf(header.data(), 100, "%s", name);
        std::snprintf(header.data() + 100, 8, "%07o", 0755);
        std::snprintf(header.data() + 108, 8, "%07o", 0);
        std::snprintf(header.data() + 116, 8, "%07o", 0);
        std::snprintf(header.data() + 124, 12, "%011o", 0);
        std::snprintf(header.data() + 136, 12, "%011o", 0);
        std::fill_n(header.data() + 148, 8, ' ');
        header[156] = '5';
        unsigned checksum = 0;
        for (unsigned char byte : header) checksum += byte;
        std::snprintf(header.data() + 148, 7, "%06o", checksum);
        tar.write(header.data(), header.size());
    }
    const std::array<char, 1024> end{};
    tar.write(end.data(), end.size());
    tar.close(); require(static_cast<bool>(tar), "write normalized duplicate tar");
    const auto compress = "gzip -c '" + raw.native() + "' > '" + archive.native() + "'";
    require(std::system(compress.c_str()) == 0, "compress normalized duplicate archive");
    rejects(Code::Extraction, [&] { run(plan({recipe(archive)})); }); boundary(fixture);
    archive_fixture(fixture, "one");
    r = recipe(archive); r.sources[0].digest = core::ContentDigest("sha512:abcd");
    rejects(Code::UnsupportedRecipe, [&] { run(plan({r})); });
    rejects(Code::InvalidInput, [&] { realization::realize(plan({recipe(archive)}), objects, {"relative"}); });
    fs::create_directory_symlink(fixture.root / "workspace", fixture.root / "workspace-link");
    rejects(Code::InvalidInput, [&] { realization::realize(plan({recipe(archive)}), objects, {fixture.root / "workspace-link"}); });
    // Link-bearing and traversal-bearing archives cannot write outside private work.
    fs::create_symlink("/tmp", fixture.root / "input/project/link");
    auto repack = [&](const std::string& flags) {
        const auto command = "tar -czf '" + archive.native() + "' " + flags + " -C '" + (fixture.root / "input").native() + "' project";
        require(std::system(command.c_str()) == 0, "repack source");
    };
    repack("");
    rejects(Code::Extraction, [&] { run(plan({recipe(archive)})); }); boundary(fixture);
    fs::remove(fixture.root / "input/project/link");
    repack("--transform='s|project|../../escape|'");
    rejects(Code::Extraction, [&] { run(plan({recipe(archive)})); }); boundary(fixture);
    require(!fs::exists(fixture.root / "escape"), "archive escaped private directory");
    repack("");
    r = recipe(archive); r.sources[0].digest = core::ContentDigest("sha256:" + std::string(64, '0'));
    rejects(Code::SourceVerification, [&] { run(plan({r})); }); boundary(fixture);
    require(fs::is_empty(fixture.root / "objects"), "digest mismatch published");
    r.sources[0].digest.reset(); r.sources[0].size_bytes = 0;
    rejects(Code::SourceVerification, [&] { run(plan({r})); });
    std::ofstream(fixture.root / "invalid") << "not an archive";
    r = recipe(fixture.root / "invalid");
    rejects(Code::Extraction, [&] { run(plan({r})); }); boundary(fixture);
    // CMake failure is capability-independent and must clean execution state.
    std::ofstream(fixture.root / "input/project/CMakeLists.txt") << "invalid_cmake_command()\n";
    const auto command = "tar -czf '" + archive.native() + "' -C '" + (fixture.root / "input").native() + "' project";
    require(std::system(command.c_str()) == 0, "replace archive");
    r = recipe(archive);
    rejects(Code::Build, [&] { run(plan({r})); }); boundary(fixture);
    // Executor-owned paths never include identity material.
    r.package_id = core::PackageId("../../escape");
    try { auto p = plan({r}); rejects(Code::Build, [&] { run(p); }); }
    catch (const core::TypeError&) { /* Public identity validation may reject it first. */ }
    require(!fs::exists(fixture.root.parent_path() / "escape"), "identity escaped workspace");
    boundary(fixture);
}
