// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <athena_toml/athena_toml.hpp>
#include <cstddef>
#include <string_view>
#include <stdexcept>
#include <filesystem>
#include <iosfwd>
namespace athena::parsing {
enum class Profile { Any, Intent, Plan, Status, Current, ProjectionBinding, Adoption,
 Generation, Record, Semantics, Policy, Package, Manifest, LegacyMetadata, StoreBinding, Pins };
struct Budget {
 std::size_t input_bytes, nodes, array_elements, depth, token_bytes, text_work;
 // Upper bounds derived from source bytes, not new metadata cardinality quotas.
 static Budget compatible(std::size_t bytes);
};
struct Stats { std::size_t nodes=0, array_elements=0, text_work=0, max_depth=0; };
enum class Rejection { Budget, Structure, Identity };
class Error : public std::invalid_argument {
public:
 Error(Rejection reason, const char* message):std::invalid_argument(message), reason(reason){}
 Rejection reason;
};
// Incremental authorization with the original toml++ 3.4 grammar. A partial AST
// may contain only previously authorized nodes. No global observer or allocator.
// Stats are copied on success AND rejection, for allocation-bound regressions.
athena_toml::table parse(std::string_view, Profile, const Budget&, Stats* = nullptr);
athena_toml::table parse_model(std::string_view, Profile);
athena_toml::table parse_file(const std::filesystem::path&, Profile, std::size_t max_input_bytes);
namespace detail {
// Internal seekable-stream reader shared with file parsing. Kept separate so
// short reads and growth can be exercised without racing a filesystem writer.
athena_toml::table parse_stream(std::istream&, Profile, std::size_t max_input_bytes);
}
inline athena_toml::table parse(std::string_view input, Profile profile) {
 return parse(input, profile, Budget::compatible(input.size()));
}
}
