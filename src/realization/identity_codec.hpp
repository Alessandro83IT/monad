// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <athena/core/install_plan.hpp>
#include <toml++/toml.hpp>
namespace athena::realization {
// Internal shared candidate codec; never a second identity representation.
toml::table encode_candidate(const core::CandidateIdentity&);
core::CandidateIdentity decode_candidate(const toml::table&);
void validate_candidate(const core::CandidateIdentity&);
}
