#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
namespace athena::hash {
struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
// Binary-safe in-process SHA-256; returns sha256: plus 64 lowercase digits.
std::string sha256(std::string_view bytes);
}
