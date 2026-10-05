#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
inline void require(bool value, const char* message)
{ if (!value) throw std::runtime_error(message); }
inline std::string unhex(std::string_view hex)
{
    require(hex.size() % 2 == 0, "bad fixture hex");
    const auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        throw std::runtime_error("bad fixture hex digit");
    };
    std::string result;
    for (std::size_t i = 0; i < hex.size(); i += 2)
        result.push_back(static_cast<char>(digit(hex[i]) * 16 + digit(hex[i + 1])));
    return result;
}
