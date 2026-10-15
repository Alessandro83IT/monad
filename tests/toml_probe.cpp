// SPDX-License-Identifier: GPL-3.0-or-later
#if defined(ATHENA_TOML_PROBE)
#include "parsing/bounded_toml.hpp"
namespace subject = athena_toml;
#else
#include <toml++/toml.hpp>
static_assert(TOML_LIB_MAJOR == 3 && TOML_LIB_MINOR == 4 && TOML_LIB_PATCH == 0);
namespace subject = toml;
#endif
#include <bit>
#include <fstream>
#include <iostream>
#include <iterator>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace {
void bytes(std::string_view value) {
    std::cout << value.size() << ':';
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : value) std::cout << hex[c >> 4] << hex[c & 15];
    std::cout << ';';
}
// Independent of formatter whitespace/inline flags. Includes every node's type,
// exact string/key bytes, integer values and IEEE double bits (including -0).
void fingerprint(const subject::node& n) {
    std::cout << static_cast<int>(n.type()) << '{';
    if (auto t = n.as_table()) {
        for (const auto& [k, v] : *t) { bytes(k.str()); fingerprint(v); }
    } else if (auto a = n.as_array()) {
        for (const auto& v : *a) fingerprint(v);
    } else if (n.is_string()) bytes(n.ref<std::string>());
    else if (n.is_integer()) std::cout << n.ref<int64_t>();
    else if (n.is_floating_point()) std::cout << std::bit_cast<uint64_t>(n.ref<double>());
    else if (n.is_boolean()) std::cout << n.ref<bool>();
    else {
        std::ostringstream out; out.imbue(std::locale::classic());
        if (n.is_date()) out << n.ref<subject::date>();
        else if (n.is_time()) out << n.ref<subject::time>();
        else if (n.is_date_time()) out << n.ref<subject::date_time>();
        else throw std::runtime_error("unhandled probe node type");
        bytes(out.str());
    }
    std::cout << '}';
}
#if defined(ATHENA_TOML_PROBE)
athena::parsing::Profile profile(std::string_view name) {
    using athena::parsing::Profile;
#define P(x) if (name == #x) return Profile::x
    P(Any); P(Intent); P(Plan); P(Status); P(Current); P(ProjectionBinding); P(Adoption);
    P(Generation); P(Record); P(Semantics); P(Policy); P(Package); P(Manifest);
    P(LegacyMetadata); P(StoreBinding); P(Pins);
#undef P
    throw std::runtime_error("unknown probe profile");
}
#endif
}
int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) return 2;
    try {
        std::ifstream in(argv[2], std::ios::binary);
        if (!in) throw std::runtime_error("cannot open fixture");
        const std::string input((std::istreambuf_iterator<char>(in)), {});
#if defined(ATHENA_TOML_PROBE)
        auto budget = athena::parsing::Budget::compatible(input.size());
        if (argc == 4 && std::string_view(argv[3]) == "zero-nodes") budget.nodes = 0;
        const auto tree = athena::parsing::parse(input, profile(argv[1]), budget);
#else
        const auto tree = subject::parse(input);
#endif
        std::cout << "ACCEPT\n"; fingerprint(tree); std::cout << '\n';
    }
#if defined(ATHENA_TOML_PROBE)
    catch (const athena::parsing::Error& e) {
        std::cout << (e.reason == athena::parsing::Rejection::Budget ? "REJECT_BUDGET\n" :
                      e.reason == athena::parsing::Rejection::Identity ? "REJECT_IDENTITY\n" : "REJECT_STRUCTURE\n");
    }
#endif
    catch (const subject::parse_error& e) { std::cout << "REJECT_GRAMMAR\n" << e.what() << '\n'; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
