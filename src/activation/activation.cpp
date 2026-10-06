#include "activation.hpp"
#include "../store/manifest.hpp"
#include "../artifact/model.hpp"
#include <iostream>
#include <stdexcept>
#include <utility>

namespace athena::activation {
namespace {
using Path = std::filesystem::path;
Projection legacy_projection(const Path& source) {
    Projection result;
    for (const auto& entry : store::read_manifest(source)) result.links.push_back(entry.path);
    return result;
}
void require_source(const Path& source) {
    if (!std::filesystem::exists(source))
        throw std::runtime_error("Directory dello store non trovata: " + source.string());
}
// Inspect each ancestor without following target symlinks. In particular a
// projected artifact symlink must never become a writable payload parent.
void real_parents(const Path& path) {
    Path current;
    for (const auto& part : std::filesystem::absolute(path)) {
        current /= part;
        const auto status = std::filesystem::symlink_status(current);
        if (status.type() == std::filesystem::file_type::not_found) continue;
        if (!std::filesystem::is_directory(status))
            throw std::runtime_error("Conflitto: il parent non è una directory reale: " + current.string());
    }
}
void outside_source(const Path& source, const Path& target) {
    const auto relative = std::filesystem::absolute(target).lexically_normal().lexically_relative(
        std::filesystem::absolute(source).lexically_normal());
    if (!relative.empty() && *relative.begin() != "..")
        throw std::runtime_error("Activation target must be outside the immutable source: " + target.string());
}
void validate(const Projection& projection) {
    for (const auto& path : projection.links) artifact::validate_path(path);
    for (const auto& path : projection.directories) artifact::validate_path(path);
}
void activate_impl(const Path& source, const Path& root, const Projection& projection, bool strict) {
    require_source(source);
    if (strict) validate(projection);
    std::cout << "Attivazione del pacchetto: " << source.string() << '\n';
    for (const auto& path : projection.directories) {
        outside_source(source, root / path);
        real_parents((root / path).parent_path());
        const auto status = std::filesystem::symlink_status(root / path);
        if (status.type() != std::filesystem::file_type::not_found && !std::filesystem::is_directory(status))
            throw std::runtime_error("Conflitto: la directory di destinazione esiste già: " + (root / path).string());
    }
    for (const auto& path : projection.links) {
        if (strict) { outside_source(source, root / path); real_parents((root / path).parent_path()); }
        if (!target_is_free(root / path))
            throw std::runtime_error("Conflitto: il file di destinazione esiste già: " + (root / path).string());
    }
    std::vector<Path> activated, directories;
    activated.reserve(projection.links.size()); directories.reserve(projection.directories.size());
    auto create_directories = [&](const Path& target) {
        std::vector<Path> missing;
        for (auto path = target; !path.empty() &&
             std::filesystem::symlink_status(path).type() == std::filesystem::file_type::not_found;
             path = path.parent_path()) missing.push_back(path);
        for (auto it = missing.rbegin(); it != missing.rend(); ++it) {
            // Journal each successful mkdir, including implicit ancestors.
            if (std::filesystem::create_directory(*it)) directories.push_back(*it);
        }
    };
    try {
        for (const auto& path : projection.directories) {
            const auto target = root / path;
            create_directories(target);
        }
        for (const auto& path : projection.links) {
            const auto target = root / path;
            if (strict) real_parents(target.parent_path());
            create_directories(target.parent_path());
            std::filesystem::create_symlink(source / path, target);
            activated.push_back(target);
        }
    } catch (...) {
        for (auto it = activated.rbegin(); it != activated.rend(); ++it) {
            std::error_code error; std::filesystem::remove(*it, error);
        }
        for (auto it = directories.rbegin(); it != directories.rend(); ++it) {
            std::error_code error; std::filesystem::remove(*it, error);
        }
        throw;
    }
}
void deactivate_impl(const Path& source, const Path& root, const Projection& projection, bool strict) {
    require_source(source);
    if (strict) validate(projection);
    std::cout << "Disattivazione del pacchetto: " << source.string() << '\n';
    // Validate all ownership before removal, avoiding a partially removed source
    // on an ordinary conflict. Filesystem errors still have existing best-effort semantics.
    std::vector<Path> targets;
    for (const auto& path : projection.links) {
        const auto target = root / path;
        if (strict) { outside_source(source, target); real_parents(target.parent_path()); }
        const auto status = std::filesystem::symlink_status(target);
        if (status.type() == std::filesystem::file_type::not_found) continue;
        if (!std::filesystem::is_symlink(status))
            throw std::runtime_error("Impossibile disattivare: il target non è un symlink: " + target.string());
        if (std::filesystem::absolute(target.parent_path() / std::filesystem::read_symlink(target)) !=
            std::filesystem::absolute(source / path))
            throw std::runtime_error("Impossibile disattivare: il symlink non punta allo store previsto: " + target.string());
        if (strict) targets.push_back(target);
        else std::filesystem::remove(target); // Preserve historical legacy iteration semantics.
    }
    for (const auto& target : targets) std::filesystem::remove(target);
    // As for legacy activation's parent directories, real directories are shared
    // scaffolding and retained. No source directory or symlink target is edited.
}
}
bool target_is_free(const Path& target) {
    return std::filesystem::symlink_status(target).type() == std::filesystem::file_type::not_found;
}
void activate(const Path& source, const Path& root) {
    require_source(source);
    activate_impl(source, root, legacy_projection(source), false);
}
void deactivate(const Path& source, const Path& root) {
    require_source(source);
    deactivate_impl(source, root, legacy_projection(source), false);
}
void activate(const Path& source, const Path& root, const Projection& projection) {
    activate_impl(source, root, projection, true);
}
void deactivate(const Path& source, const Path& root, const Projection& projection) {
    deactivate_impl(source, root, projection, true);
}
}
