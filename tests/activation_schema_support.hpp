// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "activation/plan.hpp"
#include <iostream>
#include <stdexcept>

namespace d1_test {
using namespace athena::activation;
inline void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
}
template<class F> void rejects(F action, SchemaErrorCode code) {
    try { action(); }
    catch (const SchemaError& error) {
        require(error.code() == code, std::string("Wrong error category: ") + error.what());
        require(*error.what() != '\0', "Missing error diagnostic");
        return;
    }
    throw std::runtime_error("Invalid input accepted");
}
inline std::string replace(std::string text, std::string_view from, std::string_view to) {
    const auto at = text.find(from);
    require(at != text.npos, "Test replacement not found");
    text.replace(at, from.size(), to);
    return text;
}
inline const ActivationId id("0123456789abcdef0123456789abcdef");
inline const ActivationId old_id("11111111111111111111111111111111");
inline const AdoptionId adoption_id("22222222222222222222222222222222");
inline const ProjectionId projection_id("fedcba9876543210fedcba9876543210");
inline std::string digest(char c) { return "sha256:" + std::string(64, c); }
inline WitnessReference fresh(std::uint64_t op) { return {id, op}; }
inline WitnessReference old(std::uint64_t op) { return {old_id, op}; }
inline ActivationIntent intent() {
    return {id, "/state/generations", "/store", "/target", projection_id, 42,
        athena::generation::GenerationDocumentDigest(digest('b')), AbsentBase{}};
}
inline ActivationPlan plan() {
    return {id, {ArtifactSource{athena::core::ArtifactId(digest('a'))}}, {
        {0, "bin", EnsureDirectory{}}, {1, "bin/tool", CreateLink{0, fresh(1)}}}};
}
inline int run(auto action) {
    try { action(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
}
