// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "../generation/generation.hpp"
#include "../realization/executor.hpp"

namespace athena::install {
// Execute the new core plan against an initialized generation manager.
// All locators/authority are explicit. Empty plans are validated no-ops.
// Unselected memberships survive; selected package names replace either kind.
void install_plan(const core::InstallPlan& plan,
                  generation::GenerationManager& generations,
                  const store::ObjectStore& objects,
                  const realization::ExecutionOptions& execution,
                  const std::filesystem::path& target_root);
}
