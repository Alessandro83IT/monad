//# This file is a part of toml++ and is subject to the the terms of the MIT license.
//# Copyright (c) Mark Gillard <mark.gillard@outlook.com.au>
//# See https://github.com/marzer/tomlplusplus/blob/master/LICENSE for the full license text.
// SPDX-License-Identifier: MIT
#pragma once

#include <athena_toml/impl/preprocessor.hpp>
#if ATHENA_TOML_ENABLE_PARSER

#include <athena_toml/impl/table.hpp>
#include <athena_toml/impl/parse_result.hpp>
#include <athena_toml/impl/header_start.hpp>

// Athena extension: operation-owned authorization before parser allocations.
namespace athena_toml {
struct parse_observer {
 virtual ~parse_observer() = default;
 virtual void root(const table&) = 0;
 virtual void key_start(const table&) = 0;
 virtual void key_segment(std::string_view, bool) = 0;
 virtual void key(const table&, std::string_view) = 0;
 virtual void table_child(const table&, std::string_view, bool) = 0;
 virtual void node(node_type) = 0;
 virtual void attach(const athena_toml::node&) = 0;
 virtual void element(const array&) = 0;
 virtual void complete(const table&) = 0;
 virtual void text(std::size_t, bool) = 0;
 virtual void key_storage(std::size_t) = 0;
 virtual void depth(std::size_t) = 0;
};
}

ATHENA_TOML_NAMESPACE_START
{
	ATHENA_TOML_ABI_NAMESPACE_BOOL(ATHENA_TOML_EXCEPTIONS, ex, noex);
	parse_result ATHENA_TOML_CALLCONV parse(std::string_view, parse_observer&);

	/// \brief	Parses a TOML document from a string view.
	///
	/// \detail \cpp
	/// auto tbl = toml::parse("a = 3"sv);
	/// std::cout << tbl["a"] << "\n";
	/// \ecpp
	///
	/// \out
	/// 3
	/// \eout
	///
	/// \param 	doc				The TOML document to parse. Must be valid UTF-8.
	/// \param 	source_path		The path used to initialize each node's `source().path`.
	/// 						If you don't have a path (or you have no intention of using paths in diagnostics)
	/// 						then this parameter can safely be left blank.
	///
	/// \returns	\conditional_return{With exceptions}
	///				A toml::table.
	/// 			\conditional_return{Without exceptions}
	///				A toml::parse_result.
	ATHENA_TOML_NODISCARD
	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	parse_result ATHENA_TOML_CALLCONV parse(std::string_view doc, std::string_view source_path = {});

	/// \brief	Parses a TOML document from a string view.
	///
	/// \detail \cpp
	/// auto tbl = toml::parse("a = 3"sv, "foo.toml");
	/// std::cout << tbl["a"] << "\n";
	/// \ecpp
	///
	/// \out
	/// 3
	/// \eout
	///
	/// \param 	doc				The TOML document to parse. Must be valid UTF-8.
	/// \param 	source_path		The path used to initialize each node's `source().path`.
	/// 						If you don't have a path (or you have no intention of using paths in diagnostics)
	/// 						then this parameter can safely be left blank.
	///
	/// \returns	\conditional_return{With exceptions}
	///				A toml::table.
	/// 			\conditional_return{Without exceptions}
	///				A toml::parse_result.
	ATHENA_TOML_NODISCARD
	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	parse_result ATHENA_TOML_CALLCONV parse(std::string_view doc, std::string && source_path);

	/// \brief	Parses a TOML document from a file.
	///
	/// \detail \cpp
	/// toml::parse_result get_foo_toml()
	/// {
	///		return toml::parse_file("foo.toml");
	/// }
	/// \ecpp
	///
	/// \param 	file_path		The TOML document to parse. Must be valid UTF-8.
	///
	/// \returns	\conditional_return{With exceptions}
	///				A toml::table.
	/// 			\conditional_return{Without exceptions}
	///				A toml::parse_result.
	ATHENA_TOML_NODISCARD
	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	parse_result ATHENA_TOML_CALLCONV parse_file(std::string_view file_path);

#if ATHENA_TOML_HAS_CHAR8

	/// \brief	Parses a TOML document from a char8_t string view.
	///
	/// \detail \cpp
	/// auto tbl = toml::parse(u8"a = 3"sv);
	/// std::cout << tbl["a"] << "\n";
	/// \ecpp
	///
	/// \out
	/// 3
	/// \eout
	///
	/// \param 	doc				The TOML document to parse. Must be valid UTF-8.
	/// \param 	source_path		The path used to initialize each node's `source().path`.
	/// 						If you don't have a path (or you have no intention of using paths in diagnostics)
	/// 						then this parameter can safely be left blank.
	///
	/// \returns	\conditional_return{With exceptions}
	///				A toml::table.
	/// 			\conditional_return{Without exceptions}
	///				A toml::parse_result.
	ATHENA_TOML_NODISCARD
	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	parse_result ATHENA_TOML_CALLCONV parse(std::u8string_view doc, std::string_view source_path = {});

	/// \brief	Parses a TOML document from a char8_t string view.
	///
	/// \detail \cpp
	/// auto tbl = toml::parse(u8"a = 3"sv, "foo.toml");
	/// std::cout << tbl["a"] << "\n";
	/// \ecpp
	///
	/// \out
	/// 3
	/// \eout
	///
	/// \param 	doc				The TOML document to parse. Must be valid UTF-8.
	/// \param 	source_path		The path used to initialize each node's `source().path`.
	/// 						If you don't have a path (or you have no intention of using paths in diagnostics)
	/// 						then this parameter can safely be left blank.
	///
	/// \returns	\conditional_return{With exceptions}
	///				A toml::table.
	/// 			\conditional_return{Without exceptions}
	///				A toml::parse_result.
	ATHENA_TOML_NODISCARD
	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	parse_result ATHENA_TOML_CALLCONV parse(std::u8string_view doc, std::string && source_path);

	/// \brief	Parses a TOML document from a file.
	///
	/// \detail \cpp
	/// toml::parse_result get_foo_toml()
	/// {
	///		return toml::parse_file(u8"foo.toml");
	/// }
	/// \ecpp
	///
	/// \param 	file_path		The TOML document to parse. Must be valid UTF-8.
	///
	/// \returns	\conditional_return{With exceptions}
	///				A toml::table.
	/// 			\conditional_return{Without exceptions}
	///				A toml::parse_result.
	ATHENA_TOML_NODISCARD
	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	parse_result ATHENA_TOML_CALLCONV parse_file(std::u8string_view file_path);

#endif // TOML_HAS_CHAR8

#if ATHENA_TOML_ENABLE_WINDOWS_COMPAT

	/// \brief	Parses a TOML document from a string view.
	///
	/// \availability This overload is only available when #TOML_ENABLE_WINDOWS_COMPAT is enabled.
	///
	/// \detail \cpp
	/// auto tbl = toml::parse("a = 3"sv, L"foo.toml");
	/// std::cout << tbl["a"] << "\n";
	/// \ecpp
	///
	/// \out
	/// 3
	/// \eout
	///
	/// \param 	doc				The TOML document to parse. Must be valid UTF-8.
	/// \param 	source_path		The path used to initialize each node's `source().path`.
	/// 						If you don't have a path (or you have no intention of using paths in diagnostics)
	/// 						then this parameter can safely be left blank.
	///
	/// \returns	\conditional_return{With exceptions}
	///				A toml::table.
	/// 			\conditional_return{Without exceptions}
	///				A toml::parse_result.
	ATHENA_TOML_NODISCARD
	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	parse_result ATHENA_TOML_CALLCONV parse(std::string_view doc, std::wstring_view source_path);

	/// \brief	Parses a TOML document from a stream.
	///
	/// \availability This overload is only available when #TOML_ENABLE_WINDOWS_COMPAT is enabled.
	///
	/// \detail \cpp
	/// std::stringstream ss;
	/// ss << "a = 3"sv;
	///
	/// auto tbl = toml::parse(ss);
	/// std::cout << tbl["a"] << "\n";
	/// \ecpp
	///
	/// \out
	/// 3
	/// \eout
	///
	/// \param 	doc				The TOML document to parse. Must be valid UTF-8.
	/// \param 	source_path		The path used to initialize each node's `source().path`.
	/// 						If you don't have a path (or you have no intention of using paths in diagnostics)
	/// 						then this parameter can safely be left blank.
	///
	/// \returns	\conditional_return{With exceptions}
	///				A toml::table.
	/// 			\conditional_return{Without exceptions}
	///				A toml::parse_result.
	ATHENA_TOML_NODISCARD
	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	parse_result ATHENA_TOML_CALLCONV parse(std::istream & doc, std::wstring_view source_path);

	/// \brief	Parses a TOML document from a file.
	///
	/// \availability This overload is only available when #TOML_ENABLE_WINDOWS_COMPAT is enabled.
	///
	/// \detail \cpp
	/// toml::parse_result get_foo_toml()
	/// {
	///		return toml::parse_file(L"foo.toml");
	/// }
	/// \ecpp
	///
	/// \param 	file_path		The TOML document to parse. Must be valid UTF-8.
	///
	/// \returns	\conditional_return{With exceptions}
	///				A toml::table.
	/// 			\conditional_return{Without exceptions}
	///				A toml::parse_result.
	ATHENA_TOML_NODISCARD
	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	parse_result ATHENA_TOML_CALLCONV parse_file(std::wstring_view file_path);

#endif // TOML_ENABLE_WINDOWS_COMPAT

#if ATHENA_TOML_HAS_CHAR8 && ATHENA_TOML_ENABLE_WINDOWS_COMPAT

	/// \brief	Parses a TOML document from a char8_t string view.
	///
	/// \availability This overload is only available when #TOML_ENABLE_WINDOWS_COMPAT is enabled.
	///
	/// \detail \cpp
	/// auto tbl = toml::parse(u8"a = 3"sv, L"foo.toml");
	/// std::cout << tbl["a"] << "\n";
	/// \ecpp
	///
	/// \out
	/// 3
	/// \eout
	///
	/// \param 	doc				The TOML document to parse. Must be valid UTF-8.
	/// \param 	source_path		The path used to initialize each node's `source().path`.
	/// 						If you don't have a path (or you have no intention of using paths in diagnostics)
	/// 						then this parameter can safely be left blank.
	///
	/// \returns	\conditional_return{With exceptions}
	///				A toml::table.
	/// 			\conditional_return{Without exceptions}
	///				A toml::parse_result.
	ATHENA_TOML_NODISCARD
	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	parse_result ATHENA_TOML_CALLCONV parse(std::u8string_view doc, std::wstring_view source_path);

#endif // TOML_HAS_CHAR8 && TOML_ENABLE_WINDOWS_COMPAT

	/// \brief	Parses a TOML document from a stream.
	///
	/// \detail \cpp
	/// std::stringstream ss;
	/// ss << "a = 3"sv;
	///
	/// auto tbl = toml::parse(ss);
	/// std::cout << tbl["a"] << "\n";
	/// \ecpp
	///
	/// \out
	/// 3
	/// \eout
	///
	/// \param 	doc				The TOML document to parse. Must be valid UTF-8.
	/// \param 	source_path		The path used to initialize each node's `source().path`.
	/// 						If you don't have a path (or you have no intention of using paths in diagnostics)
	/// 						then this parameter can safely be left blank.
	///
	/// \returns	\conditional_return{With exceptions}
	///				A toml::table.
	/// 			\conditional_return{Without exceptions}
	///				A toml::parse_result.
	ATHENA_TOML_NODISCARD
	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	parse_result ATHENA_TOML_CALLCONV parse(std::istream & doc, std::string_view source_path = {});

	/// \brief	Parses a TOML document from a stream.
	///
	/// \detail \cpp
	/// std::stringstream ss;
	/// ss << "a = 3"sv;
	///
	/// auto tbl = toml::parse(ss, "foo.toml");
	/// std::cout << tbl["a"] << "\n";
	/// \ecpp
	///
	/// \out
	/// 3
	/// \eout
	///
	/// \param 	doc				The TOML document to parse. Must be valid UTF-8.
	/// \param 	source_path		The path used to initialize each node's `source().path`.
	/// 						If you don't have a path (or you have no intention of using paths in diagnostics)
	/// 						then this parameter can safely be left blank.
	///
	/// \returns	\conditional_return{With exceptions}
	///				A toml::table.
	/// 			\conditional_return{Without exceptions}
	///				A toml::parse_result.
	ATHENA_TOML_NODISCARD
	ATHENA_TOML_EXPORTED_FREE_FUNCTION
	parse_result ATHENA_TOML_CALLCONV parse(std::istream & doc, std::string && source_path);

	ATHENA_TOML_ABI_NAMESPACE_END; // TOML_EXCEPTIONS

	inline namespace literals
	{
		ATHENA_TOML_ABI_NAMESPACE_BOOL(ATHENA_TOML_EXCEPTIONS, lit_ex, lit_noex);

		/// \brief	Parses TOML data from a string literal.
		///
		/// \detail \cpp
		/// using namespace toml::literals;
		///
		/// auto tbl = "a = 3"_toml;
		/// std::cout << tbl["a"] << "\n";
		/// \ecpp
		///
		/// \out
		/// 3
		/// \eout
		///
		/// \param 	str	The string data. Must be valid UTF-8.
		/// \param 	len	The string length.
		///
		/// \returns	\conditional_return{With exceptions}
		///				A toml::table.
		/// 			\conditional_return{Without exceptions}
		///				A toml::parse_result.
		ATHENA_TOML_NODISCARD
		ATHENA_TOML_ALWAYS_INLINE
		parse_result operator"" _toml(const char* str, size_t len)
		{
			return parse(std::string_view{ str, len });
		}

#if ATHENA_TOML_HAS_CHAR8

		/// \brief	Parses TOML data from a UTF-8 string literal.
		///
		/// \detail \cpp
		/// using namespace toml::literals;
		///
		/// auto tbl = u8"a = 3"_toml;
		/// std::cout << tbl["a"] << "\n";
		/// \ecpp
		///
		/// \out
		/// 3
		/// \eout
		///
		/// \param 	str	The string data. Must be valid UTF-8.
		/// \param 	len	The string length.
		///
		/// \returns	\conditional_return{With exceptions}
		///				A toml::table.
		/// 			\conditional_return{Without exceptions}
		///				A toml::parse_result.
		ATHENA_TOML_NODISCARD
		ATHENA_TOML_ALWAYS_INLINE
		parse_result operator"" _toml(const char8_t* str, size_t len)
		{
			return parse(std::u8string_view{ str, len });
		}

#endif // TOML_HAS_CHAR8

		ATHENA_TOML_ABI_NAMESPACE_END; // TOML_EXCEPTIONS
	}
}
ATHENA_TOML_NAMESPACE_END;

#include <athena_toml/impl/header_end.hpp>
#endif // TOML_ENABLE_PARSER
