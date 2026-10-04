#include "version.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace athena::version {

namespace {

struct ParsedVersion {
    unsigned long long epoch = 0;
    std::string pkgver;
    unsigned long long pkgrel = 0;
};

bool is_numeric(const std::string& value)
{
    return !value.empty() &&
           std::all_of(
               value.begin(),
               value.end(),
               [](unsigned char character) {
                   return std::isdigit(character) != 0;
               }
           );
}

bool valid_pkgver_syntax(const std::string& value);

ParsedVersion parse(const std::string& version)
{
    if (version.empty()) {
        throw std::runtime_error(
            "Versione vuota"
        );
    }

    ParsedVersion result;
    std::string value = version;

    /*
     * Arch epoch:
     *
     *     epoch:pkgver-pkgrel
     *
     * A missing epoch is equivalent to zero.
     */
    const auto epoch_separator = value.find(':');

    if (epoch_separator != std::string::npos) {

        const std::string epoch =
            value.substr(0, epoch_separator);

        if (!is_numeric(epoch)) {
            throw std::runtime_error(
                "Versione non valida: " + version
            );
        }

        try {
            result.epoch = std::stoull(epoch);
        }
        catch (const std::exception&) {
            throw std::runtime_error(
                "Versione non valida: " + version
            );
        }

        value = value.substr(epoch_separator + 1);
    }

    if (value.empty()) {
        throw std::runtime_error(
            "Versione non valida: " + version
        );
    }

    /*
     * Arch pkgrel is the final numeric component after '-'.
     *
     * We only interpret the final '-' component as pkgrel when it
     * is entirely numeric. This leaves versions such as "1.0-rc1"
     * available as ordinary pkgver strings.
     */
    const auto release_separator = value.rfind('-');

    if (release_separator != std::string::npos &&
        release_separator + 1 < value.size()) {

        const std::string release =
            value.substr(release_separator + 1);

        if (is_numeric(release)) {

            try {
                result.pkgrel = std::stoull(release);
            }
            catch (const std::exception&) {
                throw std::runtime_error(
                    "Versione non valida: " + version
                );
            }

            value = value.substr(0, release_separator);
        }
    }

    if (value.empty()) {
        throw std::runtime_error(
            "Versione non valida: " + version
        );
    }

    if (!valid_pkgver_syntax(value)) {
        throw std::runtime_error(
            "Versione non valida: " + version
        );
    }

    result.pkgver = std::move(value);

    return result;
}

struct Token {
    enum class Kind {
        Numeric,
        Alpha,
        Tilde
    };

    Kind kind;
    std::string value;
};

bool valid_pkgver_syntax(const std::string& value)
{
    if (value.empty()) {
        return false;
    }

    bool has_numeric_component = false;
    bool previous_was_separator = false;

    for (std::size_t position = 0; position < value.size(); ++position) {

        const unsigned char character =
            static_cast<unsigned char>(value[position]);

        if (std::isdigit(character)) {
            has_numeric_component = true;
            previous_was_separator = false;
            continue;
        }

        if (std::isalpha(character)) {
            previous_was_separator = false;
            continue;
        }

        /*
         * Arch version separators carry no ordering information,
         * but they must separate actual version components.
         */
        if (
            character == '.' ||
            character == '+' ||
            character == '_' ||
            character == '-'
        ) {
            if (
                position == 0 ||
                position + 1 == value.size() ||
                previous_was_separator
            ) {
                return false;
            }

            previous_was_separator = true;
            continue;
        }

        /*
         * Tilde is an ordering marker for prerelease versions.
         * It must be followed by another version component.
         */
        if (character == '~') {
            if (
                position + 1 == value.size() ||
                previous_was_separator
            ) {
                return false;
            }

            previous_was_separator = false;
            continue;
        }

        return false;
    }

    return has_numeric_component && !previous_was_separator;
}

std::vector<Token> tokenize(const std::string& value)
{
    std::vector<Token> tokens;

    std::size_t position = 0;

    while (position < value.size()) {

        const unsigned char character =
            static_cast<unsigned char>(value[position]);

        if (character == '~') {
            tokens.push_back({
                Token::Kind::Tilde,
                "~"
            });

            ++position;
            continue;
        }

        if (std::isdigit(character)) {

            const std::size_t begin = position;

            while (
                position < value.size() &&
                std::isdigit(
                    static_cast<unsigned char>(value[position])
                )
            ) {
                ++position;
            }

            tokens.push_back({
                Token::Kind::Numeric,
                value.substr(begin, position - begin)
            });

            continue;
        }

        if (std::isalpha(character)) {

            const std::size_t begin = position;

            while (
                position < value.size() &&
                std::isalpha(
                    static_cast<unsigned char>(value[position])
                )
            ) {
                ++position;
            }

            tokens.push_back({
                Token::Kind::Alpha,
                value.substr(begin, position - begin)
            });

            continue;
        }

        /*
         * Arch version separators such as '.', '+', '_' and '-'
         * do not carry ordering information by themselves.
         */
        ++position;
    }

    return tokens;
}

int compare_pkgver(
    const std::string& left,
    const std::string& right
)
{
    const auto left_tokens = tokenize(left);
    const auto right_tokens = tokenize(right);

    std::size_t index = 0;

    while (
        index < left_tokens.size() ||
        index < right_tokens.size()
    ) {

        if (index >= left_tokens.size()) {

            const auto& right_token = right_tokens[index];

            /*
             * Missing trailing numeric zeroes do not change the
             * semantic version. Therefore:
             *
             *     1.2 == 1.2.0
             *
             * A trailing alphabetic component or '~' still carries
             * ordering information.
             */
            if (right_token.kind == Token::Kind::Numeric) {

                const auto trim_zeroes =
                    [](const std::string& value) {
                        const auto position =
                            value.find_first_not_of('0');

                        return position == std::string::npos
                            ? std::string("0")
                            : value.substr(position);
                    };

                if (trim_zeroes(right_token.value) == "0") {
                    ++index;
                    continue;
                }
            }

            if (right_token.kind == Token::Kind::Tilde) {
                return 1;
            }

            return -1;
        }

        if (index >= right_tokens.size()) {

            const auto& left_token = left_tokens[index];

            /*
             * Symmetric handling for trailing numeric zeroes:
             *
             *     1.2.0 == 1.2
             */
            if (left_token.kind == Token::Kind::Numeric) {

                const auto trim_zeroes =
                    [](const std::string& value) {
                        const auto position =
                            value.find_first_not_of('0');

                        return position == std::string::npos
                            ? std::string("0")
                            : value.substr(position);
                    };

                if (trim_zeroes(left_token.value) == "0") {
                    ++index;
                    continue;
                }
            }

            if (left_token.kind == Token::Kind::Tilde) {
                return -1;
            }

            return 1;
        }

        const auto& left_token = left_tokens[index];
        const auto& right_token = right_tokens[index];

        if (
            left_token.kind == Token::Kind::Tilde ||
            right_token.kind == Token::Kind::Tilde
        ) {

            if (
                left_token.kind == Token::Kind::Tilde &&
                right_token.kind == Token::Kind::Tilde
            ) {
                ++index;
                continue;
            }

            return left_token.kind == Token::Kind::Tilde
                ? -1
                : 1;
        }

        if (
            left_token.kind == Token::Kind::Numeric &&
            right_token.kind == Token::Kind::Numeric
        ) {

            const auto trim_zeroes =
                [](const std::string& value) {
                    const auto position =
                        value.find_first_not_of('0');

                    return position == std::string::npos
                        ? std::string("0")
                        : value.substr(position);
                };

            const std::string left_number =
                trim_zeroes(left_token.value);

            const std::string right_number =
                trim_zeroes(right_token.value);

            if (left_number.size() != right_number.size()) {
                return left_number.size() < right_number.size()
                    ? -1
                    : 1;
            }

            if (left_number != right_number) {
                return left_number < right_number
                    ? -1
                    : 1;
            }
        }
        else if (
            left_token.kind == Token::Kind::Alpha &&
            right_token.kind == Token::Kind::Alpha
        ) {

            if (left_token.value != right_token.value) {
                return left_token.value < right_token.value
                    ? -1
                    : 1;
            }
        }
        else {

            /*
             * Numeric components sort after alphabetic components.
             * This matches the important Arch distinction between
             * versions such as "1.0a" and "1.0".
             */
            if (
                left_token.kind == Token::Kind::Numeric &&
                right_token.kind == Token::Kind::Alpha
            ) {
                return 1;
            }

            if (
                left_token.kind == Token::Kind::Alpha &&
                right_token.kind == Token::Kind::Numeric
            ) {
                return -1;
            }
        }

        ++index;
    }

    return 0;
}

int compare(
    const std::string& left,
    const std::string& right
)
{
    const auto left_version = parse(left);
    const auto right_version = parse(right);

    if (left_version.epoch != right_version.epoch) {
        return left_version.epoch < right_version.epoch
            ? -1
            : 1;
    }

    const int pkgver_comparison =
        compare_pkgver(
            left_version.pkgver,
            right_version.pkgver
        );

    if (pkgver_comparison != 0) {
        return pkgver_comparison;
    }

    if (left_version.pkgrel != right_version.pkgrel) {
        return left_version.pkgrel < right_version.pkgrel
            ? -1
            : 1;
    }

    return 0;
}

}

bool less(
    const std::string& left,
    const std::string& right
)
{
    return compare(left, right) < 0;
}

bool greater(
    const std::string& left,
    const std::string& right
)
{
    return compare(left, right) > 0;
}

}
