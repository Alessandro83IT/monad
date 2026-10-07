// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "remover.hpp"

#include "generation_builder.hpp"
#include "../installed/environment.hpp"

#include "../generation/generation.hpp"
#include "../paths/paths.hpp"
#include "../store/object_store.hpp"
#include "../store/transaction_pins.hpp"

#include <algorithm>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace athena::install {

void remove_packages(
    const std::vector<std::string>& package_names,
    const std::filesystem::path& target_root
)
{
    remove_packages(package_names, target_root,
        athena::core::PathLayout::from_root(athena::paths::root()));
}

void remove_packages(
    const std::vector<std::string>& package_names,
    const std::filesystem::path& target_root,
    const athena::core::PathLayout& paths
)
{
    paths.validate();
    if (package_names.empty()) return;

    athena::generation::GenerationManager generations(
        paths.generation_state,
        athena::generation::GenerationManager::GenerationDirectoryTag{}
    );

    const auto current = generations.current();

    // Membership comes from both collections, never from a repository lookup
    // or a synthetic legacy Package for a realization.
    RemovePlan removal;
    for (const auto& name : package_names) {
        if (name.empty()) {
            throw std::runtime_error("Cannot remove a package with an empty name");
        }
        const bool legacy = std::any_of(current.entries.begin(), current.entries.end(),
            [&](const auto& entry) { return entry.package == name; });
        const bool realized = std::any_of(current.realizations.begin(), current.realizations.end(),
            [&](const auto& record) { return record.candidate.package.value() == name; });
        if (!legacy && !realized) {
            throw std::runtime_error("Cannot remove package '" + name + "': package is not installed");
        }
        removal.add(name);
    }
    if (removal.empty()) return;

    auto candidate = build_removal_generation(current, removal);
    // Keep semantic rejection read-only, matching existing install/remove contracts.
    installed::capture_legacy_semantics(candidate);
    installed::validate_environment(candidate);
    athena::store::TransactionPins transaction({paths.store, generations.authority()});
    {
        core::FileLock state(core::state_lock_path(generations.authority()), core::LockKind::State, core::LockMode::Shared);
        store::StoreGcLock gc(paths.store);
        const auto stable_current = generations.current();
        candidate = build_removal_generation(stable_current, removal);
        installed::capture_legacy_semantics(candidate);
        installed::validate_environment(candidate);
        for (const auto& record : candidate.realizations) transaction.add_locked(record.artifact_id, gc);
    }
    athena::store::ObjectStore objects(paths.store);
    const auto target = generations.create_pinned(candidate.entries, candidate.realizations, candidate.policies, transaction);
    try {
        generations.switch_to(target.id, target_root, objects);
    }
    catch (...) {
        // Preserve the activation error; discard refuses to delete current.
        try { generations.discard(target.id); } catch (...) {}
        throw;
    }

    transaction.handoff(target.id);
    std::cout << "Rimozione completata. "
              << "Nuova generation: "
              << target.id
              << '\n';
}

} // namespace athena::install
