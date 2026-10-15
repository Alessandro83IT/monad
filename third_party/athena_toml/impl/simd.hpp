//# This file is a part of toml++ and is subject to the the terms of the MIT license.
//# Copyright (c) Mark Gillard <mark.gillard@outlook.com.au>
//# See https://github.com/marzer/tomlplusplus/blob/master/LICENSE for the full license text.
// SPDX-License-Identifier: MIT
#pragma once

#include "preprocessor.hpp"
#if ATHENA_TOML_ENABLE_SIMD

#if defined(__SSE2__)                                                                                                  \
	|| (defined(_MSC_VER) && (defined(_M_AMD64) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)))
#define ATHENA_TOML_HAS_SSE2 1
#endif

#if defined(__SSE4_1__) || (defined(_MSC_VER) && (defined(__AVX__) || defined(__AVX2__)))
#define ATHENA_TOML_HAS_SSE4_1 1
#endif

#endif // TOML_ENABLE_SIMD

#ifndef ATHENA_TOML_HAS_SSE2
#define ATHENA_TOML_HAS_SSE2 0
#endif
#ifndef ATHENA_TOML_HAS_SSE4_1
#define ATHENA_TOML_HAS_SSE4_1 0
#endif

ATHENA_TOML_DISABLE_WARNINGS;
#if ATHENA_TOML_HAS_SSE4_1
#include <smmintrin.h>
#endif
#if ATHENA_TOML_HAS_SSE2
#include <emmintrin.h>
#endif
ATHENA_TOML_ENABLE_WARNINGS;
