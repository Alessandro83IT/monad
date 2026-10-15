//# This file is a part of toml++ and is subject to the the terms of the MIT license.
//# Copyright (c) Mark Gillard <mark.gillard@outlook.com.au>
//# See https://github.com/marzer/tomlplusplus/blob/master/LICENSE for the full license text.
// SPDX-License-Identifier: MIT
#pragma once

#include "preprocessor.hpp"
ATHENA_TOML_DISABLE_WARNINGS;
#include <new>
ATHENA_TOML_ENABLE_WARNINGS;

#if (!defined(__apple_build_version__) && ATHENA_TOML_CLANG >= 8) || ATHENA_TOML_GCC >= 7 || ATHENA_TOML_ICC >= 1910 || ATHENA_TOML_MSVC >= 1914
#define ATHENA_TOML_LAUNDER(x) __builtin_launder(x)
#elif defined(__cpp_lib_launder) && __cpp_lib_launder >= 201606
#define ATHENA_TOML_LAUNDER(x) std::launder(x)
#else
#define ATHENA_TOML_LAUNDER(x) x
#endif
