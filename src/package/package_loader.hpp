#pragma once

#include "package.hpp"

#include <filesystem>
#include <stdexcept>

namespace athena::package {

class PackageSchemaError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Package load_from_file(const std::filesystem::path& path);

}
