// SPDX-License-Identifier: GPL-3.0-or-later
#include "operational_plan.hpp"
#include "plan_retention.hpp" // Shared D6/D7 execution inventory budgets.
#include "../artifact/model.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace athena::activation {
namespace {
[[noreturn]] void reject(SchemaErrorCode code, const char* message) {
    throw SchemaError(code, message);
}
struct ByteLess {
    bool operator()(std::string_view a, std::string_view b) const {
        return artifact::path_bytes_less(a, b);
    }
};
struct SourceLess {
    bool operator()(const ActivationSource& a, const ActivationSource& b) const {
        return source_identity_less(a, b);
    }
};
using SourceMap = std::map<ActivationSource, std::uint64_t, SourceLess>;
using Paths = std::set<std::string, ByteLess>;
struct Projection {
    std::map<std::string, std::uint64_t, ByteLess> links;
    Paths directories;
};
struct SourcePaths {
    Paths links, directories;
    bool operator==(const SourcePaths&) const = default;
};
struct InventoryBudget {
    std::size_t entries = 0, bytes = 0;
    void add(const SourcePaths& inventory, std::size_t locator_size) {
        for (const auto* paths : {&inventory.links, &inventory.directories})
            for (const auto& value : *paths) {
                const auto size = value.size() + locator_size;
                if (++entries > max_retention_references || size > max_retention_bytes - bytes)
                    reject(SchemaErrorCode::Bounds, "Cumulative source projection budget exceeded");
                bytes += size;
            }
    }
};
void path(std::string_view value) {
    validate_relative_path(value);
    if (value == ".athena-activation" || value.starts_with(".athena-activation/"))
        reject(SchemaErrorCode::Conflict, "Reserved projection administration path");
}
void parents(Paths& directories, std::string value) {
    for (;;) {
        const auto slash = value.rfind('/');
        if (slash == value.npos) return;
        value.resize(slash);
        directories.insert(value);
        if (directories.size() > max_retention_references)
            reject(SchemaErrorCode::Bounds, "Derived parent directory inventory exceeds bound");
    }
}
Projection projection(const OperationalGeneration& input, const SourceMap& sources,
                      std::map<std::uint64_t, SourcePaths>& inventories,
                      const ProjectionBinding& binding, InventoryBudget& budget) {
    auto document = generation::validate_generation_document(input.document, input.id);
    if (document.digest != input.digest)
        reject(SchemaErrorCode::Reference, "Generation document digest mismatch");
    const auto& g = document.generation;
    std::map<std::string, std::size_t> artifacts;
    std::set<std::string> legacy;
    for (const auto& record : g.realizations)
        artifacts.emplace(record.artifact_id.value(), binding.store_authority.size() +
            (binding.store_authority == "/" ? 0 : 1) + record.object_reference().size());
    for (const auto& entry : g.entries)
        if (!legacy.insert(entry.store_path).second)
            reject(SchemaErrorCode::Conflict, "Ambiguous legacy generation membership");
    if (g.id == 0 && (!artifacts.empty() || !legacy.empty()))
        reject(SchemaErrorCode::Reference, "Generation zero requires empty membership");
    Projection result;
    std::set<ActivationSource, SourceLess> seen;
    for (const auto& inventory : input.sources) {
        if (!seen.insert(inventory.source).second)
            reject(SchemaErrorCode::Conflict, "Duplicate source projection");
        std::size_t locator_size;
        if (auto object = std::get_if<ArtifactSource>(&inventory.source)) {
            const auto member = artifacts.find(object->artifact_id.value());
            if (member == artifacts.end())
                reject(SchemaErrorCode::Reference, "Source is not generation membership");
            locator_size = member->second;
            artifacts.erase(member);
        } else {
            const auto locator = std::visit([](const auto& s) -> std::string {
                if constexpr (requires { s.locator; }) return s.locator;
                else return {};
            }, inventory.source);
            const auto prefix = binding.store_authority + '/';
            if (!locator.starts_with(prefix))
                reject(SchemaErrorCode::Reference, "Legacy source outside bound store");
            const auto relative = std::string_view(locator).substr(prefix.size());
            validate_relative_path(relative);
            if (relative == "objects" || relative.starts_with("objects/"))
                reject(SchemaErrorCode::Conflict, "Legacy source inside modern object namespace");
            if (!legacy.erase(locator))
                reject(SchemaErrorCode::Reference, "Source is not generation membership");
            locator_size = locator.size();
        }
        const auto index = sources.at(inventory.source);
        if (inventory.links.size() > max_retention_references ||
            inventory.directories.size() > max_retention_references)
            reject(SchemaErrorCode::Bounds, "Source projection exceeds bound");
        SourcePaths normalized;
        Paths explicit_directories;
        for (const auto& value : inventory.links) {
            path(value);
            if (!result.links.emplace(value, index).second)
                reject(SchemaErrorCode::Conflict, "Duplicate or colliding projection link");
            normalized.links.insert(value);
            parents(normalized.directories, value);
            if (normalized.links.size() + normalized.directories.size() > max_retention_references)
                reject(SchemaErrorCode::Bounds, "Derived link projection exceeds bound");
        }
        for (const auto& value : inventory.directories) {
            path(value);
            if (!explicit_directories.insert(value).second)
                reject(SchemaErrorCode::Conflict, "Duplicate explicit source directory");
            if (!std::holds_alternative<ArtifactSource>(inventory.source) &&
                !normalized.directories.contains(value))
                reject(SchemaErrorCode::Conflict, "Legacy directory is not manifest link scaffolding");
            normalized.directories.insert(value);
            parents(normalized.directories, value);
            if (normalized.links.size() + normalized.directories.size() > max_retention_references)
                reject(SchemaErrorCode::Bounds, "Derived source projection exceeds bound");
        }
        auto [previous, inserted] = inventories.emplace(index, normalized);
        if (!inserted && previous->second != normalized)
            reject(SchemaErrorCode::Conflict, "Same source has inconsistent projection inventories");
        if (inserted) budget.add(normalized, locator_size);
        result.directories.insert(normalized.directories.begin(), normalized.directories.end());
    }
    if (!artifacts.empty() || !legacy.empty())
        reject(SchemaErrorCode::Reference, "Incomplete generation source inventory");
    for (const auto& [value, ignored] : result.links)
        if (result.directories.contains(value))
            reject(SchemaErrorCode::Conflict, "Projection link/directory or ancestor collision");
    return result;
}
}

OperationalPlanResult build_operational_plan(const OperationalPlanInput& input) {
    input.binding.validate();
    ActivationIntent intent{input.activation_id, input.binding.generation_authority,
        input.binding.store_authority, input.binding.projection_root,
        input.binding.projection_id, input.target.id, input.target.digest, input.base};
    intent.validate();
    const auto* selected = std::get_if<SelectedBase>(&input.base);
    if (bool(selected) != input.base_generation.has_value())
        reject(SchemaErrorCode::Reference, "Selected base requires exactly one generation document");
    if (selected && (selected_generation(selected->current) != input.base_generation->id ||
                     selected->generation_digest != input.base_generation->digest))
        reject(SchemaErrorCode::Reference, "Base generation identity mismatch");
    if (selected && input.base_generation->id == input.target.id &&
        input.base_generation->digest != input.target.digest)
        reject(SchemaErrorCode::Reference, "Same generation has conflicting document digests");
    if (selected && std::holds_alternative<ActivationOwnership>(selected->ownership) &&
        std::get<ActivationOwnership>(selected->ownership).id == input.activation_id)
        reject(SchemaErrorCode::Reference, "Activation identity must differ from predecessor");

    SourceMap sources;
    std::map<std::string, ActivationSource> legacy_locators;
    auto collect = [&](const OperationalGeneration& g) {
        if (g.sources.size() > max_sources)
            reject(SchemaErrorCode::Bounds, "Source inventory exceeds bound");
        for (const auto& inventory : g.sources) {
            auto [entry, inserted] = sources.emplace(inventory.source, 0);
            if (!inserted && entry->first != inventory.source)
                reject(SchemaErrorCode::Conflict, "Conflicting source identity/seal claims");
            if (sources.size() > max_sources)
                reject(SchemaErrorCode::Bounds, "Combined source inventory exceeds bound");
            std::visit([&](const auto& s) {
                if constexpr (requires { s.locator; }) {
                    auto [at, fresh] = legacy_locators.emplace(s.locator, inventory.source);
                    if (!fresh && at->second != inventory.source)
                        reject(SchemaErrorCode::Conflict, "Ambiguous legacy locator");
                }
            }, inventory.source);
        }
    };
    if (input.base_generation) collect(*input.base_generation);
    collect(input.target);
    ActivationPlan plan{input.activation_id, {}, {}};
    for (auto& [source, index] : sources) {
        index = plan.sources.size();
        plan.sources.push_back(source);
    }
    // Reuse D1 source validation before relying on inventories.
    plan.validate();
    std::map<std::uint64_t, SourcePaths> inventories;
    InventoryBudget budget;
    auto desired = projection(input.target, sources, inventories, input.binding, budget);
    auto old = input.base_generation ?
        projection(*input.base_generation, sources, inventories, input.binding, budget) : Projection{};
    if (!selected && (input.target.id != 0 || !plan.sources.empty() ||
                      !desired.links.empty() || !desired.directories.empty()))
        reject(SchemaErrorCode::Reference, "Absent base permits only empty generation zero");
    if (selected && std::holds_alternative<EmptyOwnership>(selected->ownership) &&
        !input.base_generation->sources.empty())
        reject(SchemaErrorCode::Reference, "Empty ownership requires empty membership");

    std::map<std::string, WitnessReference, ByteLess> witnesses;
    std::set<std::string> witness_ids;
    if (input.old_witnesses.size() > max_operations)
        reject(SchemaErrorCode::Bounds, "Witness inventory exceeds bound");
    for (const auto& entry : input.old_witnesses) {
        path(entry.path);
        if (!old.links.contains(entry.path))
            reject(SchemaErrorCode::Reference, "Witness is not a base link");
        if (!witnesses.emplace(entry.path, entry.witness).second ||
            !witness_ids.insert(serialize(entry.witness)).second)
            reject(SchemaErrorCode::Conflict, "Duplicate or ambiguous base witness");
    }
    if (witnesses.size() != old.links.size())
        reject(SchemaErrorCode::Reference, "Incomplete base witness inventory");
    auto append = [&](std::string value, OperationState state) {
        if (plan.operations.size() == max_operations)
            reject(SchemaErrorCode::Bounds, "Derived operations exceed bound");
        plan.operations.push_back({0, std::move(value), std::move(state)});
    };
    for (const auto& [value, source] : old.links)
        if (!desired.links.contains(value)) append(value, RemoveLink{source, witnesses.at(value)});
    for (const auto& value : desired.directories) append(value, EnsureDirectory{});
    for (const auto& [value, source] : desired.links) {
        if (old.directories.contains(value))
            reject(SchemaErrorCode::Conflict, "Directory-to-link transition has no deletion authority");
        const WitnessReference fresh{input.activation_id, 0};
        const auto previous = old.links.find(value);
        if (previous == old.links.end()) append(value, CreateLink{source, fresh});
        else if (previous->second == source)
            append(value, KeepLink{source, witnesses.at(value), source, fresh});
        else append(value, ReplaceLink{previous->second, witnesses.at(value), source, fresh});
    }
    std::sort(plan.operations.begin(), plan.operations.end(), operation_less);
    for (std::size_t n = 0; n < plan.operations.size(); ++n) {
        auto& op = plan.operations[n];
        op.id = n;
        std::visit([&](auto& state) {
            if constexpr (requires { state.new_witness; }) state.new_witness.entry_id = n;
        }, op.state);
    }
    // Includes exact old-witness/base ownership and all canonical D1 checks.
    auto digest = derive_plan_digest(intent, plan);
    return {std::move(intent), std::move(plan), std::move(digest)};
}
}
