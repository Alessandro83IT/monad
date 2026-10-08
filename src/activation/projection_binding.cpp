// SPDX-License-Identifier: GPL-3.0-or-later
#include "projection_binding.hpp"
#include "codec_detail.hpp"
namespace athena::activation {
void ProjectionBinding::validate() const {
    projection_id.validate();
    validate_absolute_locator(generation_authority);
    validate_absolute_locator(store_authority);
    validate_absolute_locator(projection_root);
}
std::string serialize(const ProjectionBinding& binding) {
    binding.validate(); codec_detail::Writer out;
    out.number("schema_version", 1);
    out.text("projection_id", binding.projection_id.value());
    out.text("generation_authority_hex", encode_hex(binding.generation_authority));
    out.text("store_authority_hex", encode_hex(binding.store_authority));
    out.text("projection_root_hex", encode_hex(binding.projection_root));
    return out.bytes();
}
ProjectionBinding parse_projection_binding(std::string_view bytes) {
    namespace c = codec_detail;
    const auto table = c::parse(bytes, 32 * 1024);
    c::keys(table, {"schema_version", "projection_id", "generation_authority_hex", "store_authority_hex", "projection_root_hex"});
    c::version(table);
    ProjectionBinding result{ProjectionId(c::string(table, "projection_id")),
        decode_hex(c::string(table, "generation_authority_hex"), max_path_size),
        decode_hex(c::string(table, "store_authority_hex"), max_path_size),
        decode_hex(c::string(table, "projection_root_hex"), max_path_size)};
    result.validate(); c::canonical(bytes, serialize(result)); return result;
}
}
