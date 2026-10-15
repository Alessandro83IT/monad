//# This file is a part of toml++ and is subject to the the terms of the MIT license.
//# Copyright (c) Mark Gillard <mark.gillard@outlook.com.au>
//# See https://github.com/marzer/tomlplusplus/blob/master/LICENSE for the full license text.
// SPDX-License-Identifier: MIT
#pragma once

#include "preprocessor.hpp"
#if ATHENA_TOML_ENABLE_PARSER

#include "std_except.hpp"
#include "source_region.hpp"
#include "print_to_stream.hpp"
#include "header_start.hpp"

#if ATHENA_TOML_DOXYGEN || !ATHENA_TOML_EXCEPTIONS
#define ATHENA_TOML_PARSE_ERROR_BASE
#else
#define ATHENA_TOML_PARSE_ERROR_BASE	 : public std::runtime_error
#endif

ATHENA_TOML_NAMESPACE_START
{
	ATHENA_TOML_ABI_NAMESPACE_BOOL(ATHENA_TOML_EXCEPTIONS, ex, noex);

	/// \brief	An error generated when parsing fails.
	///
	/// \remarks This class inherits from std::runtime_error when exceptions are enabled.
	/// 		 The public interface is the same regardless of exception mode.
	class parse_error ATHENA_TOML_PARSE_ERROR_BASE
	{
	  private:
#if !ATHENA_TOML_EXCEPTIONS
		std::string description_;
#endif
		source_region source_;

	  public:
#if ATHENA_TOML_EXCEPTIONS

		ATHENA_TOML_NODISCARD_CTOR
		ATHENA_TOML_ATTR(nonnull)
		parse_error(const char* desc, source_region&& src) noexcept //
			: std::runtime_error{ desc },
			  source_{ std::move(src) }
		{}

		ATHENA_TOML_NODISCARD_CTOR
		ATHENA_TOML_ATTR(nonnull)
		parse_error(const char* desc, const source_region& src) noexcept //
			: parse_error{ desc, source_region{ src } }
		{}

		ATHENA_TOML_NODISCARD_CTOR
		ATHENA_TOML_ATTR(nonnull)
		parse_error(const char* desc, const source_position& position, const source_path_ptr& path = {}) noexcept
			: parse_error{ desc, source_region{ position, position, path } }
		{}

#else

		ATHENA_TOML_NODISCARD_CTOR
		parse_error(std::string&& desc, source_region&& src) noexcept //
			: description_{ std::move(desc) },
			  source_{ std::move(src) }
		{}

		ATHENA_TOML_NODISCARD_CTOR
		parse_error(std::string&& desc, const source_region& src) noexcept //
			: parse_error{ std::move(desc), source_region{ src } }
		{}

		ATHENA_TOML_NODISCARD_CTOR
		parse_error(std::string&& desc, const source_position& position, const source_path_ptr& path = {}) noexcept
			: parse_error{ std::move(desc), source_region{ position, position, path } }
		{}

#endif

		/// \brief	Returns a textual description of the error.
		/// \remark The backing string is guaranteed to be null-terminated.
		ATHENA_TOML_NODISCARD
		std::string_view description() const noexcept
		{
#if ATHENA_TOML_EXCEPTIONS
			return std::string_view{ what() };
#else
			return description_;
#endif
		}

		/// \brief	Returns the region of the source document responsible for the error.
		ATHENA_TOML_NODISCARD
		const source_region& source() const noexcept
		{
			return source_;
		}

		/// \brief	Prints a parse_error to a stream.
		///
		/// \detail \cpp
		/// try
		/// {
		/// 	auto tbl = toml::parse("enabled = trUe"sv);
		/// }
		/// catch (const toml::parse_error & err)
		/// {
		/// 	std::cerr << "Parsing failed:\n"sv << err << "\n";
		/// }
		/// \ecpp
		///
		/// \out
		/// Parsing failed:
		/// Encountered unexpected character while parsing boolean; expected 'true', saw 'trU'
		///		(error occurred at line 1, column 13)
		/// \eout
		///
		/// \tparam Char The output stream's underlying character type. Must be 1 byte in size.
		/// \param 	lhs	The stream.
		/// \param 	rhs	The parse_error.
		///
		/// \returns	The input stream.
		friend std::ostream& operator<<(std::ostream& lhs, const parse_error& rhs)
		{
			impl::print_to_stream(lhs, rhs.description());
			impl::print_to_stream(lhs, "\n\t(error occurred at "sv);
			impl::print_to_stream(lhs, rhs.source());
			impl::print_to_stream(lhs, ")"sv);
			return lhs;
		}
	};

	ATHENA_TOML_ABI_NAMESPACE_END; // TOML_EXCEPTIONS
}
ATHENA_TOML_NAMESPACE_END;

#undef ATHENA_TOML_PARSE_ERROR_BASE

#include "header_end.hpp"
#endif // TOML_ENABLE_PARSER
