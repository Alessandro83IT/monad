//# This file is a part of toml++ and is subject to the the terms of the MIT license.
//# Copyright (c) Mark Gillard <mark.gillard@outlook.com.au>
//# See https://github.com/marzer/tomlplusplus/blob/master/LICENSE for the full license text.
// SPDX-License-Identifier: MIT
#pragma once

#include "std_string.hpp"
#include "forward_declarations.hpp"
#include "header_start.hpp"

ATHENA_TOML_IMPL_NAMESPACE_START
{
	// Q: "why does print_to_stream() exist? why not just use ostream::write(), ostream::put() etc?"
	// A: - I'm using <charconv> to format numerics. Faster and locale-independent.
	//    - I can (potentially) avoid forcing users to drag in <sstream> and <iomanip>.
	//    - Strings in C++. Honestly.

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	ATHENA_TOML_ATTR(nonnull)
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, const char*, size_t);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, std::string_view);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, const std::string&);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, char);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, signed char, value_flags = {}, size_t min_digits = 0);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, signed short, value_flags = {}, size_t min_digits = 0);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, signed int, value_flags = {}, size_t min_digits = 0);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, signed long, value_flags = {}, size_t min_digits = 0);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, signed long long, value_flags = {}, size_t min_digits = 0);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, unsigned char, value_flags = {}, size_t min_digits = 0);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, unsigned short, value_flags = {}, size_t min_digits = 0);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, unsigned int, value_flags = {}, size_t min_digits = 0);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, unsigned long, value_flags = {}, size_t min_digits = 0);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, unsigned long long, value_flags = {}, size_t min_digits = 0);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, float, value_flags = {}, bool relaxed_precision = false);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, double, value_flags = {}, bool relaxed_precision = false);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, bool);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, const athena_toml::date&);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, const athena_toml::time&);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, const athena_toml::time_offset&);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, const athena_toml::date_time&);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, const source_position&);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, const source_region&);

#if ATHENA_TOML_ENABLE_FORMATTERS

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, const array&);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, const table&);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, const value<std::string>&);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, const value<int64_t>&);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, const value<double>&);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, const value<bool>&);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, const value<date>&);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, const value<time>&);

	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	void ATHENA_TOML_CALLCONV print_to_stream(std::ostream&, const value<date_time>&);

#endif

	template <typename T, typename U>
	inline void print_to_stream_bookended(std::ostream & stream, const T& val, const U& bookend)
	{
		print_to_stream(stream, bookend);
		print_to_stream(stream, val);
		print_to_stream(stream, bookend);
	}
}
ATHENA_TOML_IMPL_NAMESPACE_END;

#include "header_end.hpp"
