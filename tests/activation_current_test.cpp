// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation_schema_support.hpp"
#include <limits>
#include <type_traits>
using namespace d1_test;

int main() { return run([] {
    static_assert(!std::is_convertible_v<ActivationId, ProjectionId>);
    static_assert(!std::is_convertible_v<PlanDigest, athena::core::ArtifactId>);
    static_assert(!std::is_same_v<PlanDigest, athena::generation::GenerationDocumentDigest>);
    const CurrentSelection current = CurrentV1{42, id, PlanDigest(digest('a'))};
    const std::string canonical = "schema_version = 1\ngeneration = 42\nactivation_id = \"0123456789abcdef0123456789abcdef\"\nplan_digest = \"" + digest('a') + "\"\n";
    require(serialize(current) == canonical, "Current canonical writer order/bytes");
    require(parse_current_selection(canonical) == current, "Current v1 roundtrip");
    const auto maximum = std::to_string(max_generation);
    for (const auto& number : {std::string("0"), std::string("1"), maximum})
        for (const auto& suffix : {std::string(), std::string("\n")}) {
            const auto bytes = number + suffix;
            const auto parsed = parse_current_selection(bytes);
            require(std::holds_alternative<HistoricalCurrent>(parsed), "Decimal variant lost");
            require(std::get<HistoricalCurrent>(parsed).bytes == bytes, "Decimal bytes changed");
            require(serialize(parsed) == bytes, "Decimal serialization changed");
            require(selected_generation(parsed) == std::stoull(number), "Decimal generation changed");
        }
    for (const auto& bytes : {"", "00", "01", "+1", "-1", " 1", "1 ", "\t1", "1\t", "1\r\n", "1\n\n", "1junk", "1\nother"})
        rejects([&] { (void)parse_current_selection(bytes); }, SchemaErrorCode::Schema);
    for (const auto& bytes : {"9223372036854775808", "18446744073709551616", "999999999999999999999999999999999999"})
        rejects([&] { (void)parse_current_selection(bytes); }, SchemaErrorCode::Bounds);
    rejects([&] { (void)parse_current_selection(std::string(max_current_size + 1, '1')); }, SchemaErrorCode::Bounds);
    rejects([&] { (void)serialize(CurrentSelection{HistoricalCurrent{2, "1\n"}}); }, SchemaErrorCode::Reference);
    rejects([&] { (void)serialize(CurrentSelection{CurrentV1{max_generation + 1, id, PlanDigest(digest('a'))}}); }, SchemaErrorCode::Bounds);
    require(selected_generation(parse_current_selection(replace(canonical, "generation = 42", "generation = " + maximum))) == max_generation, "v1 maximum rejected");
    require(selected_generation(parse_current_selection(replace(canonical, "generation = 42", "generation = 0"))) == 0, "v1 zero rejected");

    for (const auto& value : {"1.0", "\"1\"", "true", "[1]", "{x = 1}", "2", "-1"})
        rejects([&] { (void)parse_current_selection(replace(canonical, "schema_version = 1", std::string("schema_version = ") + value)); }, SchemaErrorCode::Schema);
    for (const auto& value : {"-1", "9223372036854775808", "42.0", "\"42\"", "true", "[42]", "{x = 42}"})
        rejects([&] { (void)parse_current_selection(replace(canonical, "generation = 42", std::string("generation = ") + value)); }, SchemaErrorCode::Schema);
    for (const auto& value : {"", "0", "0123456789abcdef0123456789abcde", "0123456789abcdef0123456789abcdef0", "0123456789ABCDEF0123456789abcdef", "g123456789abcdef0123456789abcdef"})
        rejects([&] { (void)ActivationId(value); }, SchemaErrorCode::Identity);
    for (const auto& value : {digest('A'), std::string("sha256:123"), std::string("sha512:") + std::string(64, 'a'), digest('g'), digest('a') + "0"}) {
        rejects([&] { (void)PlanDigest(value); }, SchemaErrorCode::Identity);
        rejects([&] { (void)parse_current_selection(replace(canonical, digest('a'), value)); }, SchemaErrorCode::Identity);
    }
    rejects([&] { (void)parse_current_selection(replace(canonical, id.value(), "0123456789ABCDEF0123456789abcdef")); }, SchemaErrorCode::Identity);
    for (const auto& field : {"activation_id", "plan_digest"}) {
        const auto from = std::string(field) + " = \"" + (std::string(field) == "activation_id" ? id.value() : digest('a')) + "\"";
        rejects([&] { (void)parse_current_selection(replace(canonical, from, std::string(field) + " = 1")); }, SchemaErrorCode::Schema);
    }
    for (const auto& bytes : {
        replace(canonical, "schema_version = 1", "schema_version = 0x1"),
        replace(canonical, "generation = 42", "generation = +42"),
        replace(canonical, "generation = 42", "generation = 4_2"),
        replace(canonical, " = ", "  = "), canonical + "\n", canonical + "# comment\n",
        replace(canonical, "generation = 42", "generation = 42 # comment"), canonical.substr(0, canonical.size() - 1),
        replace(canonical, "\n", "\r\n")})
        rejects([&] { (void)parse_current_selection(bytes); }, SchemaErrorCode::Noncanonical);
    for (const auto& bytes : {
        canonical + "other = 1\n", canonical + "generation = 42\n", canonical + "junk\n",
        replace(canonical, "schema_version = 1\ngeneration = 42\n", "generation = 42\nschema_version = 1\n"),
        replace(canonical, "generation = 42\n", "")})
        rejects([&] { (void)parse_current_selection(bytes); }, SchemaErrorCode::Schema);
    require(serialize(current) == canonical, "Rejected parses mutated source current");
    CurrentSelection moved_from = current;
    const auto moved_to = std::move(moved_from);
    require(serialize(moved_to) == canonical, "Move lost valid current");
    const auto& remaining = std::get<CurrentV1>(moved_from);
    if (remaining.activation_id.value().empty() || remaining.plan_digest.value().empty())
        rejects([&] { (void)serialize(moved_from); }, SchemaErrorCode::Identity);
    const auto exact_bound = canonical + std::string(max_current_size - canonical.size(), ' ');
    rejects([&] { (void)parse_current_selection(exact_bound); }, SchemaErrorCode::Noncanonical);
}); }
