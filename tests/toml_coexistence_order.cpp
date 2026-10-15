// SPDX-License-Identifier: GPL-3.0-or-later
#if defined(ATHENA_FORK_FIRST)
#include "parsing/bounded_toml.hpp"
#include <toml++/toml.hpp>
#define ORDER_FUNCTION isolated_toml_athena_first
#else
#include <toml++/toml.hpp>
#include "parsing/bounded_toml.hpp"
#define ORDER_FUNCTION isolated_toml_upstream_first
#endif
#include <type_traits>
static_assert(!std::is_same_v<toml::node, athena_toml::node>);
static_assert(!std::is_same_v<toml::table, athena_toml::table>);
static_assert(!std::is_same_v<toml::value<std::string>, athena_toml::value<std::string>>);
static_assert(!std::is_same_v<toml::parse_error, athena_toml::parse_error>);
static_assert(TOML_LIB_MAJOR == 3 && ATHENA_TOML_LIB_MAJOR == 3);
bool ORDER_FUNCTION() {
    const auto upstream = toml::parse("key = [{value='☃'}]");
    const auto fork = athena::parsing::parse("key = [{value='☃'}]", athena::parsing::Profile::Any);
    return upstream["key"][0]["value"].value<std::string>() ==
           fork["key"][0]["value"].value<std::string>();
}
