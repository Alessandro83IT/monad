#include "model.hpp"

#include <algorithm>
#include <limits>
#include <map>

namespace athena::artifact {
namespace {
std::string display_path(std::string_view path)
{
    if (path.empty()) return "<root>";
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char byte : path) {
        if (byte >= 0x20 && byte <= 0x7e && byte != '\\' && byte != '"')
            result += static_cast<char>(byte);
        else {
            result += "\\x";
            result += hex[byte >> 4];
            result += hex[byte & 15];
        }
    }
    return result + '"';
}
struct ByteLess {
    bool operator()(std::string_view a, std::string_view b) const noexcept
    { return path_bytes_less(a, b); }
};
// No stored lengths can disagree with actual bytes. Check v1's U64 range
// without implementing any canonical encoding or framed-length calculation.
void validate_length(std::size_t size, const std::string& path)
{
    if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
        if (size > std::numeric_limits<std::uint64_t>::max())
            throw Error(ErrorCode::InvalidModel, path, "length exceeds v1 range");
    }
}
}

Error::Error(ErrorCode code, std::string path, std::string_view detail)
    : std::runtime_error("artifact " + display_path(path) + ": " + std::string(detail)),
      code_(code), path_(std::move(path)) {}

bool path_bytes_less(std::string_view left, std::string_view right) noexcept
{
    return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
        [](unsigned char a, unsigned char b) { return a < b; });
}

void validate_path(std::string_view path)
{
    if (path.empty() || path.find('\0') != path.npos)
        throw Error(ErrorCode::InvalidPath, std::string(path), "empty path or NUL byte");
    std::size_t start = 0;
    do {
        const auto end = path.find('/', start);
        const auto component = path.substr(start, end == path.npos ? end : end - start);
        if (component.empty() || component == "." || component == "..")
            throw Error(ErrorCode::InvalidPath, std::string(path), "noncanonical path component");
        if (end == path.npos) break;
        start = end + 1;
    } while (true);
}

void validate(const NormalizedArtifact& artifact)
{
    if (artifact.schema_version != 1)
        throw Error(ErrorCode::InvalidModel, {}, "schema version must be 1");
    validate_length(artifact.entries.size(), {});
    std::map<std::string, bool, ByteLess> seen;
    const std::string* previous = nullptr;
    for (const auto& entry : artifact.entries) {
        validate_path(entry.path);
        validate_length(entry.path.size(), entry.path);
        if (!seen.emplace(entry.path, std::holds_alternative<Directory>(entry.value)).second)
            throw Error(ErrorCode::InvalidModel, entry.path, "duplicate entry path");
        if (previous && !path_bytes_less(*previous, entry.path))
            throw Error(ErrorCode::InvalidModel, entry.path, "entries must be sorted by unsigned path bytes");
        previous = &entry.path;
        const auto separator = entry.path.rfind('/');
        if (separator != entry.path.npos) {
            const auto parent = seen.find(entry.path.substr(0, separator));
            if (parent == seen.end() || !parent->second)
                throw Error(ErrorCode::InvalidModel, entry.path, "parent must be an explicit directory");
        }
        if (entry.value.valueless_by_exception())
            throw Error(ErrorCode::InvalidModel, entry.path, "missing entry value");
        if (const auto* file = std::get_if<RegularFile>(&entry.value))
            validate_length(file->content.size(), entry.path);
        if (const auto* link = std::get_if<SymbolicLink>(&entry.value)) {
            if (link->target.empty() || link->target.find('\0') != link->target.npos)
                throw Error(ErrorCode::InvalidModel, entry.path, "link target must be nonempty and NUL-free");
            validate_length(link->target.size(), entry.path);
        }
    }
}

void normalize(NormalizedArtifact& artifact)
{
    std::sort(artifact.entries.begin(), artifact.entries.end(), [](const auto& a, const auto& b) {
        return path_bytes_less(a.path, b.path);
    });
    validate(artifact);
}
} // namespace athena::artifact
