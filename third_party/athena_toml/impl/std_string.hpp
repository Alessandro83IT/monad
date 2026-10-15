//# This file is a part of toml++ and is subject to the the terms of the MIT license.
//# Copyright (c) Mark Gillard <mark.gillard@outlook.com.au>
//# See https://github.com/marzer/tomlplusplus/blob/master/LICENSE for the full license text.
// SPDX-License-Identifier: MIT
#pragma once

#include "preprocessor.hpp"
ATHENA_TOML_DISABLE_WARNINGS;
#include <string_view>
#include <string>
ATHENA_TOML_ENABLE_WARNINGS;

#if ATHENA_TOML_DOXYGEN                                                                                                       \
	|| (defined(__cpp_char8_t) && __cpp_char8_t >= 201811 && defined(__cpp_lib_char8_t)                                \
		&& __cpp_lib_char8_t >= 201907)
#define ATHENA_TOML_HAS_CHAR8 1
#else
#define ATHENA_TOML_HAS_CHAR8 0
#endif

/// \cond

namespace athena_toml // non-abi namespace; this is not an error
{
	using namespace std::string_literals;
	using namespace std::string_view_literals;
}

#if ATHENA_TOML_ENABLE_WINDOWS_COMPAT

ATHENA_TOML_IMPL_NAMESPACE_START
{
	ATHENA_TOML_NODISCARD
	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	std::string narrow(std::wstring_view);

	ATHENA_TOML_NODISCARD
	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	std::wstring widen(std::string_view);

#if ATHENA_TOML_HAS_CHAR8

	ATHENA_TOML_NODISCARD
	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	std::wstring widen(std::u8string_view);

#endif
}
ATHENA_TOML_IMPL_NAMESPACE_END;

#endif // TOML_ENABLE_WINDOWS_COMPAT

/// \endcond
