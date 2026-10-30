// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "temporary_directory.hpp"

#include "../src/archive/archive.hpp"
#include "../src/download/downloader.hpp"
#include "../src/process/run.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

int main()
{
    athena::test::TemporaryDirectory fixture("athena-process-argv");
    const auto bin = fixture.path() / "bin";
    fs::create_directories(bin);
    const auto capture = fixture.path() / "arguments.txt";
    const auto marker = fixture.path() / "shell-injection-marker";

    {
        std::ofstream fake_curl(bin / "curl");
        fake_curl << "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$ARG_CAPTURE\"\n";
    }
    fs::permissions(bin / "curl", fs::perms::owner_all);

    const auto archive_source = fixture.path() / "package";
    fs::create_directories(archive_source);
    std::ofstream(archive_source / "README") << "fixture";
    const auto hostile_archive = fixture.path() / "archive $(touch-marker).tar.gz";
    assert(athena::process::run({"tar", "-czf", hostile_archive.string(), "-C", fixture.path().string(), "package"}) == 0);

    const char* old_path_value = std::getenv("PATH");
    const std::string old_path = old_path_value ? old_path_value : "";
    const std::string test_path = bin.string() + ":" + old_path;
    assert(::setenv("PATH", test_path.c_str(), 1) == 0);
    assert(::setenv("ARG_CAPTURE", capture.c_str(), 1) == 0);

    const std::string hostile_url = "https://example.invalid/$(touch " + marker.string() + ")";
    const auto destination = fixture.path() / "download.tar.gz";
    athena::download::download_file(hostile_url, destination);
    assert(!fs::exists(marker));
    {
        std::ifstream input(capture);
        std::string line;
        std::string last;
        while (std::getline(input, line)) last = line;
        assert(last == hostile_url);
    }

    const auto extraction_parent = fixture.path() / "extract";
    fs::create_directories(extraction_parent);
    const auto stale_collision = extraction_parent /
        ("." + hostile_archive.stem().stem().string() + "-extract");
    fs::create_directories(stale_collision);
    std::ofstream(stale_collision / "keep.txt") << "unrelated data";
    const auto extracted = athena::archive::extract(hostile_archive, extraction_parent);
    assert(fs::is_directory(extracted));
    assert(fs::exists(stale_collision / "keep.txt"));
    assert(!fs::exists(marker));

    if (old_path_value) assert(::setenv("PATH", old_path.c_str(), 1) == 0);
    else assert(::unsetenv("PATH") == 0);
    assert(::unsetenv("ARG_CAPTURE") == 0);
    return 0;
}
