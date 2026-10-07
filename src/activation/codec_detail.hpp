// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "identity.hpp"
#include <initializer_list>
#include <toml++/toml.hpp>

namespace athena::activation::codec_detail {
toml::table parse(std::string_view, std::size_t limit = max_document_size);
void keys(const toml::table&, std::initializer_list<std::string_view>);
std::string string(const toml::table&, std::string_view);
std::uint64_t integer(const toml::table&, std::string_view);
void version(const toml::table&);
void canonical(std::string_view supplied, std::string_view written);
std::uint64_t decimal(std::string_view);
void generation(std::uint64_t);
void append_u64(std::string&, std::uint64_t);

class Writer {
public:
    explicit Writer(std::size_t limit = max_document_size) : limit_(limit) {}
    void append(std::string_view bytes);
    void text(std::string_view key, std::string_view value);
    void number(std::string_view key, std::uint64_t value);
    const std::string& bytes() const { return bytes_; }
private:
    std::string bytes_;
    std::size_t limit_;
};
}
