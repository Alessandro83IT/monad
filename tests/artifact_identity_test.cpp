#include "artifact/identity.hpp"
#include "artifact/encoding.hpp"
#include "hash/sha256.hpp"
#include "artifact_golden_vectors.hpp"
#include "artifact_test_support.hpp"
#include <algorithm>
using namespace athena::artifact;
int main()
{
    require(athena::hash::sha256("") == "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty SHA-256");
    require(athena::hash::sha256("abc") == "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc SHA-256");
    for (const auto& vector : golden_vectors) {
        const auto before = vector.model;
        const auto id = derive_artifact_id(vector.model);
        require(id.value() == vector.artifact_id, vector.name);
        require(before == vector.model, "identity mutated model");
        const auto raw_digest = athena::hash::sha256(unhex(vector.canonical_hex));
        require(raw_digest == std::string("sha256:") + vector.canonical_sha256, "canonical SHA-256 mismatch");
        require(raw_digest != id.value(), "tree hash confused with framed ArtifactId");
    }
    const NormalizedArtifact base{1, {{"a", RegularFile{false, "hi"}}}};
    const auto base_id = derive_artifact_id(base);
    for (const NormalizedArtifact& changed : std::vector<NormalizedArtifact>{
        {1, {{"a", RegularFile{false, "HI"}}}}, {1, {{"b", RegularFile{false, "hi"}}}},
        {1, {{"a", RegularFile{true, "hi"}}}}, {1, {{"a", Directory{}}}},
        {1, {{"a", SymbolicLink{"hi"}}}},
        {1, {{"a", RegularFile{false, "hi"}}, {"empty", Directory{}}}}
    }) require(derive_artifact_id(changed) != base_id, "semantic change omitted");
    require(derive_artifact_id({1, {{"l", SymbolicLink{"a"}}}}) !=
        derive_artifact_id({1, {{"l", SymbolicLink{"./a"}}}}), "literal target change omitted");
    require(derive_artifact_id({}) != derive_artifact_id({1, {{"empty", Directory{}}}}), "empty directory omitted");
    const auto reject = [](const NormalizedArtifact& model, ErrorCode code) {
        const auto before = model;
        for (bool identity : {false, true}) {
            bool rejected = false;
            try { if (identity) (void)derive_artifact_id(model); else (void)canonical_bytes(model); }
            catch (const Error& error) { require(error.code() == code, "wrong validation error"); rejected = true; }
            require(rejected, "invalid model accepted");
        }
        require(model == before, "invalid input repaired");
    };
    reject({2, {}}, ErrorCode::InvalidModel);
    for (const auto& path : {"", "/a", "a/", "a//b", "a/../b", "a/./b"})
        reject({1, {{path, Directory{}}}}, ErrorCode::InvalidPath);
    reject({1, {{std::string("a\0b", 3), Directory{}}}}, ErrorCode::InvalidPath);
    reject({1, {{"a", Directory{}}, {"a", Directory{}}}}, ErrorCode::InvalidModel);
    reject({1, {{"a/b", Directory{}}}}, ErrorCode::InvalidModel);
    reject({1, {{"a", RegularFile{}}, {"a/b", Directory{}}}}, ErrorCode::InvalidModel);
    reject({1, {{"a", SymbolicLink{"x"}}, {"a/b", Directory{}}}}, ErrorCode::InvalidModel);
    reject({1, {{"a", SymbolicLink{""}}}}, ErrorCode::InvalidModel);
    reject({1, {{"a", SymbolicLink{std::string("x\0y", 3)}}}}, ErrorCode::InvalidModel);
    NormalizedArtifact ordered{1, {{"a", Directory{}}, {"a/b", Directory{}},
        {"z", Directory{}}, {std::string(1, '\x80'), Directory{}}, {std::string(1, '\xff'), Directory{}}}};
    const auto bytes = canonical_bytes(ordered);
    // Independent unsigned path order fixture, including prefix and high bytes.
    require(bytes == unhex("617468656e612d61727469666163742d7472656500000000010000000000000005"
        "01000000000000000161" "010000000000000003612f62" "0100000000000000017a"
        "01000000000000000180" "010000000000000001ff"), "unsigned ordering bytes");
    std::reverse(ordered.entries.begin(), ordered.entries.end());
    reject(ordered, ErrorCode::InvalidModel);
}
