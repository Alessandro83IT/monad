//# This file is a part of toml++ and is subject to the the terms of the MIT license.
//# Copyright (c) Mark Gillard <mark.gillard@outlook.com.au>
//# See https://github.com/marzer/tomlplusplus/blob/master/LICENSE for the full license text.
// SPDX-License-Identifier: MIT
#pragma once

//# {{
#include "preprocessor.hpp"
#if !ATHENA_TOML_IMPLEMENTATION
#error This is an implementation-only header.
#endif
//# }}

#include "node.hpp"
#include "node_view.hpp"
#include "at_path.hpp"
#include "table.hpp"
#include "array.hpp"
#include "value.hpp"
#include "header_start.hpp"

ATHENA_TOML_NAMESPACE_START
{
	ATHENA_TOML_EXTERNAL_LINKAGE
	node::node() noexcept = default;

	ATHENA_TOML_EXTERNAL_LINKAGE
	node::~node() noexcept = default;

	ATHENA_TOML_EXTERNAL_LINKAGE
	node::node(node && other) noexcept //
		: source_{ std::exchange(other.source_, {}) }
	{}

	ATHENA_TOML_EXTERNAL_LINKAGE
	node::node(const node& /*other*/) noexcept
	{
		// does not copy source information - this is not an error
		//
		// see https://github.com/marzer/tomlplusplus/issues/49#issuecomment-665089577
	}

	ATHENA_TOML_EXTERNAL_LINKAGE
	node& node::operator=(const node& /*rhs*/) noexcept
	{
		// does not copy source information - this is not an error
		//
		// see https://github.com/marzer/tomlplusplus/issues/49#issuecomment-665089577

		source_ = {};
		return *this;
	}

	ATHENA_TOML_EXTERNAL_LINKAGE
	node& node::operator=(node&& rhs) noexcept
	{
		if (&rhs != this)
			source_ = std::exchange(rhs.source_, {});
		return *this;
	}

	ATHENA_TOML_EXTERNAL_LINKAGE
	node_view<node> node::at_path(std::string_view path) noexcept
	{
		return athena_toml::at_path(*this, path);
	}

	ATHENA_TOML_EXTERNAL_LINKAGE
	node_view<const node> node::at_path(std::string_view path) const noexcept
	{
		return athena_toml::at_path(*this, path);
	}

	ATHENA_TOML_EXTERNAL_LINKAGE
	node_view<node> node::at_path(const path& p) noexcept
	{
		return athena_toml::at_path(*this, p);
	}

	ATHENA_TOML_EXTERNAL_LINKAGE
	node_view<const node> node::at_path(const path& p) const noexcept
	{
		return athena_toml::at_path(*this, p);
	}

#if ATHENA_TOML_ENABLE_WINDOWS_COMPAT

	ATHENA_TOML_EXTERNAL_LINKAGE
	node_view<node> node::at_path(std::wstring_view path)
	{
		return athena_toml::at_path(*this, path);
	}

	ATHENA_TOML_EXTERNAL_LINKAGE
	node_view<const node> node::at_path(std::wstring_view path) const
	{
		return athena_toml::at_path(*this, path);
	}

#endif // TOML_ENABLE_WINDOWS_COMPAT

	ATHENA_TOML_EXTERNAL_LINKAGE
	node_view<node> node::operator[](const path& p) noexcept
	{
		return athena_toml::at_path(*this, p);
	}

	ATHENA_TOML_EXTERNAL_LINKAGE
	node_view<const node> node::operator[](const path& p) const noexcept
	{
		return athena_toml::at_path(*this, p);
	}
}
ATHENA_TOML_NAMESPACE_END;

ATHENA_TOML_IMPL_NAMESPACE_START
{
	ATHENA_TOML_PURE_GETTER
	ATHENA_TOML_EXTERNAL_LINKAGE
	bool ATHENA_TOML_CALLCONV node_deep_equality(const node* lhs, const node* rhs) noexcept
	{
		// both same or both null
		if (lhs == rhs)
			return true;

		// lhs null != rhs null or different types
		if ((!lhs != !rhs) || lhs->type() != rhs->type())
			return false;

		return lhs->visit(
			[=](auto& l) noexcept
			{
				using concrete_type = remove_cvref<decltype(l)>;

				return l == *(rhs->as<concrete_type>());
			});
	}
}
ATHENA_TOML_IMPL_NAMESPACE_END;

#include "header_end.hpp"
