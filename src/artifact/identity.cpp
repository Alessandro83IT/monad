#include "identity.hpp"
#include "encoding.hpp"
#include "../hash/sha256.hpp"
namespace athena::artifact {
core::ArtifactId derive_artifact_id(const NormalizedArtifact& artifact)
{
    const auto canonical = canonical_bytes(artifact); // validates before encoding
    using namespace encoding_detail;
    const auto length = checked_length(canonical.size());
    const auto preimage_length = checked_add(28, length);
    std::string preimage;
    if (preimage_length > preimage.max_size())
        throw Error(ErrorCode::LengthOverflow, {}, "identity preimage exceeds local string limit");
    preimage.reserve(static_cast<std::size_t>(preimage_length));
    preimage.append("athena-artifact\0", 16);
    append_u32(preimage, artifact.schema_version);
    append_u64(preimage, length);
    preimage.append(canonical);
    try { return core::ArtifactId(hash::sha256(preimage)); }
    catch (const hash::Error& error) { throw Error(ErrorCode::HashFailure, {}, error.what()); }
    catch (const core::TypeError& error) { throw Error(ErrorCode::InvalidModel, {}, error.what()); }
}
}
