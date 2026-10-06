// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "artifact/model.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace athena::artifact;

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
template<class Function>
void rejects(Function function, ErrorCode code)
{
    try { function(); }
    catch (const Error& error) { require(error.code() == code, "wrong model error category"); return; }
    throw std::runtime_error("invalid model accepted");
}
}

int main()
{
    validate({});
    NormalizedArtifact valid{1, {
        {"bin", Directory{}}, {"bin/tool", RegularFile{true, std::string("a\0\xff", 3)}},
        {"empty", Directory{}}, {"link", SymbolicLink{"../outside//./target/"}}
    }};
    validate(valid);
    require(std::get<RegularFile>(valid.entries[1].value).content == std::string("a\0\xff", 3), "binary content lost");
    require(std::get<SymbolicLink>(valid.entries[3].value).target == "../outside//./target/", "link target rewritten");

    const std::vector<std::string> invalid_paths = {
        "", "/a", "/", "a/", "a//b", ".", "..", "a/./b", "a/../b",
        "a//", std::string("a\0b", 3)
    };
    for (const auto& path : invalid_paths) {
        rejects([&] { validate_path(path); }, ErrorCode::InvalidPath);
        rejects([&] { validate({1, {{path, Directory{}}}}); }, ErrorCode::InvalidPath);
    }
    for (const auto& path : {".config", "a..b", "C:foo", "a\\b", "a\nb", "Case"}) validate_path(path);
    validate_path(std::string("data/\xff", 6));
    rejects([] { validate({2, {}}); }, ErrorCode::InvalidModel);
    rejects([] {
        NormalizedArtifact duplicate{1, {{"a", Directory{}}, {"a", Directory{}}}};
        normalize(duplicate);
    }, ErrorCode::InvalidModel);
    rejects([] { validate({1, {{"bin/tool", RegularFile{}}}}); }, ErrorCode::InvalidModel);
    rejects([] { validate({1, {{"bin", RegularFile{}}, {"bin/tool", RegularFile{}}}}); }, ErrorCode::InvalidModel);
    rejects([] { validate({1, {{"bin", SymbolicLink{"elsewhere"}}, {"bin/tool", RegularFile{}}}}); }, ErrorCode::InvalidModel);
    rejects([] { validate({1, {{"a", SymbolicLink{""}}}}); }, ErrorCode::InvalidModel);
    rejects([] { validate({1, {{"a", SymbolicLink{std::string("x\0y", 3)}}}}); }, ErrorCode::InvalidModel);

    NormalizedArtifact unordered{1, {{std::string(1, '\xff'), Directory{}},
        {"z", RegularFile{}}, {std::string(1, '\x80'), SymbolicLink{std::string("/\xff", 2)}},
        {"a/b", Directory{}}, {"a", Directory{}}}};
    rejects([&] { validate(unordered); }, ErrorCode::InvalidModel);
    normalize(unordered);
    require(unordered.entries[0].path == "a" && unordered.entries[1].path == "a/b" &&
        unordered.entries[2].path == "z" && unordered.entries[3].path == std::string(1, '\x80') &&
        unordered.entries[4].path == std::string(1, '\xff'), "unsigned byte order incorrect");
    auto reversed = unordered;
    std::reverse(reversed.entries.begin(), reversed.entries.end());
    normalize(reversed);
    require(unordered == reversed, "input order affects canonical model");
    auto unchanged = unordered;
    normalize(unchanged);
    require(unchanged == unordered, "normalization not idempotent");
    std::cout << "Artifact model tests passed.\n";
}
