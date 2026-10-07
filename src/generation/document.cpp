// SPDX-License-Identifier: GPL-3.0-or-later
#include "document.hpp"
#include "../activation/codec_detail.hpp"
#include "../hash/sha256.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <system_error>

namespace athena::generation {
namespace a = activation;
namespace d = core::detail;
ValidatedGenerationDocument validate_generation_document(std::string_view bytes, std::uint64_t expected_id) {
    a::codec_detail::generation(expected_id);
    if (bytes.size() > a::max_document_size) throw a::SchemaError(a::SchemaErrorCode::Bounds, "Generation document exceeds bound");
    auto generation = deserialize_generation(std::string(bytes));
    if (generation.id != expected_id) throw a::SchemaError(a::SchemaErrorCode::Reference, "Generation document ID mismatch");
    std::string preimage("athena-generation-document-v1\0", sizeof("athena-generation-document-v1\0") - 1);
    a::codec_detail::append_u64(preimage, bytes.size());
    preimage.append(bytes);
    return {std::move(generation), std::string(bytes), GenerationDocumentDigest(hash::sha256(preimage))};
}
namespace {
void unchanged(int parent, const std::string& name, const struct stat& opened) {
    struct stat named{};
    if (::fstatat(parent, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) < 0 ||
        named.st_dev != opened.st_dev || named.st_ino != opened.st_ino ||
        (named.st_mode & S_IFMT) != (opened.st_mode & S_IFMT))
        throw a::SchemaError(a::SchemaErrorCode::Metadata, "Generation metadata substituted");
}
}
ValidatedGenerationDocument read_generation_document(const std::filesystem::path& authority, std::uint64_t id, const GenerationRead& read) {
    a::codec_detail::generation(id);
    auto parent = d::open_directory(authority);
    const auto name = std::to_string(id);
    auto directory = d::open_at(parent.get(), name, O_RDONLY | O_DIRECTORY);
    const auto directory_identity = d::stat_fd(directory.get());
    unchanged(parent.get(), name, directory_identity);
    auto file = d::open_at(directory.get(), "generation.toml", O_RDONLY);
    d::regular(file.get());
    const auto before = d::stat_fd(file.get());
    unchanged(directory.get(), "generation.toml", before);
    if (before.st_size < 0 || static_cast<std::uintmax_t>(before.st_size) > a::max_document_size)
        throw a::SchemaError(a::SchemaErrorCode::Bounds, "Generation document exceeds bound");
    std::string bytes;
    char buffer[4096];
    for (;;) {
        const auto count = read(file.get(), buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) throw std::system_error(errno, std::generic_category(), "Read generation document");
        if (!count) break;
        if (static_cast<std::size_t>(count) > sizeof(buffer))
            throw a::SchemaError(a::SchemaErrorCode::Metadata, "Invalid generation read count");
        if (static_cast<std::size_t>(count) > a::max_document_size - bytes.size())
            throw a::SchemaError(a::SchemaErrorCode::Bounds, "Generation document exceeds bound");
        bytes.append(buffer, static_cast<std::size_t>(count));
    }
    const auto after = d::stat_fd(file.get());
    if (before.st_size != after.st_size || before.st_mode != after.st_mode ||
        before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
        before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec ||
        static_cast<std::uintmax_t>(after.st_size) != bytes.size())
        throw a::SchemaError(a::SchemaErrorCode::Metadata, "Generation document changed while reading");
    unchanged(directory.get(), "generation.toml", after);
    unchanged(parent.get(), name, directory_identity);
    return validate_generation_document(bytes, id);
}
}
