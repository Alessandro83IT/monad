// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "parser.hpp"

#include "constraint.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace athena::dependency {

namespace {

class Parser {
public:

    explicit Parser(const std::string& input)
        : text(input)
    {
    }

    ExpressionPtr parse()
    {
        skip_whitespace();

        if (position >= text.size()) {
            throw std::runtime_error(
                "Espressione di dipendenza vuota"
            );
        }

        auto expression = parse_or();

        skip_whitespace();

        if (position != text.size()) {
            throw std::runtime_error(
                "Token inatteso nella dipendenza '" +
                text +
                "'"
            );
        }

        return expression;
    }

private:

    ExpressionPtr parse_or()
    {
        std::vector<ExpressionPtr> expressions;

        expressions.push_back(parse_and());

        while (consume_keyword("OR")) {
            expressions.push_back(parse_and());
        }

        if (expressions.size() == 1) {
            return expressions.front();
        }

        return std::make_shared<const OrExpression>(
            std::move(expressions)
        );
    }

    ExpressionPtr parse_and()
    {
        std::vector<ExpressionPtr> expressions;

        expressions.push_back(parse_unary());

        while (consume_keyword("AND")) {
            expressions.push_back(parse_unary());
        }

        if (expressions.size() == 1) {
            return expressions.front();
        }

        return std::make_shared<const AndExpression>(
            std::move(expressions)
        );
    }

    ExpressionPtr parse_unary()
    {
        if (consume_keyword("NOT")) {
            return std::make_shared<const NotExpression>(
                parse_unary()
            );
        }

        return parse_primary();
    }

    ExpressionPtr parse_primary()
    {
        skip_whitespace();

        if (starts_with_keyword("when")) return parse_when();
        if (starts_with_scope()) return parse_scope();

        if (consume_character('(')) {

            auto expression = parse_or();

            skip_whitespace();

            if (!consume_character(')')) {
                throw std::runtime_error(
                    "Parentesi ')' mancante nella dipendenza '" +
                    text +
                    "'"
                );
            }

            return expression;
        }

        return parse_dependency_atom();
    }

    ExpressionPtr parse_dependency_atom()
    {
        const std::string package = parse_identifier();

        if (package.empty()) {
            throw std::runtime_error(
                "Nome del pacchetto mancante nella dipendenza '" +
                text +
                "'"
            );
        }

        if (package == "true") return std::make_shared<const TrueExpression>();
        if (package == "false") return std::make_shared<const FalseExpression>();
        ExpressionPtr reference;
        if (consume_character('(')) {
            const auto value = parse_reference_argument();
            if (package == "capability" || package == "cap") reference = std::make_shared<const CapabilityRef>(value);
            else if (package == "feature") reference = std::make_shared<const FeatureRef>(value);
            else if (package == "platform") reference = std::make_shared<const PlatformRef>(value);
            else if (package == "architecture" || package == "arch") reference = std::make_shared<const ArchitectureRef>(value);
            else if (package == "abi") reference = std::make_shared<const ABIRef>(value);
            else throw std::runtime_error("Riferimento funzione non supportato: " + package);
        }
        else reference = std::make_shared<const PackageRef>(package);

        skip_whitespace();

        if (consume_character('{')) {
            auto constraint = parse_constraint_or();
            if (!consume_character('}')) throw std::runtime_error("Chiusura vincolo versione mancante");
            return std::make_shared<const VersionComparison>(reference,
                std::make_shared<const VersionConstraint>(std::move(constraint)));
        }

        if (position >= text.size() ||
            text[position] == ')' ||
            text[position] == ',' ||
            starts_with_keyword("AND") ||
            starts_with_keyword("OR"))
        {
            return reference;
        }

        const auto op = parse_operator();

        skip_whitespace();

        const std::string version = parse_version();

        if (version.empty()) {
            throw std::runtime_error(
                "Versione mancante nella dipendenza '" +
                text +
                "'"
            );
        }

        auto constraint =
            std::make_shared<const VersionConstraint>(
                VersionConstraint::comparison(
                    op,
                    version
                )
            );

        return std::make_shared<const VersionComparison>(
            reference,
            constraint
        );
    }

    std::string parse_reference_argument() {
        skip_whitespace();
        const auto start = position;
        while (position < text.size() && text[position] != ')') ++position;
        const auto value = trim(text.substr(start, position - start));
        if (!consume_character(')') || value.empty()) throw std::runtime_error("Riferimento vuoto o parentesi mancante");
        return value;
    }

    VersionConstraint parse_constraint_or() {
        std::vector<VersionConstraint> parts{parse_constraint_and()};
        while (consume_keyword("OR")) parts.push_back(parse_constraint_and());
        return parts.size() == 1 ? parts.front() : VersionConstraint::any_of(std::move(parts));
    }
    VersionConstraint parse_constraint_and() {
        std::vector<VersionConstraint> parts{parse_constraint_unary()};
        while (consume_keyword("AND")) parts.push_back(parse_constraint_unary());
        return parts.size() == 1 ? parts.front() : VersionConstraint::all(std::move(parts));
    }
    VersionConstraint parse_constraint_unary() {
        if (consume_keyword("NOT")) return VersionConstraint::not_(parse_constraint_unary());
        if (consume_character('(')) {
            auto nested = parse_constraint_or();
            if (!consume_character(')')) throw std::runtime_error("Parentesi constraint mancante");
            return nested;
        }
        const auto op = parse_operator();
        skip_whitespace();
        const auto value = parse_version();
        if (value.empty()) throw std::runtime_error("Versione constraint vuota");
        return VersionConstraint::comparison(op, value);
    }

    bool starts_with_scope() const {
        static const char* roles[] = {"runtime", "build", "test", "optional", "development", "propagated", "optional-disabled"};
        for (const auto* role : roles) {
            const std::string prefix = std::string(role) + "(";
            if (text.compare(position, prefix.size(), prefix) == 0) return true;
        }
        return false;
    }

    ExpressionPtr parse_scope() {
        const auto start = position;
        while (position < text.size() && text[position] != '(') ++position;
        const auto role = text.substr(start, position - start);
        ++position;
        const auto context_start = position;
        while (position < text.size() && text[position] != ')') ++position;
        const auto context_name = text.substr(context_start, position - context_start);
        if (!consume_character(')') || !consume_character(':') || !consume_character('(')) throw std::runtime_error("Scope ruolo/contesto non valida");
        DependencyKind kind;
        bool enabled = true;
        if (role == "runtime") kind = DependencyKind::Runtime;
        else if (role == "build") kind = DependencyKind::Build;
        else if (role == "test") kind = DependencyKind::Test;
        else if (role == "optional") kind = DependencyKind::Optional;
        else if (role == "optional-disabled") { kind = DependencyKind::Optional; enabled = false; }
        else if (role == "development") kind = DependencyKind::Development;
        else if (role == "propagated") kind = DependencyKind::Propagated;
        else throw std::runtime_error("Ruolo non supportato");
        DependencyContext context;
        if (context_name == "host") context = DependencyContext::Host;
        else if (context_name == "build") context = DependencyContext::Build;
        else if (context_name == "target") context = DependencyContext::Target;
        else throw std::runtime_error("Contesto non supportato");
        auto body = parse_or();
        if (!consume_character(')')) throw std::runtime_error("Parentesi scope mancante");
        return std::make_shared<const ScopedExpression>(kind, context, std::move(body), enabled);
    }

    ExpressionPtr parse_when() {
        position += 4;
        if (!consume_character('(')) throw std::runtime_error("when richiede '('");
        auto condition = parse_or();
        if (!consume_character(',')) throw std::runtime_error("when richiede separatore ','");
        auto body = parse_or();
        if (!consume_character(')')) throw std::runtime_error("when richiede ')'");
        return std::make_shared<const ConditionalExpression>(std::move(condition), std::move(body));
    }

    std::string parse_identifier()
    {
        skip_whitespace();

        const std::size_t start = position;

        while (position < text.size()) {

            const char character = text[position];

            if (character == ' ' ||
                character == '\t' ||
                character == '(' ||
                character == ')' ||
                character == '<' ||
                character == '>' ||
                character == '=' ||
                character == '!' || character == ',' || character == ':')
            {
                break;
            }

            ++position;
        }

        return text.substr(
            start,
            position - start
        );
    }

    std::string parse_version()
    {
        const std::size_t start = position;

        while (position < text.size()) {

            if (text[position] == ')' || text[position] == '}') {
                break;
            }

            if (starts_with_keyword("AND") ||
                starts_with_keyword("OR"))
            {
                break;
            }

            ++position;
        }

        std::string version =
            text.substr(
                start,
                position - start
            );

        return trim(version);
    }

    ComparisonOperator parse_operator()
    {
        skip_whitespace();

        if (consume_string(">=")) {
            return ComparisonOperator::GreaterEqual;
        }

        if (consume_string("<=")) {
            return ComparisonOperator::LessEqual;
        }

        if (consume_string("!=")) {
            return ComparisonOperator::NotEqual;
        }

        if (consume_string("=")) {
            return ComparisonOperator::Equal;
        }

        if (consume_string(">")) {
            return ComparisonOperator::Greater;
        }

        if (consume_string("<")) {
            return ComparisonOperator::Less;
        }

        throw std::runtime_error(
            "Operatore di versione non valido nella dipendenza '" +
            text +
            "'"
        );
    }

    bool consume_keyword(const std::string& keyword)
    {
        skip_whitespace();

        if (!starts_with_keyword(keyword)) {
            return false;
        }

        position += keyword.size();

        return true;
    }

    bool starts_with_keyword(
        const std::string& keyword
    ) const
    {
        if (text.compare(
                position,
                keyword.size(),
                keyword
            ) != 0)
        {
            return false;
        }

        const std::size_t end =
            position + keyword.size();

        if (end < text.size()) {

            const char next = text[end];

            if (next != ' ' &&
                next != '\t' &&
                next != '(' &&
                next != ')')
            {
                return false;
            }
        }

        return true;
    }

    bool consume_character(char character)
    {
        skip_whitespace();

        if (position >= text.size() ||
            text[position] != character)
        {
            return false;
        }

        ++position;

        return true;
    }

    bool consume_string(const std::string& value)
    {
        if (text.compare(
                position,
                value.size(),
                value
            ) != 0)
        {
            return false;
        }

        position += value.size();

        return true;
    }

    void skip_whitespace()
    {
        while (position < text.size() &&
               (text[position] == ' ' ||
                text[position] == '\t'))
        {
            ++position;
        }
    }

    static std::string trim(
        const std::string& value
    )
    {
        const auto first =
            value.find_first_not_of(" \t");

        if (first == std::string::npos) {
            return "";
        }

        const auto last =
            value.find_last_not_of(" \t");

        return value.substr(
            first,
            last - first + 1
        );
    }

    const std::string& text;
    std::size_t position = 0;
};

}

Dependency parse_dependency(const std::string& text)
{
    const auto colon = text.find(':');
    const bool role_header = colon != std::string::npos &&
        (text.rfind("runtime(", 0) == 0 || text.rfind("build(", 0) == 0 ||
         text.rfind("test(", 0) == 0 || text.rfind("optional(", 0) == 0 ||
         text.rfind("optional-disabled(", 0) == 0 || text.rfind("development(", 0) == 0 ||
         text.rfind("propagated(", 0) == 0);
    if (role_header) {
        const auto header = text.substr(0, colon);
        const auto open = header.find('(');
        if (open == std::string::npos || header.back() != ')')
            throw std::runtime_error("Intestazione ruolo/contesto non valida");
        const auto role = header.substr(0, open);
        const auto ctx = header.substr(open + 1, header.size() - open - 2);
        DependencyKind kind;
        bool enabled = true;
        if (role == "optional-disabled") { kind = DependencyKind::Optional; enabled = false; }
        else if (role == "runtime") kind = DependencyKind::Runtime;
        else if (role == "build") kind = DependencyKind::Build;
        else if (role == "test") kind = DependencyKind::Test;
        else if (role == "optional") kind = DependencyKind::Optional;
        else if (role == "development") kind = DependencyKind::Development;
        else if (role == "propagated") kind = DependencyKind::Propagated;
        else throw std::runtime_error("Ruolo dipendenza non supportato");
        DependencyContext context;
        if (ctx == "host") context = DependencyContext::Host;
        else if (ctx == "build") context = DependencyContext::Build;
        else if (ctx == "target") context = DependencyContext::Target;
        else throw std::runtime_error("Contesto dipendenza non supportato");
        const auto expression_text = text.substr(colon + 1);
        Parser parser(expression_text);
        return {parser.parse(), kind, context, enabled};
    }
    Parser parser(text);

    return Dependency{
        parser.parse(),
        DependencyKind::Runtime,
        DependencyContext::Target
    };
}

} // namespace athena::dependency
