#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace athena::artifact {

enum class ErrorCode {
    InvalidRoot, InspectionFailure, InvalidPath, UnsupportedFileType,
    UnsupportedPermissions, HardLinkTopology, UnsupportedMetadata,
    ReadFailure, InvalidModel, ChangedTree, LengthOverflow, HashFailure
};

// Internal error, with a byte-preserving relative path (empty means root).
class Error : public std::runtime_error {
public:
    Error(ErrorCode code, std::string path, std::string_view detail);
    ErrorCode code() const noexcept { return code_; }
    const std::string& path() const noexcept { return path_; }
private:
    ErrorCode code_;
    std::string path_;
};

struct Directory {
    bool operator==(const Directory&) const = default;
};
struct RegularFile {
    bool executable = false;
    std::string content; // Complete logical bytes, including NUL; not text.
    bool operator==(const RegularFile&) const = default;
};
struct SymbolicLink {
    std::string target; // Literal Linux readlink bytes, never a resolved path.
    bool operator==(const SymbolicLink&) const = default;
};
struct NormalizedArtifactEntry {
    std::string path; // Linux native bytes; no Unicode conversion.
    std::variant<Directory, RegularFile, SymbolicLink> value;
    bool operator==(const NormalizedArtifactEntry&) const = default;
};
struct NormalizedArtifact {
    std::uint32_t schema_version = 1;
    std::vector<NormalizedArtifactEntry> entries;
    bool operator==(const NormalizedArtifact&) const = default;
};

bool path_bytes_less(std::string_view left, std::string_view right) noexcept;
void validate_path(std::string_view path);
// Pure strict validation: requires unsigned-byte sorted input.
void validate(const NormalizedArtifact& artifact);
// Pure canonicalization, also validates duplicates, fields and tree structure.
void normalize(NormalizedArtifact& artifact);

} // namespace athena::artifact
