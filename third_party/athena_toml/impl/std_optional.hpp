//# This file is a part of toml++ and is subject to the the terms of the MIT license.
//# Copyright (c) Mark Gillard <mark.gillard@outlook.com.au>
//# See https://github.com/marzer/tomlplusplus/blob/master/LICENSE for the full license text.
// SPDX-License-Identifier: MIT
#pragma once

#include "preprocessor.hpp"
ATHENA_TOML_DISABLE_WARNINGS;
#if !ATHENA_TOML_HAS_CUSTOM_OPTIONAL_TYPE
#include <optional>
#endif
ATHENA_TOML_ENABLE_WARNINGS;

ATHENA_TOML_NAMESPACE_START
{
#if ATHENA_TOML_HAS_CUSTOM_OPTIONAL_TYPE

	template <typename T>
	using optional = ATHENA_TOML_OPTIONAL_TYPE<T>;

#else

	/// \brief	The 'optional' type used throughout the library.
	///
	/// \remarks By default this will be an alias for std::optional, but you can change the optional type
	/// 		 used by the library by defining #TOML_OPTIONAL_TYPE.
	template <typename T>
	using optional = std::optional<T>;

#endif
}
ATHENA_TOML_NAMESPACE_END;
