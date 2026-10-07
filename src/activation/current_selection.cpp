// SPDX-License-Identifier: GPL-3.0-or-later
#include "current_selection.hpp"
#include "codec_detail.hpp"

namespace athena::activation {
namespace d = codec_detail;
std::uint64_t selected_generation(const CurrentSelection& selection) {
    return std::visit([](const auto& value) { return value.generation; }, selection);
}
std::string serialize(const CurrentSelection& selection) {
    d::generation(selected_generation(selection));
    if (const auto* old = std::get_if<HistoricalCurrent>(&selection)) {
        if (old->bytes.size() > max_current_size) throw SchemaError(SchemaErrorCode::Bounds, "Current exceeds bound");
        std::string_view number = old->bytes;
        if (number.ends_with('\n')) number.remove_suffix(1);
        if (d::decimal(number) != old->generation) throw SchemaError(SchemaErrorCode::Reference, "Historical current generation mismatch");
        return old->bytes;
    }
    const auto& current = std::get<CurrentV1>(selection);
    current.activation_id.validate(); current.plan_digest.validate();
    d::Writer writer(max_current_size);
    writer.number("schema_version", 1);
    writer.number("generation", current.generation);
    writer.text("activation_id", current.activation_id.value());
    writer.text("plan_digest", current.plan_digest.value());
    return writer.bytes();
}
CurrentSelection parse_current_selection(std::string_view bytes) {
    if (bytes.size() > max_current_size) throw SchemaError(SchemaErrorCode::Bounds, "Current exceeds bound");
    // Anything lacking the v1 discriminator must satisfy the whole decimal grammar.
    if (!bytes.starts_with("schema_version")) {
        auto number = bytes;
        if (number.ends_with('\n')) number.remove_suffix(1);
        return HistoricalCurrent{d::decimal(number), std::string(bytes)};
    }
    const auto table = d::parse(bytes, max_current_size);
    d::keys(table, {"schema_version", "generation", "activation_id", "plan_digest"});
    d::version(table);
    CurrentSelection result = CurrentV1{d::integer(table, "generation"),
        ActivationId(d::string(table, "activation_id")), PlanDigest(d::string(table, "plan_digest"))};
    d::canonical(bytes, serialize(result));
    return result;
}
}
