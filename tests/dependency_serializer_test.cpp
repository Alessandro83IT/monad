// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/dependency/parser.hpp"
#include "../src/dependency/serializer.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace athena::dependency;

namespace {

void assert_round_trip(const std::string& input)
{
    const auto first = parse_dependency(input);
    const auto serialized = serialize_dependency(first);
    const auto second = parse_dependency(serialized);

    assert(second.expression != nullptr);
    assert(
        first.expression->kind ==
        second.expression->kind
    );

    /*
     * A second serialization must be stable. This catches cases where
     * the first serialization accidentally relies on parser normalization
     * that changes again on subsequent passes.
     */
    assert(
        serialize_dependency(second) ==
        serialized
    );
}

}

int main()
{
    {
        const auto dependency =
            parse_dependency("zlib");

        assert(
            serialize_dependency(dependency) ==
            "zlib"
        );
    }

    {
        const auto dependency =
            parse_dependency("zlib >= 1.2");

        assert(
            serialize_dependency(dependency) ==
            "zlib >= 1.2"
        );
    }

    {
        const auto dependency =
            parse_dependency(
                "zlib >= 1.2 AND openssl >= 3.0"
            );

        assert(
            serialize_dependency(dependency) ==
            "zlib >= 1.2 AND openssl >= 3.0"
        );
    }

    {
        const auto dependency =
            parse_dependency(
                "zlib OR openssl AND curl"
            );

        /*
         * AND binds more tightly than OR, so no parentheses are
         * necessary around the right-hand expression.
         */
        assert(
            serialize_dependency(dependency) ==
            "zlib OR openssl AND curl"
        );
    }

    {
        const auto dependency =
            parse_dependency(
                "(zlib OR openssl) AND curl"
            );

        /*
         * Here the parentheses are semantically necessary because
         * OR is nested inside an AND expression.
         */
        assert(
            serialize_dependency(dependency) ==
            "(zlib OR openssl) AND curl"
        );
    }

    {
        const auto dependency =
            parse_dependency(
                "NOT (zlib OR openssl)"
            );

        assert(
            serialize_dependency(dependency) ==
            "NOT (zlib OR openssl)"
        );
    }

    {
        const auto dependency =
            parse_dependency(
                "NOT zlib AND openssl"
            );

        assert(
            serialize_dependency(dependency) ==
            "NOT zlib AND openssl"
        );
    }

    assert_round_trip("zlib");
    assert_round_trip("zlib >= 1.2");
    assert_round_trip("zlib >= 1.2 AND openssl >= 3.0");
    assert_round_trip("zlib OR openssl AND curl");
    assert_round_trip("(zlib OR openssl) AND curl");
    assert_round_trip("NOT (zlib OR openssl)");
    assert_round_trip("NOT zlib AND openssl");

    std::cout
        << "Dependency serializer tests passed.\n";

    return 0;
}
