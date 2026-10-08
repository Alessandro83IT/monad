// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "identity.hpp"
namespace athena::activation {
struct ProjectionBinding {
    ProjectionId projection_id;
    std::string generation_authority, store_authority, projection_root;
    void validate() const;
    bool operator==(const ProjectionBinding&) const = default;
};
std::string serialize(const ProjectionBinding&);
ProjectionBinding parse_projection_binding(std::string_view);
}
