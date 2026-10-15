//# This file is a part of toml++ and is subject to the the terms of the MIT license.
//# Copyright (c) Mark Gillard <mark.gillard@outlook.com.au>
//# See https://github.com/marzer/tomlplusplus/blob/master/LICENSE for the full license text.
// SPDX-License-Identifier: MIT
#pragma once

//#=====================================================================================================================
//# C++ VERSION
//#=====================================================================================================================

#ifndef __cplusplus
#error athena_toml++ is a C++ library.
#endif

#ifndef ATHENA_TOML_CPP
#ifdef _MSVC_LANG
#if _MSVC_LANG > __cplusplus
#define ATHENA_TOML_CPP _MSVC_LANG
#endif
#endif
#ifndef ATHENA_TOML_CPP
#define ATHENA_TOML_CPP __cplusplus
#endif
#if ATHENA_TOML_CPP >= 202900L
#undef ATHENA_TOML_CPP
#define ATHENA_TOML_CPP 29
#elif ATHENA_TOML_CPP >= 202600L
#undef ATHENA_TOML_CPP
#define ATHENA_TOML_CPP 26
#elif ATHENA_TOML_CPP >= 202302L
#undef ATHENA_TOML_CPP
#define ATHENA_TOML_CPP 23
#elif ATHENA_TOML_CPP >= 202002L
#undef ATHENA_TOML_CPP
#define ATHENA_TOML_CPP 20
#elif ATHENA_TOML_CPP >= 201703L
#undef ATHENA_TOML_CPP
#define ATHENA_TOML_CPP 17
#elif ATHENA_TOML_CPP >= 201402L
#undef ATHENA_TOML_CPP
#define ATHENA_TOML_CPP 14
#elif ATHENA_TOML_CPP >= 201103L
#undef ATHENA_TOML_CPP
#define ATHENA_TOML_CPP 11
#else
#undef ATHENA_TOML_CPP
#define ATHENA_TOML_CPP 0
#endif
#endif

#if !ATHENA_TOML_CPP
#error athena_toml++ requires C++17 or higher. For a pre-C++11 ATHENA_TOML library see https://github.com/ToruNiina/Boost.toml
#elif ATHENA_TOML_CPP < 17
#error athena_toml++ requires C++17 or higher. For a C++11 ATHENA_TOML library see https://github.com/ToruNiina/toml11
#endif

//#=====================================================================================================================
//# COMPILER
//#=====================================================================================================================

#ifndef ATHENA_TOML_MAKE_VERSION
#define ATHENA_TOML_MAKE_VERSION(major, minor, patch) (((major)*10000) + ((minor)*100) + ((patch)))
#endif

#ifndef ATHENA_TOML_INTELLISENSE
#ifdef __INTELLISENSE__
#define ATHENA_TOML_INTELLISENSE 1
#else
#define ATHENA_TOML_INTELLISENSE 0
#endif
#endif

#ifndef ATHENA_TOML_DOXYGEN
#if defined(DOXYGEN) || defined(__DOXYGEN) || defined(__DOXYGEN__) || defined(__doxygen__) || defined(__POXY__)        \
	|| defined(__poxy__)
#define ATHENA_TOML_DOXYGEN 1
#else
#define ATHENA_TOML_DOXYGEN 0
#endif
#endif

#ifndef ATHENA_TOML_CLANG
#ifdef __clang__
#define ATHENA_TOML_CLANG __clang_major__
#else
#define ATHENA_TOML_CLANG 0
#endif

// special handling for apple clang; see:
// - https://github.com/marzer/tomlplusplus/issues/189
// - https://en.wikipedia.org/wiki/Xcode
// -
// https://stackoverflow.com/questions/19387043/how-can-i-reliably-detect-the-version-of-clang-at-preprocessing-time
#if ATHENA_TOML_CLANG && defined(__apple_build_version__)
#undef ATHENA_TOML_CLANG
#define ATHENA_TOML_CLANG_VERSION ATHENA_TOML_MAKE_VERSION(__clang_major__, __clang_minor__, __clang_patchlevel__)
#if ATHENA_TOML_CLANG_VERSION >= ATHENA_TOML_MAKE_VERSION(15, 0, 0)
#define ATHENA_TOML_CLANG 16
#elif ATHENA_TOML_CLANG_VERSION >= ATHENA_TOML_MAKE_VERSION(14, 3, 0)
#define ATHENA_TOML_CLANG 15
#elif ATHENA_TOML_CLANG_VERSION >= ATHENA_TOML_MAKE_VERSION(14, 0, 0)
#define ATHENA_TOML_CLANG 14
#elif ATHENA_TOML_CLANG_VERSION >= ATHENA_TOML_MAKE_VERSION(13, 1, 6)
#define ATHENA_TOML_CLANG 13
#elif ATHENA_TOML_CLANG_VERSION >= ATHENA_TOML_MAKE_VERSION(13, 0, 0)
#define ATHENA_TOML_CLANG 12
#elif ATHENA_TOML_CLANG_VERSION >= ATHENA_TOML_MAKE_VERSION(12, 0, 5)
#define ATHENA_TOML_CLANG 11
#elif ATHENA_TOML_CLANG_VERSION >= ATHENA_TOML_MAKE_VERSION(12, 0, 0)
#define ATHENA_TOML_CLANG 10
#elif ATHENA_TOML_CLANG_VERSION >= ATHENA_TOML_MAKE_VERSION(11, 0, 3)
#define ATHENA_TOML_CLANG 9
#elif ATHENA_TOML_CLANG_VERSION >= ATHENA_TOML_MAKE_VERSION(11, 0, 0)
#define ATHENA_TOML_CLANG 8
#elif ATHENA_TOML_CLANG_VERSION >= ATHENA_TOML_MAKE_VERSION(10, 0, 1)
#define ATHENA_TOML_CLANG 7
#else
#define ATHENA_TOML_CLANG 6 // not strictly correct but doesn't matter below this
#endif
#undef ATHENA_TOML_CLANG_VERSION
#endif
#endif

#ifndef ATHENA_TOML_ICC
#ifdef __INTEL_COMPILER
#define ATHENA_TOML_ICC __INTEL_COMPILER
#ifdef __ICL
#define ATHENA_TOML_ICC_CL ATHENA_TOML_ICC
#else
#define ATHENA_TOML_ICC_CL 0
#endif
#else
#define ATHENA_TOML_ICC	0
#define ATHENA_TOML_ICC_CL 0
#endif
#endif

#ifndef ATHENA_TOML_MSVC_LIKE
#ifdef _MSC_VER
#define ATHENA_TOML_MSVC_LIKE _MSC_VER
#else
#define ATHENA_TOML_MSVC_LIKE 0
#endif
#endif

#ifndef ATHENA_TOML_MSVC
#if ATHENA_TOML_MSVC_LIKE && !ATHENA_TOML_CLANG && !ATHENA_TOML_ICC
#define ATHENA_TOML_MSVC ATHENA_TOML_MSVC_LIKE
#else
#define ATHENA_TOML_MSVC 0
#endif
#endif

#ifndef ATHENA_TOML_GCC_LIKE
#ifdef __GNUC__
#define ATHENA_TOML_GCC_LIKE __GNUC__
#else
#define ATHENA_TOML_GCC_LIKE 0
#endif
#endif

#ifndef ATHENA_TOML_GCC
#if ATHENA_TOML_GCC_LIKE && !ATHENA_TOML_CLANG && !ATHENA_TOML_ICC
#define ATHENA_TOML_GCC ATHENA_TOML_GCC_LIKE
#else
#define ATHENA_TOML_GCC 0
#endif
#endif

#ifndef ATHENA_TOML_CUDA
#if defined(__CUDACC__) || defined(__CUDA_ARCH__) || defined(__CUDA_LIBDEVICE__)
#define ATHENA_TOML_CUDA 1
#else
#define ATHENA_TOML_CUDA 0
#endif
#endif

//#=====================================================================================================================
//# ARCHITECTURE
//#=====================================================================================================================

#ifndef ATHENA_TOML_ARCH_ITANIUM
#if defined(__ia64__) || defined(__ia64) || defined(_IA64) || defined(__IA64__) || defined(_M_IA64)
#define ATHENA_TOML_ARCH_ITANIUM 1
#define ATHENA_TOML_ARCH_BITNESS 64
#else
#define ATHENA_TOML_ARCH_ITANIUM 0
#endif
#endif

#ifndef ATHENA_TOML_ARCH_AMD64
#if defined(__amd64__) || defined(__amd64) || defined(__x86_64__) || defined(__x86_64) || defined(_M_AMD64)
#define ATHENA_TOML_ARCH_AMD64	  1
#define ATHENA_TOML_ARCH_BITNESS 64
#else
#define ATHENA_TOML_ARCH_AMD64 0
#endif
#endif

#ifndef ATHENA_TOML_ARCH_X86
#if defined(__i386__) || defined(_M_IX86)
#define ATHENA_TOML_ARCH_X86	  1
#define ATHENA_TOML_ARCH_BITNESS 32
#else
#define ATHENA_TOML_ARCH_X86 0
#endif
#endif

#ifndef ATHENA_TOML_ARCH_ARM
#if defined(__aarch64__) || defined(__ARM_ARCH_ISA_A64) || defined(_M_ARM64) || defined(__ARM_64BIT_STATE)             \
	|| defined(_M_ARM64EC)
#define ATHENA_TOML_ARCH_ARM32	  0
#define ATHENA_TOML_ARCH_ARM64	  1
#define ATHENA_TOML_ARCH_ARM	  1
#define ATHENA_TOML_ARCH_BITNESS 64
#elif defined(__arm__) || defined(_M_ARM) || defined(__ARM_32BIT_STATE)
#define ATHENA_TOML_ARCH_ARM32	  1
#define ATHENA_TOML_ARCH_ARM64	  0
#define ATHENA_TOML_ARCH_ARM	  1
#define ATHENA_TOML_ARCH_BITNESS 32
#else
#define ATHENA_TOML_ARCH_ARM32 0
#define ATHENA_TOML_ARCH_ARM64 0
#define ATHENA_TOML_ARCH_ARM	0
#endif
#endif

#ifndef ATHENA_TOML_ARCH_BITNESS
#define ATHENA_TOML_ARCH_BITNESS 0
#endif

#ifndef ATHENA_TOML_ARCH_X64
#if ATHENA_TOML_ARCH_BITNESS == 64
#define ATHENA_TOML_ARCH_X64 1
#else
#define ATHENA_TOML_ARCH_X64 0
#endif
#endif

//#=====================================================================================================================
//# OS
//#=====================================================================================================================

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__) || defined(__CYGWIN__)
#define ATHENA_TOML_WINDOWS 1
#else
#define ATHENA_TOML_WINDOWS 0
#endif

#ifdef __unix__
#define ATHENA_TOML_UNIX 1
#else
#define ATHENA_TOML_UNIX 0
#endif

#ifdef __linux__
#define ATHENA_TOML_LINUX 1
#else
#define ATHENA_TOML_LINUX 0
#endif

//#=====================================================================================================================
//# ATTRIBUTES / FEATURE DETECTION / UTILITY MACROS
//#=====================================================================================================================

// TOML_HAS_INCLUDE
#ifndef ATHENA_TOML_HAS_INCLUDE
#ifdef __has_include
#define ATHENA_TOML_HAS_INCLUDE(header)		__has_include(header)
#else
#define ATHENA_TOML_HAS_INCLUDE(header) 0
#endif
#endif

// TOML_HAS_BUILTIN
#ifndef ATHENA_TOML_HAS_BUILTIN
#ifdef __has_builtin
#define ATHENA_TOML_HAS_BUILTIN(name) __has_builtin(name)
#else
#define ATHENA_TOML_HAS_BUILTIN(name) 0
#endif
#endif

// TOML_HAS_FEATURE
#ifndef ATHENA_TOML_HAS_FEATURE
#ifdef __has_feature
#define ATHENA_TOML_HAS_FEATURE(name) __has_feature(name)
#else
#define ATHENA_TOML_HAS_FEATURE(name) 0
#endif
#endif

// TOML_HAS_ATTR
#ifndef ATHENA_TOML_HAS_ATTR
#ifdef __has_attribute
#define ATHENA_TOML_HAS_ATTR(attr) __has_attribute(attr)
#else
#define ATHENA_TOML_HAS_ATTR(attr) 0
#endif
#endif

// TOML_HAS_CPP_ATTR
#ifndef ATHENA_TOML_HAS_CPP_ATTR
#ifdef __has_cpp_attribute
#define ATHENA_TOML_HAS_CPP_ATTR(attr) __has_cpp_attribute(attr)
#else
#define ATHENA_TOML_HAS_CPP_ATTR(attr) 0
#endif
#endif

// TOML_ATTR (gnu attributes)
#ifndef ATHENA_TOML_ATTR
#if ATHENA_TOML_CLANG || ATHENA_TOML_GCC_LIKE
#define ATHENA_TOML_ATTR(...) __attribute__((__VA_ARGS__))
#else
#define ATHENA_TOML_ATTR(...)
#endif
#endif

// TOML_DECLSPEC (msvc attributes)
#ifndef ATHENA_TOML_DECLSPEC
#if ATHENA_TOML_MSVC_LIKE
#define ATHENA_TOML_DECLSPEC(...) __declspec(__VA_ARGS__)
#else
#define ATHENA_TOML_DECLSPEC(...)
#endif
#endif

// TOML_COMPILER_HAS_EXCEPTIONS
#ifndef ATHENA_TOML_COMPILER_HAS_EXCEPTIONS
#if defined(__EXCEPTIONS) || defined(_CPPUNWIND) || defined(__cpp_exceptions)
#define ATHENA_TOML_COMPILER_HAS_EXCEPTIONS 1
#else
#define ATHENA_TOML_COMPILER_HAS_EXCEPTIONS 0
#endif
#endif

// TOML_COMPILER_HAS_RTTI
#ifndef ATHENA_TOML_COMPILER_HAS_RTTI
#if defined(_CPPRTTI) || defined(__GXX_RTTI) || ATHENA_TOML_HAS_FEATURE(cxx_rtti)
#define ATHENA_TOML_COMPILER_HAS_RTTI 1
#else
#define ATHENA_TOML_COMPILER_HAS_RTTI 0
#endif
#endif

// TOML_CONCAT
#define ATHENA_TOML_CONCAT_1(x, y) x##y
#define ATHENA_TOML_CONCAT(x, y)	ATHENA_TOML_CONCAT_1(x, y)

// TOML_MAKE_STRING
#define ATHENA_TOML_MAKE_STRING_1(s) #s
#define ATHENA_TOML_MAKE_STRING(s)	  ATHENA_TOML_MAKE_STRING_1(s)

// TOML_PRAGMA_XXXX (compiler-specific pragmas)
#if ATHENA_TOML_CLANG
#define ATHENA_TOML_PRAGMA_CLANG(decl) _Pragma(ATHENA_TOML_MAKE_STRING(clang decl))
#else
#define ATHENA_TOML_PRAGMA_CLANG(decl)
#endif
#if ATHENA_TOML_CLANG >= 8
#define ATHENA_TOML_PRAGMA_CLANG_GE_8(decl) ATHENA_TOML_PRAGMA_CLANG(decl)
#else
#define ATHENA_TOML_PRAGMA_CLANG_GE_8(decl)
#endif
#if ATHENA_TOML_CLANG >= 9
#define ATHENA_TOML_PRAGMA_CLANG_GE_9(decl) ATHENA_TOML_PRAGMA_CLANG(decl)
#else
#define ATHENA_TOML_PRAGMA_CLANG_GE_9(decl)
#endif
#if ATHENA_TOML_CLANG >= 10
#define ATHENA_TOML_PRAGMA_CLANG_GE_10(decl) ATHENA_TOML_PRAGMA_CLANG(decl)
#else
#define ATHENA_TOML_PRAGMA_CLANG_GE_10(decl)
#endif
#if ATHENA_TOML_CLANG >= 11
#define ATHENA_TOML_PRAGMA_CLANG_GE_11(decl) ATHENA_TOML_PRAGMA_CLANG(decl)
#else
#define ATHENA_TOML_PRAGMA_CLANG_GE_11(decl)
#endif
#if ATHENA_TOML_GCC
#define ATHENA_TOML_PRAGMA_GCC(decl) _Pragma(ATHENA_TOML_MAKE_STRING(GCC decl))
#else
#define ATHENA_TOML_PRAGMA_GCC(decl)
#endif
#if ATHENA_TOML_MSVC
#define ATHENA_TOML_PRAGMA_MSVC(...) __pragma(__VA_ARGS__)
#else
#define ATHENA_TOML_PRAGMA_MSVC(...)
#endif
#if ATHENA_TOML_ICC
#define ATHENA_TOML_PRAGMA_ICC(...) __pragma(__VA_ARGS__)
#else
#define ATHENA_TOML_PRAGMA_ICC(...)
#endif

// TOML_ALWAYS_INLINE
#ifdef _MSC_VER
#define ATHENA_TOML_ALWAYS_INLINE __forceinline
#elif ATHENA_TOML_GCC || ATHENA_TOML_CLANG || ATHENA_TOML_HAS_ATTR(__always_inline__)
#define ATHENA_TOML_ALWAYS_INLINE                                                                                             \
	ATHENA_TOML_ATTR(__always_inline__)                                                                                       \
	inline
#else
#define ATHENA_TOML_ALWAYS_INLINE inline
#endif

// TOML_NEVER_INLINE
#ifdef _MSC_VER
#define ATHENA_TOML_NEVER_INLINE ATHENA_TOML_DECLSPEC(noinline)
#elif ATHENA_TOML_CUDA // https://gitlab.gnome.org/GNOME/glib/-/issues/2555
#define ATHENA_TOML_NEVER_INLINE ATHENA_TOML_ATTR(noinline)
#else
#if ATHENA_TOML_GCC || ATHENA_TOML_CLANG || ATHENA_TOML_HAS_ATTR(__noinline__)
#define ATHENA_TOML_NEVER_INLINE ATHENA_TOML_ATTR(__noinline__)
#endif
#endif
#ifndef ATHENA_TOML_NEVER_INLINE
#define ATHENA_TOML_NEVER_INLINE
#endif

// MSVC attributes
#define ATHENA_TOML_ABSTRACT_INTERFACE ATHENA_TOML_DECLSPEC(novtable)
#define ATHENA_TOML_EMPTY_BASES		ATHENA_TOML_DECLSPEC(empty_bases)

// TOML_TRIVIAL_ABI
#if ATHENA_TOML_CLANG || ATHENA_TOML_HAS_ATTR(__trivial_abi__)
#define ATHENA_TOML_TRIVIAL_ABI ATHENA_TOML_ATTR(__trivial_abi__)
#else
#define ATHENA_TOML_TRIVIAL_ABI
#endif

// TOML_NODISCARD
#if ATHENA_TOML_CPP >= 17 && ATHENA_TOML_HAS_CPP_ATTR(nodiscard) >= 201603
#define ATHENA_TOML_NODISCARD [[nodiscard]]
#elif ATHENA_TOML_CLANG || ATHENA_TOML_GCC || ATHENA_TOML_HAS_ATTR(__warn_unused_result__)
#define ATHENA_TOML_NODISCARD ATHENA_TOML_ATTR(__warn_unused_result__)
#else
#define ATHENA_TOML_NODISCARD
#endif

// TOML_NODISCARD_CTOR
#if ATHENA_TOML_CPP >= 17 && ATHENA_TOML_HAS_CPP_ATTR(nodiscard) >= 201907
#define ATHENA_TOML_NODISCARD_CTOR [[nodiscard]]
#else
#define ATHENA_TOML_NODISCARD_CTOR
#endif

// pure + const
#ifndef ATHENA_TOML_PURE
#ifdef NDEBUG
#define ATHENA_TOML_PURE                                                                                                      \
	ATHENA_TOML_DECLSPEC(noalias)                                                                                             \
	ATHENA_TOML_ATTR(pure)
#else
#define ATHENA_TOML_PURE
#endif
#endif
#ifndef ATHENA_TOML_CONST
#ifdef NDEBUG
#define ATHENA_TOML_CONST                                                                                                     \
	ATHENA_TOML_DECLSPEC(noalias)                                                                                             \
	ATHENA_TOML_ATTR(const)
#else
#define ATHENA_TOML_CONST
#endif
#endif
#ifndef ATHENA_TOML_INLINE_GETTER
#define ATHENA_TOML_INLINE_GETTER                                                                                             \
	ATHENA_TOML_NODISCARD                                                                                                     \
	ATHENA_TOML_ALWAYS_INLINE
#endif
#ifndef ATHENA_TOML_PURE_GETTER
#define ATHENA_TOML_PURE_GETTER                                                                                               \
	ATHENA_TOML_NODISCARD                                                                                                     \
	ATHENA_TOML_PURE
#endif
#ifndef ATHENA_TOML_PURE_INLINE_GETTER
#define ATHENA_TOML_PURE_INLINE_GETTER                                                                                        \
	ATHENA_TOML_NODISCARD                                                                                                     \
	ATHENA_TOML_ALWAYS_INLINE                                                                                                 \
	ATHENA_TOML_PURE
#endif
#ifndef ATHENA_TOML_CONST_GETTER
#define ATHENA_TOML_CONST_GETTER                                                                                              \
	ATHENA_TOML_NODISCARD                                                                                                     \
	ATHENA_TOML_CONST
#endif
#ifndef ATHENA_TOML_CONST_INLINE_GETTER
#define ATHENA_TOML_CONST_INLINE_GETTER                                                                                       \
	ATHENA_TOML_NODISCARD                                                                                                     \
	ATHENA_TOML_ALWAYS_INLINE                                                                                                 \
	ATHENA_TOML_CONST
#endif

// TOML_ASSUME
#ifdef _MSC_VER
#define ATHENA_TOML_ASSUME(expr) __assume(expr)
#elif ATHENA_TOML_ICC || ATHENA_TOML_CLANG || ATHENA_TOML_HAS_BUILTIN(__builtin_assume)
#define ATHENA_TOML_ASSUME(expr) __builtin_assume(expr)
#elif ATHENA_TOML_HAS_CPP_ATTR(assume) >= 202207
#define ATHENA_TOML_ASSUME(expr) [[assume(expr)]]
#elif ATHENA_TOML_HAS_ATTR(__assume__)
#define ATHENA_TOML_ASSUME(expr) __attribute__((__assume__(expr)))
#else
#define ATHENA_TOML_ASSUME(expr) static_cast<void>(0)
#endif

// TOML_UNREACHABLE
#ifdef _MSC_VER
#define ATHENA_TOML_UNREACHABLE __assume(0)
#elif ATHENA_TOML_ICC || ATHENA_TOML_CLANG || ATHENA_TOML_GCC || ATHENA_TOML_HAS_BUILTIN(__builtin_unreachable)
#define ATHENA_TOML_UNREACHABLE __builtin_unreachable()
#else
#define ATHENA_TOML_UNREACHABLE static_cast<void>(0)
#endif

// TOML_LIKELY
#if ATHENA_TOML_CPP >= 20 && ATHENA_TOML_HAS_CPP_ATTR(likely) >= 201803
#define ATHENA_TOML_LIKELY(...) (__VA_ARGS__) [[likely]]
#define ATHENA_TOML_LIKELY_CASE [[likely]]
#elif ATHENA_TOML_GCC || ATHENA_TOML_CLANG || ATHENA_TOML_HAS_BUILTIN(__builtin_expect)
#define ATHENA_TOML_LIKELY(...) (__builtin_expect(!!(__VA_ARGS__), 1))
#else
#define ATHENA_TOML_LIKELY(...) (__VA_ARGS__)
#endif
#ifndef ATHENA_TOML_LIKELY_CASE
#define ATHENA_TOML_LIKELY_CASE
#endif

// TOML_UNLIKELY
#if ATHENA_TOML_CPP >= 20 && ATHENA_TOML_HAS_CPP_ATTR(unlikely) >= 201803
#define ATHENA_TOML_UNLIKELY(...) (__VA_ARGS__) [[unlikely]]
#define ATHENA_TOML_UNLIKELY_CASE [[unlikely]]
#elif ATHENA_TOML_GCC || ATHENA_TOML_CLANG || ATHENA_TOML_HAS_BUILTIN(__builtin_expect)
#define ATHENA_TOML_UNLIKELY(...) (__builtin_expect(!!(__VA_ARGS__), 0))
#else
#define ATHENA_TOML_UNLIKELY(...) (__VA_ARGS__)
#endif
#ifndef ATHENA_TOML_UNLIKELY_CASE
#define ATHENA_TOML_UNLIKELY_CASE
#endif

// TOML_FLAGS_ENUM
#if ATHENA_TOML_CLANG || ATHENA_TOML_HAS_ATTR(flag_enum)
#define ATHENA_TOML_FLAGS_ENUM __attribute__((flag_enum))
#else
#define ATHENA_TOML_FLAGS_ENUM
#endif

// TOML_OPEN_ENUM + TOML_CLOSED_ENUM
#if ATHENA_TOML_CLANG || ATHENA_TOML_HAS_ATTR(enum_extensibility)
#define ATHENA_TOML_OPEN_ENUM	 __attribute__((enum_extensibility(open)))
#define ATHENA_TOML_CLOSED_ENUM __attribute__((enum_extensibility(closed)))
#else
#define ATHENA_TOML_OPEN_ENUM
#define ATHENA_TOML_CLOSED_ENUM
#endif

// TOML_OPEN_FLAGS_ENUM + TOML_CLOSED_FLAGS_ENUM
#define ATHENA_TOML_OPEN_FLAGS_ENUM   ATHENA_TOML_OPEN_ENUM ATHENA_TOML_FLAGS_ENUM
#define ATHENA_TOML_CLOSED_FLAGS_ENUM ATHENA_TOML_CLOSED_ENUM ATHENA_TOML_FLAGS_ENUM

// TOML_MAKE_FLAGS
#define ATHENA_TOML_MAKE_FLAGS_2(T, op, linkage)                                                                              \
	ATHENA_TOML_CONST_INLINE_GETTER                                                                                           \
	linkage constexpr T operator op(T lhs, T rhs) noexcept                                                             \
	{                                                                                                                  \
		using under = std::underlying_type_t<T>;                                                                       \
		return static_cast<T>(static_cast<under>(lhs) op static_cast<under>(rhs));                                     \
	}                                                                                                                  \
                                                                                                                       \
	linkage constexpr T& operator ATHENA_TOML_CONCAT(op, =)(T & lhs, T rhs) noexcept                                          \
	{                                                                                                                  \
		return lhs = (lhs op rhs);                                                                                     \
	}                                                                                                                  \
                                                                                                                       \
	static_assert(true)
#define ATHENA_TOML_MAKE_FLAGS_1(T, linkage)                                                                                  \
	static_assert(std::is_enum_v<T>);                                                                                  \
                                                                                                                       \
	ATHENA_TOML_MAKE_FLAGS_2(T, &, linkage);                                                                                  \
	ATHENA_TOML_MAKE_FLAGS_2(T, |, linkage);                                                                                  \
	ATHENA_TOML_MAKE_FLAGS_2(T, ^, linkage);                                                                                  \
                                                                                                                       \
	ATHENA_TOML_CONST_INLINE_GETTER                                                                                           \
	linkage constexpr T operator~(T val) noexcept                                                                      \
	{                                                                                                                  \
		using under = std::underlying_type_t<T>;                                                                       \
		return static_cast<T>(~static_cast<under>(val));                                                               \
	}                                                                                                                  \
                                                                                                                       \
	ATHENA_TOML_CONST_INLINE_GETTER                                                                                           \
	linkage constexpr bool operator!(T val) noexcept                                                                   \
	{                                                                                                                  \
		using under = std::underlying_type_t<T>;                                                                       \
		return !static_cast<under>(val);                                                                               \
	}                                                                                                                  \
                                                                                                                       \
	static_assert(true)
#define ATHENA_TOML_MAKE_FLAGS(T) ATHENA_TOML_MAKE_FLAGS_1(T, )

#define ATHENA_TOML_UNUSED(...) static_cast<void>(__VA_ARGS__)

#define ATHENA_TOML_DELETE_DEFAULTS(T)                                                                                        \
	T(const T&)			   = delete;                                                                                   \
	T(T&&)				   = delete;                                                                                   \
	T& operator=(const T&) = delete;                                                                                   \
	T& operator=(T&&)	   = delete

#define ATHENA_TOML_ASYMMETRICAL_EQUALITY_OPS(LHS, RHS, ...)                                                                  \
	__VA_ARGS__ ATHENA_TOML_NODISCARD                                                                                         \
	friend bool operator==(RHS rhs, LHS lhs) noexcept                                                                  \
	{                                                                                                                  \
		return lhs == rhs;                                                                                             \
	}                                                                                                                  \
	__VA_ARGS__ ATHENA_TOML_NODISCARD                                                                                         \
	friend bool operator!=(LHS lhs, RHS rhs) noexcept                                                                  \
	{                                                                                                                  \
		return !(lhs == rhs);                                                                                          \
	}                                                                                                                  \
	__VA_ARGS__ ATHENA_TOML_NODISCARD                                                                                         \
	friend bool operator!=(RHS rhs, LHS lhs) noexcept                                                                  \
	{                                                                                                                  \
		return !(lhs == rhs);                                                                                          \
	}                                                                                                                  \
	static_assert(true)

#define ATHENA_TOML_EVAL_BOOL_1(T, F) T
#define ATHENA_TOML_EVAL_BOOL_0(T, F) F

#if !defined(__POXY__) && !defined(POXY_IMPLEMENTATION_DETAIL)
#define POXY_IMPLEMENTATION_DETAIL(...) __VA_ARGS__
#endif

//======================================================================================================================
// COMPILER-SPECIFIC WARNING MANAGEMENT
//======================================================================================================================

#if ATHENA_TOML_CLANG

#define ATHENA_TOML_PUSH_WARNINGS                                                                                             \
	ATHENA_TOML_PRAGMA_CLANG(diagnostic push)                                                                                 \
	ATHENA_TOML_PRAGMA_CLANG(diagnostic ignored "-Wunknown-warning-option")                                                   \
	static_assert(true)

#define ATHENA_TOML_DISABLE_SWITCH_WARNINGS                                                                                   \
	ATHENA_TOML_PRAGMA_CLANG(diagnostic ignored "-Wswitch")                                                                   \
	static_assert(true)

#define ATHENA_TOML_DISABLE_ARITHMETIC_WARNINGS                                                                               \
	ATHENA_TOML_PRAGMA_CLANG_GE_10(diagnostic ignored "-Wimplicit-int-float-conversion")                                      \
	ATHENA_TOML_PRAGMA_CLANG(diagnostic ignored "-Wfloat-equal")                                                              \
	ATHENA_TOML_PRAGMA_CLANG(diagnostic ignored "-Wdouble-promotion")                                                         \
	ATHENA_TOML_PRAGMA_CLANG(diagnostic ignored "-Wchar-subscripts")                                                          \
	ATHENA_TOML_PRAGMA_CLANG(diagnostic ignored "-Wshift-sign-overflow")                                                      \
	static_assert(true)

#define ATHENA_TOML_DISABLE_SPAM_WARNINGS                                                                                     \
	ATHENA_TOML_PRAGMA_CLANG_GE_8(diagnostic ignored "-Wdefaulted-function-deleted")                                          \
	ATHENA_TOML_PRAGMA_CLANG_GE_9(diagnostic ignored "-Wctad-maybe-unsupported")                                              \
	ATHENA_TOML_PRAGMA_CLANG_GE_10(diagnostic ignored "-Wzero-as-null-pointer-constant")                                      \
	ATHENA_TOML_PRAGMA_CLANG_GE_11(diagnostic ignored "-Wsuggest-destructor-override")                                        \
	ATHENA_TOML_PRAGMA_CLANG(diagnostic ignored "-Wweak-vtables")                                                             \
	ATHENA_TOML_PRAGMA_CLANG(diagnostic ignored "-Wweak-template-vtables")                                                    \
	ATHENA_TOML_PRAGMA_CLANG(diagnostic ignored "-Wdouble-promotion")                                                         \
	ATHENA_TOML_PRAGMA_CLANG(diagnostic ignored "-Wchar-subscripts")                                                          \
	ATHENA_TOML_PRAGMA_CLANG(diagnostic ignored "-Wmissing-field-initializers")                                               \
	ATHENA_TOML_PRAGMA_CLANG(diagnostic ignored "-Wpadded")                                                                   \
	static_assert(true)

#define ATHENA_TOML_POP_WARNINGS                                                                                              \
	ATHENA_TOML_PRAGMA_CLANG(diagnostic pop)                                                                                  \
	static_assert(true)

#define ATHENA_TOML_DISABLE_WARNINGS                                                                                          \
	ATHENA_TOML_PRAGMA_CLANG(diagnostic push)                                                                                 \
	ATHENA_TOML_PRAGMA_CLANG(diagnostic ignored "-Weverything")                                                               \
	static_assert(true, "")

#define ATHENA_TOML_ENABLE_WARNINGS                                                                                           \
	ATHENA_TOML_PRAGMA_CLANG(diagnostic pop)                                                                                  \
	static_assert(true)

#define ATHENA_TOML_SIMPLE_STATIC_ASSERT_MESSAGES 1

#elif ATHENA_TOML_MSVC

#define ATHENA_TOML_PUSH_WARNINGS                                                                                             \
	__pragma(warning(push))                                                                                            \
	static_assert(true)

#if ATHENA_TOML_HAS_INCLUDE(<CodeAnalysis/Warnings.h>)
#pragma warning(push, 0)
#include <CodeAnalysis/Warnings.h>
#pragma warning(pop)
#define ATHENA_TOML_DISABLE_CODE_ANALYSIS_WARNINGS                                                                            \
	__pragma(warning(disable : ALL_CODE_ANALYSIS_WARNINGS))                                                            \
	static_assert(true)
#else
#define ATHENA_TOML_DISABLE_CODE_ANALYSIS_WARNINGS static_assert(true)
#endif

#define ATHENA_TOML_DISABLE_SWITCH_WARNINGS                                                                                   \
	__pragma(warning(disable : 4061))                                                                                  \
	__pragma(warning(disable : 4062))                                                                                  \
	__pragma(warning(disable : 4063))                                                                                  \
	__pragma(warning(disable : 5262))  /* switch-case implicit fallthrough (false-positive) */                         \
	__pragma(warning(disable : 26819)) /* cg: unannotated fallthrough */                                               \
	static_assert(true)

#define ATHENA_TOML_DISABLE_SPAM_WARNINGS                                                                                     \
	__pragma(warning(disable : 4127)) /* conditional expr is constant */                                               \
	__pragma(warning(disable : 4324)) /* structure was padded due to alignment specifier */                            \
	__pragma(warning(disable : 4348))                                                                                  \
	__pragma(warning(disable : 4464)) /* relative include path contains '..' */                                        \
	__pragma(warning(disable : 4505)) /* unreferenced local function removed */                                        \
	__pragma(warning(disable : 4514)) /* unreferenced inline function has been removed */                              \
	__pragma(warning(disable : 4582)) /* constructor is not implicitly called */                                       \
	__pragma(warning(disable : 4619)) /*  there is no warning number 'XXXX' */                                         \
	__pragma(warning(disable : 4623)) /* default constructor was implicitly defined as deleted */                      \
	__pragma(warning(disable : 4625)) /* copy constructor was implicitly defined as deleted */                         \
	__pragma(warning(disable : 4626)) /* assignment operator was implicitly defined as deleted */                      \
	__pragma(warning(disable : 4710)) /* function not inlined */                                                       \
	__pragma(warning(disable : 4711)) /* function selected for automatic expansion */                                  \
	__pragma(warning(disable : 4820)) /* N bytes padding added */                                                      \
	__pragma(warning(disable : 4946)) /* reinterpret_cast used between related classes */                              \
	__pragma(warning(disable : 5026)) /* move constructor was implicitly defined as deleted	*/                         \
	__pragma(warning(disable : 5027)) /* move assignment operator was implicitly defined as deleted	*/                 \
	__pragma(warning(disable : 5039)) /* potentially throwing function passed to 'extern "C"' function */              \
	__pragma(warning(disable : 5045)) /* Compiler will insert Spectre mitigation */                                    \
	__pragma(warning(disable : 5264)) /* const variable is not used (false-positive) */                                \
	__pragma(warning(disable : 26451))                                                                                 \
	__pragma(warning(disable : 26490))                                                                                 \
	__pragma(warning(disable : 26495))                                                                                 \
	__pragma(warning(disable : 26812))                                                                                 \
	__pragma(warning(disable : 26819))                                                                                 \
	static_assert(true)

#define ATHENA_TOML_DISABLE_ARITHMETIC_WARNINGS                                                                               \
	__pragma(warning(disable : 4365)) /* argument signed/unsigned mismatch */                                          \
	__pragma(warning(disable : 4738)) /* storing 32-bit float result in memory */                                      \
	__pragma(warning(disable : 5219)) /* implicit conversion from integral to float */                                 \
	static_assert(true)

#define ATHENA_TOML_POP_WARNINGS                                                                                              \
	__pragma(warning(pop))                                                                                             \
	static_assert(true)

#define ATHENA_TOML_DISABLE_WARNINGS                                                                                          \
	__pragma(warning(push, 0))                                                                                         \
	__pragma(warning(disable : 4348))                                                                                  \
	__pragma(warning(disable : 4668))                                                                                  \
	__pragma(warning(disable : 5105))                                                                                  \
	__pragma(warning(disable : 5264))                                                                                  \
	ATHENA_TOML_DISABLE_CODE_ANALYSIS_WARNINGS;                                                                               \
	ATHENA_TOML_DISABLE_SWITCH_WARNINGS;                                                                                      \
	ATHENA_TOML_DISABLE_SPAM_WARNINGS;                                                                                        \
	ATHENA_TOML_DISABLE_ARITHMETIC_WARNINGS;                                                                                  \
	static_assert(true)

#define ATHENA_TOML_ENABLE_WARNINGS ATHENA_TOML_POP_WARNINGS

#elif ATHENA_TOML_ICC

#define ATHENA_TOML_PUSH_WARNINGS                                                                                             \
	__pragma(warning(push))                                                                                            \
	static_assert(true)

#define ATHENA_TOML_DISABLE_SPAM_WARNINGS                                                                                     \
	__pragma(warning(disable : 82))	  /* storage class is not first */                                                 \
	__pragma(warning(disable : 111))  /* statement unreachable (false-positive) */                                     \
	__pragma(warning(disable : 869))  /* unreferenced parameter */                                                     \
	__pragma(warning(disable : 1011)) /* missing return (false-positive) */                                            \
	__pragma(warning(disable : 2261)) /* assume expr side-effects discarded */                                         \
	static_assert(true)

#define ATHENA_TOML_POP_WARNINGS                                                                                              \
	__pragma(warning(pop))                                                                                             \
	static_assert(true)

#define ATHENA_TOML_DISABLE_WARNINGS                                                                                          \
	__pragma(warning(push, 0))                                                                                         \
	ATHENA_TOML_DISABLE_SPAM_WARNINGS

#define ATHENA_TOML_ENABLE_WARNINGS                                                                                           \
	__pragma(warning(pop))                                                                                             \
	static_assert(true)

#elif ATHENA_TOML_GCC

#define ATHENA_TOML_PUSH_WARNINGS                                                                                             \
	ATHENA_TOML_PRAGMA_GCC(diagnostic push)                                                                                   \
	static_assert(true)

#define ATHENA_TOML_DISABLE_SWITCH_WARNINGS                                                                                   \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wswitch")                                                                     \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wswitch-enum")                                                                \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wswitch-default")                                                             \
	static_assert(true)

#define ATHENA_TOML_DISABLE_ARITHMETIC_WARNINGS                                                                               \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wfloat-equal")                                                                \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wsign-conversion")                                                            \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wchar-subscripts")                                                            \
	static_assert(true)

#define ATHENA_TOML_DISABLE_SUGGEST_ATTR_WARNINGS                                                                             \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wsuggest-attribute=const")                                                    \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wsuggest-attribute=pure")                                                     \
	static_assert(true)

#define ATHENA_TOML_DISABLE_SPAM_WARNINGS                                                                                     \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wpadded")                                                                     \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wcast-align")                                                                 \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wcomment")                                                                    \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wtype-limits")                                                                \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wuseless-cast")                                                               \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wchar-subscripts")                                                            \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wsubobject-linkage")                                                          \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wmissing-field-initializers")                                                 \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wmaybe-uninitialized")                                                        \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wnoexcept")                                                                   \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wnull-dereference")                                                           \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wduplicated-branches")                                                        \
	static_assert(true)

#define ATHENA_TOML_POP_WARNINGS                                                                                              \
	ATHENA_TOML_PRAGMA_GCC(diagnostic pop)                                                                                    \
	static_assert(true)

#define ATHENA_TOML_DISABLE_WARNINGS                                                                                          \
	ATHENA_TOML_PRAGMA_GCC(diagnostic push)                                                                                   \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wall")                                                                        \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wextra")                                                                      \
	ATHENA_TOML_PRAGMA_GCC(diagnostic ignored "-Wpedantic")                                                                   \
	ATHENA_TOML_DISABLE_SWITCH_WARNINGS;                                                                                      \
	ATHENA_TOML_DISABLE_ARITHMETIC_WARNINGS;                                                                                  \
	ATHENA_TOML_DISABLE_SUGGEST_ATTR_WARNINGS;                                                                                \
	ATHENA_TOML_DISABLE_SPAM_WARNINGS;                                                                                        \
	static_assert(true)

#define ATHENA_TOML_ENABLE_WARNINGS                                                                                           \
	ATHENA_TOML_PRAGMA_GCC(diagnostic pop)                                                                                    \
	static_assert(true)

#endif

#ifndef ATHENA_TOML_PUSH_WARNINGS
#define ATHENA_TOML_PUSH_WARNINGS static_assert(true)
#endif
#ifndef ATHENA_TOML_DISABLE_CODE_ANALYSIS_WARNINGS
#define ATHENA_TOML_DISABLE_CODE_ANALYSIS_WARNINGS static_assert(true)
#endif
#ifndef ATHENA_TOML_DISABLE_SWITCH_WARNINGS
#define ATHENA_TOML_DISABLE_SWITCH_WARNINGS static_assert(true)
#endif
#ifndef ATHENA_TOML_DISABLE_SUGGEST_ATTR_WARNINGS
#define ATHENA_TOML_DISABLE_SUGGEST_ATTR_WARNINGS static_assert(true)
#endif
#ifndef ATHENA_TOML_DISABLE_SPAM_WARNINGS
#define ATHENA_TOML_DISABLE_SPAM_WARNINGS static_assert(true)
#endif
#ifndef ATHENA_TOML_DISABLE_ARITHMETIC_WARNINGS
#define ATHENA_TOML_DISABLE_ARITHMETIC_WARNINGS static_assert(true)
#endif
#ifndef ATHENA_TOML_POP_WARNINGS
#define ATHENA_TOML_POP_WARNINGS static_assert(true)
#endif
#ifndef ATHENA_TOML_DISABLE_WARNINGS
#define ATHENA_TOML_DISABLE_WARNINGS static_assert(true)
#endif
#ifndef ATHENA_TOML_ENABLE_WARNINGS
#define ATHENA_TOML_ENABLE_WARNINGS static_assert(true)
#endif
#ifndef ATHENA_TOML_SIMPLE_STATIC_ASSERT_MESSAGES
#define ATHENA_TOML_SIMPLE_STATIC_ASSERT_MESSAGES 0
#endif

//#====================================================================================================================
//# USER CONFIGURATION
//#====================================================================================================================
/// \addtogroup		configuration		Library Configuration
/// \brief Preprocessor macros for configuring library functionality.
/// \detail Define these before including toml++ to alter the way it functions.
/// \remarks Some of these options have ABI implications; inline namespaces are used to prevent
/// 		 you from trying to link incompatible combinations together.
/// @{

#ifdef ATHENA_TOML_CONFIG_HEADER
#include ATHENA_TOML_CONFIG_HEADER
#endif
//# {{
#if ATHENA_TOML_DOXYGEN
#define ATHENA_TOML_CONFIG_HEADER
#endif
/// \def TOML_CONFIG_HEADER
/// \brief An additional header to include before any other toml++ header files.
/// \detail Not defined by default.
//# }}

// is the library being built as a shared lib/dll using meson and friends?
#ifndef ATHENA_TOML_SHARED_LIB
#define ATHENA_TOML_SHARED_LIB 0
#endif

// header-only mode
#if !defined(ATHENA_TOML_HEADER_ONLY) && defined(ATHENA_TOML_ALL_INLINE) // was TOML_ALL_INLINE pre-2.0
#define ATHENA_TOML_HEADER_ONLY ATHENA_TOML_ALL_INLINE
#endif
#if !defined(ATHENA_TOML_HEADER_ONLY) || (defined(ATHENA_TOML_HEADER_ONLY) && ATHENA_TOML_HEADER_ONLY) || ATHENA_TOML_INTELLISENSE
#undef ATHENA_TOML_HEADER_ONLY
#define ATHENA_TOML_HEADER_ONLY 1
#endif
#if ATHENA_TOML_DOXYGEN || ATHENA_TOML_SHARED_LIB
#undef ATHENA_TOML_HEADER_ONLY
#define ATHENA_TOML_HEADER_ONLY 0
#endif
/// \def TOML_HEADER_ONLY
/// \brief Sets whether the library is entirely inline.
/// \detail Defaults to `1`.
/// \remark Disabling this means that you must define #TOML_IMPLEMENTATION in
///	<strong><em>exactly one</em></strong> translation unit in your project:
/// \cpp
/// // global_header_that_includes_toml++.h
/// #define TOML_HEADER_ONLY 0
/// #include <toml.hpp>
///
/// // some_code_file.cpp
/// #define TOML_IMPLEMENTATION
/// #include "global_header_that_includes_toml++.h"
/// \ecpp

// internal implementation switch
#if defined(ATHENA_TOML_IMPLEMENTATION) || ATHENA_TOML_HEADER_ONLY
#undef ATHENA_TOML_IMPLEMENTATION
#define ATHENA_TOML_IMPLEMENTATION 1
#else
#define ATHENA_TOML_IMPLEMENTATION 0
#endif
/// \def TOML_IMPLEMENTATION
/// \brief Enables the library's implementation when #TOML_HEADER_ONLY is disabled.
/// \detail Not defined by default. Meaningless when #TOML_HEADER_ONLY is enabled.

// dll/shared lib function exports (legacy - TOML_API was the old name for this setting)
#if !defined(ATHENA_TOML_EXPORTED_MEMBER_FUNCTION) && !defined(ATHENA_TOML_EXPORTED_STATIC_FUNCTION)                                 \
	&& !defined(ATHENA_TOML_EXPORTED_FREE_FUNCTION) && !defined(ATHENA_TOML_EXPORTED_CLASS) && defined(ATHENA_TOML_API)
#define ATHENA_TOML_EXPORTED_MEMBER_FUNCTION ATHENA_TOML_API
#define ATHENA_TOML_EXPORTED_STATIC_FUNCTION ATHENA_TOML_API
#define ATHENA_TOML_EXPORTED_FREE_FUNCTION	  ATHENA_TOML_API
#endif

// dll/shared lib exports
#if ATHENA_TOML_SHARED_LIB
#undef ATHENA_TOML_API
#undef ATHENA_TOML_EXPORTED_CLASS
#undef ATHENA_TOML_EXPORTED_MEMBER_FUNCTION
#undef ATHENA_TOML_EXPORTED_STATIC_FUNCTION
#undef ATHENA_TOML_EXPORTED_FREE_FUNCTION
#if ATHENA_TOML_WINDOWS
#if ATHENA_TOML_IMPLEMENTATION
#define ATHENA_TOML_EXPORTED_CLASS			__declspec(dllexport)
#define ATHENA_TOML_EXPORTED_FREE_FUNCTION __declspec(dllexport)
#else
#define ATHENA_TOML_EXPORTED_CLASS			__declspec(dllimport)
#define ATHENA_TOML_EXPORTED_FREE_FUNCTION __declspec(dllimport)
#endif
#ifndef ATHENA_TOML_CALLCONV
#define ATHENA_TOML_CALLCONV __cdecl
#endif
#elif defined(__GNUC__) && __GNUC__ >= 4
#define ATHENA_TOML_EXPORTED_CLASS			  __attribute__((visibility("default")))
#define ATHENA_TOML_EXPORTED_MEMBER_FUNCTION __attribute__((visibility("default")))
#define ATHENA_TOML_EXPORTED_STATIC_FUNCTION __attribute__((visibility("default")))
#define ATHENA_TOML_EXPORTED_FREE_FUNCTION	  __attribute__((visibility("default")))
#endif
#endif
#ifndef ATHENA_TOML_EXPORTED_CLASS
#define ATHENA_TOML_EXPORTED_CLASS
#endif
#ifndef ATHENA_TOML_EXPORTED_MEMBER_FUNCTION
#define ATHENA_TOML_EXPORTED_MEMBER_FUNCTION
#endif
#ifndef ATHENA_TOML_EXPORTED_STATIC_FUNCTION
#define ATHENA_TOML_EXPORTED_STATIC_FUNCTION
#endif
#ifndef ATHENA_TOML_EXPORTED_FREE_FUNCTION
#define ATHENA_TOML_EXPORTED_FREE_FUNCTION
#endif
/// \def TOML_EXPORTED_CLASS
/// \brief An 'export' annotation to add to classes.
/// \detail Not defined by default.
///	\remark You might override this with `__declspec(dllexport)` if you were building the library
/// 		into the public API of a DLL on Windows.
///
/// \def TOML_EXPORTED_MEMBER_FUNCTION
/// \brief An 'export' annotation to add to non-static class member functions.
/// \detail Not defined by default.
///	\remark You might override this with `__declspec(dllexport)` if you were building the library
/// 		into the public API of a DLL on Windows.
///
/// \def TOML_EXPORTED_FREE_FUNCTION
/// \brief An 'export' annotation to add to free functions.
/// \detail Not defined by default.
///	\remark You might override this with `__declspec(dllexport)` if you were building the library
/// 		into the public API of a DLL on Windows.
///
/// \def TOML_EXPORTED_STATIC_FUNCTION
/// \brief An 'export' annotation to add to `static` class member functions.
/// \detail Not defined by default.
///	\remark You might override this with `__declspec(dllexport)` if you were building the library
/// 		into the public API of a DLL on Windows.

// experimental language features
#if !defined(ATHENA_TOML_ENABLE_UNRELEASED_FEATURES) && defined(ATHENA_TOML_UNRELEASED_FEATURES) // was TOML_UNRELEASED_FEATURES
																				   // pre-3.0
#define ATHENA_TOML_ENABLE_UNRELEASED_FEATURES ATHENA_TOML_UNRELEASED_FEATURES
#endif
#if (defined(ATHENA_TOML_ENABLE_UNRELEASED_FEATURES) && ATHENA_TOML_ENABLE_UNRELEASED_FEATURES) || ATHENA_TOML_INTELLISENSE
#undef ATHENA_TOML_ENABLE_UNRELEASED_FEATURES
#define ATHENA_TOML_ENABLE_UNRELEASED_FEATURES 1
#endif
#ifndef ATHENA_TOML_ENABLE_UNRELEASED_FEATURES
#define ATHENA_TOML_ENABLE_UNRELEASED_FEATURES 0
#endif
/// \def TOML_ENABLE_UNRELEASED_FEATURES
/// \brief Enables support for unreleased TOML language features not yet part of a
///		[numbered version](https://github.com/toml-lang/toml/releases).
/// \detail Defaults to `0`.
/// \see [TOML Language Support](https://github.com/marzer/tomlplusplus/blob/master/README.md#toml-language-support)

// parser
#if !defined(ATHENA_TOML_ENABLE_PARSER) && defined(ATHENA_TOML_PARSER) // was TOML_PARSER pre-3.0
#define ATHENA_TOML_ENABLE_PARSER ATHENA_TOML_PARSER
#endif
#if !defined(ATHENA_TOML_ENABLE_PARSER) || (defined(ATHENA_TOML_ENABLE_PARSER) && ATHENA_TOML_ENABLE_PARSER) || ATHENA_TOML_INTELLISENSE
#undef ATHENA_TOML_ENABLE_PARSER
#define ATHENA_TOML_ENABLE_PARSER 1
#endif
/// \def		TOML_ENABLE_PARSER
/// \brief		Sets whether the parser-related parts of the library are included.
/// \detail		Defaults to `1`.
/// \remarks	If you don't parse any TOML from files or strings, setting `TOML_ENABLE_PARSER`
///				to `0` can improve compilation speed and reduce binary size.

// formatters
#if !defined(ATHENA_TOML_ENABLE_FORMATTERS) || (defined(ATHENA_TOML_ENABLE_FORMATTERS) && ATHENA_TOML_ENABLE_FORMATTERS) || ATHENA_TOML_INTELLISENSE
#undef ATHENA_TOML_ENABLE_FORMATTERS
#define ATHENA_TOML_ENABLE_FORMATTERS 1
#endif
/// \def		TOML_ENABLE_FORMATTERS
/// \brief		Sets whether the various formatter classes are enabled.
/// \detail		Defaults to `1`.
/// \remarks	If you don't need to re-serialize TOML data, setting `TOML_ENABLE_FORMATTERS`
///				to `0` can improve compilation speed and reduce binary size.
/// \see
///  - toml::toml_formatter
///  - toml::json_formatter
///  - toml::yaml_formatter

// SIMD
#if !defined(ATHENA_TOML_ENABLE_SIMD) || (defined(ATHENA_TOML_ENABLE_SIMD) && ATHENA_TOML_ENABLE_SIMD) || ATHENA_TOML_INTELLISENSE
#undef ATHENA_TOML_ENABLE_SIMD
#define ATHENA_TOML_ENABLE_SIMD 1
#endif

// windows compat
#if !defined(ATHENA_TOML_ENABLE_WINDOWS_COMPAT) && defined(ATHENA_TOML_WINDOWS_COMPAT) // was TOML_WINDOWS_COMPAT pre-3.0
#define ATHENA_TOML_ENABLE_WINDOWS_COMPAT ATHENA_TOML_WINDOWS_COMPAT
#endif
#if !defined(ATHENA_TOML_ENABLE_WINDOWS_COMPAT) || (defined(ATHENA_TOML_ENABLE_WINDOWS_COMPAT) && ATHENA_TOML_ENABLE_WINDOWS_COMPAT)        \
	|| ATHENA_TOML_INTELLISENSE
#undef ATHENA_TOML_ENABLE_WINDOWS_COMPAT
#define ATHENA_TOML_ENABLE_WINDOWS_COMPAT 1
#endif
/// \cond
#if !ATHENA_TOML_WINDOWS
#undef ATHENA_TOML_ENABLE_WINDOWS_COMPAT
#define ATHENA_TOML_ENABLE_WINDOWS_COMPAT 0
#endif
/// \endcond
#ifndef ATHENA_TOML_INCLUDE_WINDOWS_H
#define ATHENA_TOML_INCLUDE_WINDOWS_H 0
#endif
/// \def TOML_ENABLE_WINDOWS_COMPAT
/// \brief Enables the use of wide strings (wchar_t, std::wstring) in various places throughout the library
/// 	   when building for Windows.
/// \detail Defaults to `1` when building for Windows, `0` otherwise. Has no effect when building for anything other
/// 		than Windows.
/// \remark	This <strong>does not</strong> change the underlying string type used to represent TOML keys and string
/// 		values; that will still be std::string. This setting simply enables some narrow &lt;=&gt; wide string
/// 		conversions when necessary at various interface boundaries.
/// 		<br><br>
/// 		If you're building for Windows and you have no need for Windows' "Pretends-to-be-unicode" wide strings,
/// 		you can safely set this to `0`.

// custom optional
#ifdef ATHENA_TOML_OPTIONAL_TYPE
#define ATHENA_TOML_HAS_CUSTOM_OPTIONAL_TYPE 1
#else
#define ATHENA_TOML_HAS_CUSTOM_OPTIONAL_TYPE 0
#endif
//# {{
#if ATHENA_TOML_DOXYGEN
#define ATHENA_TOML_OPTIONAL_TYPE
#endif
/// \def TOML_OPTIONAL_TYPE
/// \brief Overrides the `optional<T>` type used by the library.
/// \detail Not defined by default (use std::optional).
/// \warning The library uses optionals internally in a few places; if you choose to replace the optional type
/// 		 it must be with something that is still API-compatible with std::optional
/// 		 (e.g. [tl::optional](https://github.com/TartanLlama/optional)).
//# }}

// exceptions (library use)
#if ATHENA_TOML_COMPILER_HAS_EXCEPTIONS
#if !defined(ATHENA_TOML_EXCEPTIONS) || (defined(ATHENA_TOML_EXCEPTIONS) && ATHENA_TOML_EXCEPTIONS)
#undef ATHENA_TOML_EXCEPTIONS
#define ATHENA_TOML_EXCEPTIONS 1
#endif
#else
#if defined(ATHENA_TOML_EXCEPTIONS) && ATHENA_TOML_EXCEPTIONS
#error ATHENA_TOML_EXCEPTIONS was explicitly enabled but exceptions are disabled/unsupported by the compiler.
#endif
#undef ATHENA_TOML_EXCEPTIONS
#define ATHENA_TOML_EXCEPTIONS 0
#endif
/// \def TOML_EXCEPTIONS
/// \brief Sets whether the library uses exceptions to report parsing failures.
/// \detail Defaults to `1` or `0` according to your compiler's exception mode.

// calling convention for static/free/friend functions
#ifndef ATHENA_TOML_CALLCONV
#define ATHENA_TOML_CALLCONV
#endif
/// \def TOML_CALLCONV
/// \brief Calling convention to apply to exported free/static functions.
/// \detail Not defined by default (let the compiler decide).

//# {{
#if ATHENA_TOML_DOXYGEN
#define ATHENA_TOML_SMALL_FLOAT_TYPE
#endif
/// \def TOML_SMALL_FLOAT_TYPE
/// \brief If your codebase has an additional 'small' float type (e.g. half-precision), this tells toml++ about it.
/// \detail Not defined by default.
//# }}

#ifndef ATHENA_TOML_UNDEF_MACROS
#define ATHENA_TOML_UNDEF_MACROS 1
#endif

#ifndef ATHENA_TOML_MAX_NESTED_VALUES
#define ATHENA_TOML_MAX_NESTED_VALUES 256
// this refers to the depth of nested values, e.g. inline tables and arrays.
// 256 is crazy high! if you're hitting this limit with real input, TOML is probably the wrong tool for the job...
#endif

#ifdef ATHENA_TOML_CHAR_8_STRINGS
#if ATHENA_TOML_CHAR_8_STRINGS
#error ATHENA_TOML_CHAR_8_STRINGS was removed in athena_toml++ 2.0.0; all value setters and getters now work with char8_t strings implicitly.
#endif
#endif

#ifdef ATHENA_TOML_LARGE_FILES
#if !ATHENA_TOML_LARGE_FILES
#error Support for !ATHENA_TOML_LARGE_FILES (i.e. 'small files') was removed in athena_toml++ 3.0.0.
#endif
#endif

#ifndef ATHENA_TOML_LIFETIME_HOOKS
#define ATHENA_TOML_LIFETIME_HOOKS 0
#endif

#ifdef NDEBUG
#undef ATHENA_TOML_ASSERT
#define ATHENA_TOML_ASSERT(expr) static_assert(true)
#endif
#ifndef ATHENA_TOML_ASSERT
#ifndef assert
ATHENA_TOML_DISABLE_WARNINGS;
#include <cassert>
ATHENA_TOML_ENABLE_WARNINGS;
#endif
#define ATHENA_TOML_ASSERT(expr) assert(expr)
#endif
#ifdef NDEBUG
#define ATHENA_TOML_ASSERT_ASSUME(expr) ATHENA_TOML_ASSUME(expr)
#else
#define ATHENA_TOML_ASSERT_ASSUME(expr) ATHENA_TOML_ASSERT(expr)
#endif
/// \def TOML_ASSERT(expr)
/// \brief Sets the assert function used by the library.
/// \detail Defaults to the standard C `assert()`.

//# {{
#if ATHENA_TOML_DOXYGEN
#define ATHENA_TOML_SMALL_INT_TYPE
#endif
/// \def TOML_SMALL_INT_TYPE
/// \brief If your codebase has an additional 'small' integer type (e.g. 24-bits), this tells toml++ about it.
/// \detail Not defined by default.
//# }}

#ifndef ATHENA_TOML_ENABLE_FLOAT16
#define ATHENA_TOML_ENABLE_FLOAT16 0
#endif
//# {{
/// \def TOML_ENABLE_FLOAT16
/// \brief Enable support for the built-in `_Float16` type.
/// \detail Defaults to `0`.
//# }}

/// @}
//#====================================================================================================================
//# CHARCONV SUPPORT
//#====================================================================================================================

#if !defined(ATHENA_TOML_FLOAT_CHARCONV) && (ATHENA_TOML_GCC || ATHENA_TOML_CLANG || (ATHENA_TOML_ICC && !ATHENA_TOML_ICC_CL))
// not supported by any version of GCC or Clang as of 26/11/2020
// not supported by any version of ICC on Linux as of 11/01/2021
#define ATHENA_TOML_FLOAT_CHARCONV 0
#endif
#if !defined(ATHENA_TOML_INT_CHARCONV) && (defined(__EMSCRIPTEN__) || defined(__APPLE__))
// causes link errors on emscripten
// causes Mac OS SDK version errors on some versions of Apple Clang
#define ATHENA_TOML_INT_CHARCONV 0
#endif
#ifndef ATHENA_TOML_INT_CHARCONV
#define ATHENA_TOML_INT_CHARCONV 1
#endif
#ifndef ATHENA_TOML_FLOAT_CHARCONV
#define ATHENA_TOML_FLOAT_CHARCONV 1
#endif
#if (ATHENA_TOML_INT_CHARCONV || ATHENA_TOML_FLOAT_CHARCONV) && !ATHENA_TOML_HAS_INCLUDE(<charconv>)
#undef ATHENA_TOML_INT_CHARCONV
#undef ATHENA_TOML_FLOAT_CHARCONV
#define ATHENA_TOML_INT_CHARCONV	0
#define ATHENA_TOML_FLOAT_CHARCONV 0
#endif

//#=====================================================================================================================
//# SFINAE
//#=====================================================================================================================

/// \cond
#if defined(__cpp_concepts) && __cpp_concepts >= 201907
#define ATHENA_TOML_REQUIRES(...) requires(__VA_ARGS__)
#else
#define ATHENA_TOML_REQUIRES(...)
#endif
#define ATHENA_TOML_ENABLE_IF(...) , typename std::enable_if<(__VA_ARGS__), int>::type = 0
#define ATHENA_TOML_CONSTRAINED_TEMPLATE(condition, ...)                                                                      \
	template <__VA_ARGS__ ATHENA_TOML_ENABLE_IF(condition)>                                                                   \
	ATHENA_TOML_REQUIRES(condition)
#define ATHENA_TOML_HIDDEN_CONSTRAINT(condition, ...) ATHENA_TOML_CONSTRAINED_TEMPLATE(condition, __VA_ARGS__)
/// \endcond
//# {{
#ifndef ATHENA_TOML_CONSTRAINED_TEMPLATE
#define ATHENA_TOML_CONSTRAINED_TEMPLATE(condition, ...) template <__VA_ARGS__>
#endif
#ifndef ATHENA_TOML_HIDDEN_CONSTRAINT
#define ATHENA_TOML_HIDDEN_CONSTRAINT(condition, ...)
#endif
//# }}

//#=====================================================================================================================
//# FLOAT128
//#=====================================================================================================================

#if defined(__SIZEOF_FLOAT128__) && defined(__FLT128_MANT_DIG__) && defined(__LDBL_MANT_DIG__)                         \
	&& __FLT128_MANT_DIG__ > __LDBL_MANT_DIG__
#define ATHENA_TOML_FLOAT128 __float128
#endif

//#=====================================================================================================================
//# INT128
//#=====================================================================================================================

#ifdef __SIZEOF_INT128__
#define ATHENA_TOML_INT128	 __int128_t
#define ATHENA_TOML_UINT128 __uint128_t
#endif

//#====================================================================================================================
//# VERSIONS AND NAMESPACES
//#====================================================================================================================
// clang-format off

#include "version.hpp"

#define ATHENA_TOML_LIB_SINGLE_HEADER 0

#if ATHENA_TOML_ENABLE_UNRELEASED_FEATURES
	#define ATHENA_TOML_LANG_EFFECTIVE_VERSION													\
		ATHENA_TOML_MAKE_VERSION(ATHENA_TOML_LANG_MAJOR, ATHENA_TOML_LANG_MINOR, ATHENA_TOML_LANG_PATCH+1)
#else
	#define ATHENA_TOML_LANG_EFFECTIVE_VERSION													\
		ATHENA_TOML_MAKE_VERSION(ATHENA_TOML_LANG_MAJOR, ATHENA_TOML_LANG_MINOR, ATHENA_TOML_LANG_PATCH)
#endif

#define ATHENA_TOML_LANG_HIGHER_THAN(major, minor, patch)										\
		(ATHENA_TOML_LANG_EFFECTIVE_VERSION > ATHENA_TOML_MAKE_VERSION(major, minor, patch))

#define ATHENA_TOML_LANG_AT_LEAST(major, minor, patch)											\
		(ATHENA_TOML_LANG_EFFECTIVE_VERSION >= ATHENA_TOML_MAKE_VERSION(major, minor, patch))

#define ATHENA_TOML_LANG_UNRELEASED															\
		ATHENA_TOML_LANG_HIGHER_THAN(ATHENA_TOML_LANG_MAJOR, ATHENA_TOML_LANG_MINOR, ATHENA_TOML_LANG_PATCH)

#ifndef ATHENA_TOML_ABI_NAMESPACES
	#if ATHENA_TOML_DOXYGEN
		#define ATHENA_TOML_ABI_NAMESPACES 0
	#else
		#define ATHENA_TOML_ABI_NAMESPACES 1
	#endif
#endif
#if ATHENA_TOML_ABI_NAMESPACES
	#define ATHENA_TOML_NAMESPACE_START				namespace athena_toml { inline namespace ATHENA_TOML_CONCAT(v, ATHENA_TOML_LIB_MAJOR)
	#define ATHENA_TOML_NAMESPACE_END					} static_assert(true)
	#define ATHENA_TOML_NAMESPACE						::athena_toml::ATHENA_TOML_CONCAT(v, ATHENA_TOML_LIB_MAJOR)
	#define ATHENA_TOML_ABI_NAMESPACE_START(name)		inline namespace name { static_assert(true)
	#define ATHENA_TOML_ABI_NAMESPACE_BOOL(cond, T, F)	ATHENA_TOML_ABI_NAMESPACE_START(ATHENA_TOML_CONCAT(ATHENA_TOML_EVAL_BOOL_, cond)(T, F))
	#define ATHENA_TOML_ABI_NAMESPACE_END				} static_assert(true)
#else
	#define ATHENA_TOML_NAMESPACE_START				namespace athena_toml
	#define ATHENA_TOML_NAMESPACE_END					static_assert(true)
	#define ATHENA_TOML_NAMESPACE						athena_toml
	#define ATHENA_TOML_ABI_NAMESPACE_START(...)		static_assert(true)
	#define ATHENA_TOML_ABI_NAMESPACE_BOOL(...)		static_assert(true)
	#define ATHENA_TOML_ABI_NAMESPACE_END				static_assert(true)
#endif
#define ATHENA_TOML_IMPL_NAMESPACE_START				ATHENA_TOML_NAMESPACE_START { namespace impl
#define ATHENA_TOML_IMPL_NAMESPACE_END					} ATHENA_TOML_NAMESPACE_END
#if ATHENA_TOML_HEADER_ONLY
	#define ATHENA_TOML_ANON_NAMESPACE_START			static_assert(ATHENA_TOML_IMPLEMENTATION); ATHENA_TOML_IMPL_NAMESPACE_START
	#define ATHENA_TOML_ANON_NAMESPACE_END				ATHENA_TOML_IMPL_NAMESPACE_END
	#define ATHENA_TOML_ANON_NAMESPACE					ATHENA_TOML_NAMESPACE::impl
	#define ATHENA_TOML_EXTERNAL_LINKAGE				inline
	#define ATHENA_TOML_INTERNAL_LINKAGE				inline
#else
	#define ATHENA_TOML_ANON_NAMESPACE_START			static_assert(ATHENA_TOML_IMPLEMENTATION);	\
												using namespace athena_toml;				\
												namespace
	#define ATHENA_TOML_ANON_NAMESPACE_END				static_assert(true)
	#define ATHENA_TOML_ANON_NAMESPACE
	#define ATHENA_TOML_EXTERNAL_LINKAGE
	#define ATHENA_TOML_INTERNAL_LINKAGE				static
#endif

// clang-format on
//#====================================================================================================================
//# ASSERT
//#====================================================================================================================

//#====================================================================================================================
//# STATIC ASSERT MESSAGE FORMATTING
//#====================================================================================================================
// clang-format off
/// \cond

#if ATHENA_TOML_SIMPLE_STATIC_ASSERT_MESSAGES

	#define ATHENA_TOML_SA_NEWLINE		" "
	#define ATHENA_TOML_SA_LIST_SEP	", "
	#define ATHENA_TOML_SA_LIST_BEG	" ("
	#define ATHENA_TOML_SA_LIST_END	")"
	#define ATHENA_TOML_SA_LIST_NEW	" "
	#define ATHENA_TOML_SA_LIST_NXT	", "

#else

	#define ATHENA_TOML_SA_NEWLINE			"\n| "
	#define ATHENA_TOML_SA_LIST_SEP		ATHENA_TOML_SA_NEWLINE "  - "
	#define ATHENA_TOML_SA_LIST_BEG		ATHENA_TOML_SA_LIST_SEP
	#define ATHENA_TOML_SA_LIST_END
	#define ATHENA_TOML_SA_LIST_NEW		ATHENA_TOML_SA_NEWLINE ATHENA_TOML_SA_NEWLINE
	#define ATHENA_TOML_SA_LIST_NXT		ATHENA_TOML_SA_LIST_NEW

#endif

#define ATHENA_TOML_SA_NATIVE_VALUE_TYPE_LIST							\
	ATHENA_TOML_SA_LIST_BEG	"std::string"							\
	ATHENA_TOML_SA_LIST_SEP	"int64_t"								\
	ATHENA_TOML_SA_LIST_SEP	"double"								\
	ATHENA_TOML_SA_LIST_SEP	"bool"									\
	ATHENA_TOML_SA_LIST_SEP	"toml::date"							\
	ATHENA_TOML_SA_LIST_SEP	"toml::time"							\
	ATHENA_TOML_SA_LIST_SEP	"toml::date_time"						\
	ATHENA_TOML_SA_LIST_END

#define ATHENA_TOML_SA_NODE_TYPE_LIST									\
	ATHENA_TOML_SA_LIST_BEG	"toml::table"							\
	ATHENA_TOML_SA_LIST_SEP	"toml::array"							\
	ATHENA_TOML_SA_LIST_SEP	"toml::value<std::string>"				\
	ATHENA_TOML_SA_LIST_SEP	"toml::value<int64_t>"					\
	ATHENA_TOML_SA_LIST_SEP	"toml::value<double>"					\
	ATHENA_TOML_SA_LIST_SEP	"toml::value<bool>"						\
	ATHENA_TOML_SA_LIST_SEP	"toml::value<toml::date>"				\
	ATHENA_TOML_SA_LIST_SEP	"toml::value<toml::time>"				\
	ATHENA_TOML_SA_LIST_SEP	"toml::value<toml::date_time>"			\
	ATHENA_TOML_SA_LIST_END

#define ATHENA_TOML_SA_UNWRAPPED_NODE_TYPE_LIST						\
	ATHENA_TOML_SA_LIST_NEW	"A native TOML value type"				\
	ATHENA_TOML_SA_NATIVE_VALUE_TYPE_LIST								\
																\
	ATHENA_TOML_SA_LIST_NXT	"A TOML node type"						\
	ATHENA_TOML_SA_NODE_TYPE_LIST

/// \endcond
// clang-format on
