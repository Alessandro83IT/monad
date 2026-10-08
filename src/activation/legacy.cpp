// SPDX-License-Identifier: GPL-3.0-or-later
#include "legacy.hpp"
#include "codec_detail.hpp"
#include "../artifact/encoding.hpp"
#include "../artifact/identity.hpp"
#include "../hash/sha256.hpp"
#include "../store/transaction_pins.hpp"
#include <algorithm>
#include <fcntl.h>
#include <linux/fs.h>
#include <map>
#include <set>
#include <unistd.h>
namespace athena::activation {
namespace d = core::detail;
namespace c = codec_detail;
namespace {
[[noreturn]] void bad(LegacyCause cause, std::string_view message, int error = 0,
    ProjectionOutcome outcome = ProjectionOutcome::NotMutated) {
    throw LegacyError(cause, outcome, error, message);
}
std::string loc(const LegacySource& s) { return std::visit([](const auto& v) { return v.locator; }, s); }
bool same(const struct statx& a, const struct statx& b) {
    return a.stx_ino == b.stx_ino && a.stx_dev_major == b.stx_dev_major &&
        a.stx_dev_minor == b.stx_dev_minor && a.stx_mnt_id == b.stx_mnt_id;
}
bool same_observations(const std::vector<artifact::ScanObservation>& first,
    const std::vector<artifact::ScanObservation>& second) {
    if (first.size() != second.size()) return false;
    for (std::size_t n = 0; n < first.size(); ++n) {
        const auto& a = first[n].status; const auto& b = second[n].status;
        if (first[n].path != second[n].path || a.st_dev != b.st_dev || a.st_ino != b.st_ino ||
            a.st_mode != b.st_mode || a.st_uid != b.st_uid || a.st_gid != b.st_gid || a.st_nlink != b.st_nlink ||
            a.st_size != b.st_size || a.st_mtim.tv_sec != b.st_mtim.tv_sec || a.st_mtim.tv_nsec != b.st_mtim.tv_nsec ||
            a.st_ctim.tv_sec != b.st_ctim.tv_sec || a.st_ctim.tv_nsec != b.st_ctim.tv_nsec) return false;
    }
    return true;
}
void frame(std::string& out, std::string_view bytes) { c::append_u64(out, bytes.size()); out.append(bytes); }
const std::string& content(const artifact::NormalizedArtifact& tree, std::string_view path) {
    for (const auto& e : tree.entries) if (e.path == path) {
        if (const auto* f = std::get_if<artifact::RegularFile>(&e.value)) return f->content;
        bad(LegacyCause::Manifest, "Administrative metadata must be a regular file");
    }
    bad(LegacyCause::Manifest, "Missing legacy administrative metadata");
}
bool has(const artifact::NormalizedArtifact& tree, std::string_view path) {
    return std::any_of(tree.entries.begin(), tree.entries.end(), [&](const auto& e) { return e.path == path; });
}
}
std::vector<std::string> LegacyAccess::manifest_projection(const artifact::NormalizedArtifact& tree) try {
    auto t = c::parse(content(tree, ".athena/manifest.toml")); c::keys(t, {"files"});
    const auto* files = t["files"].as_array();
    if (!files || files->size() > max_operations) bad(LegacyCause::Manifest, "Invalid manifest files array");
    std::set<std::string, decltype(&artifact::path_bytes_less)> paths(&artifact::path_bytes_less);
    for (const auto& n : *files) {
        const auto* row = n.as_table(); if (!row) bad(LegacyCause::Manifest, "Invalid manifest entry");
        c::keys(*row, {"path"}); auto path = c::string(*row, "path"); validate_projection_path(path);
        if (path == ".athena" || path.starts_with(".athena/") || !paths.insert(path).second)
            bad(LegacyCause::Manifest, "Duplicate or administrative manifest path");
        auto entry = std::lower_bound(tree.entries.begin(), tree.entries.end(), path,
            [](const auto& e, const auto& p) { return artifact::path_bytes_less(e.path, p); });
        if (entry == tree.entries.end() || entry->path != path || std::holds_alternative<artifact::Directory>(entry->value))
            bad(LegacyCause::Manifest, "Missing or directory manifest payload");
    }
    // A path-only manifest cannot authorize a second, unlisted payload tree.
    // Directory scaffolding and the two historical metadata files are allowed.
    for (const auto& e : tree.entries) {
        if (std::holds_alternative<artifact::Directory>(e.value)) continue;
        if (e.path == ".athena/manifest.toml" || e.path == ".athena/metadata.toml") continue;
        if (!paths.contains(e.path)) bad(LegacyCause::Manifest, "Unlisted legacy payload or conflicting metadata");
    }
    return {paths.begin(), paths.end()};
} catch (const SchemaError& e) {
    throw LegacyError(LegacyCause::Manifest, ProjectionOutcome::NotMutated, 0, e.what(), std::current_exception());
} catch (const ProjectionError& e) {
    // Here ProjectionConflict means a reserved path in the manifest, not a
    // filesystem conflict. Do not reclassify any other projection failure.
    if (e.cause != ProjectionCause::ProjectionConflict) throw;
    throw LegacyError(LegacyCause::Manifest, e.outcome, e.error, e.what(), std::current_exception());
}
namespace {
void metadata(const artifact::NormalizedArtifact& tree, std::string_view package) try {
    auto t = c::parse(content(tree, ".athena/metadata.toml"));
    for (const auto& [k, v] : t) {
        (void)v;
        if (k != "name" && k != "version" && k != "source" && k != "sha256" &&
            k != "build_system" && k != "dependencies" && k != "provides")
            bad(LegacyCause::Manifest, "Unknown legacy metadata key");
    }
    for (auto key : {"name", "version", "source", "sha256", "build_system"})
        if (c::string(t, key).empty()) bad(LegacyCause::Manifest, "Empty legacy metadata field");
    if (c::string(t, "name") != package) bad(LegacyCause::Manifest, "Generation package/metadata mismatch");
    auto strings = [](const toml::node_view<const toml::node>& view) {
        const auto* a = view.as_array(); if (!a) bad(LegacyCause::Manifest, "Invalid metadata array");
        std::set<std::string> seen;
        for (const auto& n : *a) { auto s = n.value<std::string>();
            if (!n.is_string() || !s || s->empty() || !seen.insert(*s).second)
                bad(LegacyCause::Manifest, "Invalid/duplicate metadata string"); }
    };
    if (t.contains("dependencies")) strings(std::as_const(t)["dependencies"]);
    if (t.contains("provides")) {
        const auto* p = t["provides"].as_table(); if (!p) bad(LegacyCause::Manifest, "Invalid provides metadata");
        for (const auto& [k, v] : *p) {
            (void)v; if (k != "features" && k != "capabilities") bad(LegacyCause::Manifest, "Unknown provides key");
            strings((*p)[k.str()]);
        }
    }
} catch (const SchemaError& e) {
    throw LegacyError(LegacyCause::Manifest, ProjectionOutcome::NotMutated, 0, e.what(), std::current_exception());
}
}
LegacyError::LegacyError(LegacyCause c, ProjectionOutcome o, int e, std::string_view message, std::exception_ptr u)
    : std::runtime_error(std::string(message)), cause(c), outcome(o), error(e), underlying(std::move(u)) {}

LegacyInventory LegacyAccess::inspect_impl(ProjectionContext& p, const ProjectionState* state,
    std::uint64_t generation_id, std::string_view locator, bool publish, const artifact::ScanLimits& limits,
    const LegacySource* expected) {
    bool barrier_started = false;
    try {
        if (state) p.state(*state);
        if (!publish && !expected) bad(LegacyCause::SealMismatch, "Verification requires an expected sealed source");
        p.revalidate(); validate_absolute_locator(locator);
        const auto overlaps = [](std::string_view a, std::string_view b) {
            const auto contains = [](std::string_view parent, std::string_view child) {
                return parent == child || (child.starts_with(parent) &&
                    (parent == "/" || (child.size() > parent.size() && child[parent.size()] == '/')));
            };
            return contains(a, b) || contains(b, a);
        };
        if (overlaps(locator, p.binding_.generation_authority) || overlaps(locator, p.binding_.projection_root))
            bad(LegacyCause::UnsafeSource, "Legacy source overlaps administrative or projection authority");
        const std::string prefix = p.binding_.store_authority + '/';
        if (!locator.starts_with(prefix)) bad(LegacyCause::UnsafeSource, "Legacy source outside bound store");
        const std::string relative(locator.substr(prefix.size())); validate_relative_path(relative);
        if (relative == "objects" || relative.starts_with("objects/"))
            bad(LegacyCause::UnsafeSource, "Legacy source inside modern namespace");
        const auto doc = generation_document(p, generation_id);
        std::string package;
        unsigned memberships = 0;
        for (const auto& e : doc.generation.entries) if (e.store_path == locator) { package = e.package; ++memberships; }
        if (memberships != 1) bad(LegacyCause::HistoricalAuthority, "Source must have one exact authoritative generation membership");
        const auto slash = relative.rfind('/');
        auto parent = slash == relative.npos ? d::Fd::adopt(::fcntl(p.store_.get(), F_DUPFD_CLOEXEC, 0)) :
            [&] { open_how h{}; h.flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC; h.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_XDEV;
                const auto name = relative.substr(0, slash); const int fd = p.io_.open(p.store_.get(), name.c_str(), h);
                if (fd < 0) bad(LegacyCause::UnsafeSource, "Unsafe legacy parent", errno); return d::Fd::adopt(fd); }();
        const auto leaf = relative.substr(slash == relative.npos ? 0 : slash + 1);
        const auto original = p.identity(parent.get(), leaf, false);
        open_how pin_how{}; pin_how.flags = O_PATH | O_NOFOLLOW | O_CLOEXEC;
        pin_how.resolve = RESOLVE_BENEATH | RESOLVE_NO_XDEV;
        const int pin = p.io_.open(parent.get(), leaf.c_str(), pin_how);
        if (pin < 0) bad(LegacyCause::UnsafeSource, "Pin legacy source inode", errno);
        auto source_inode = d::Fd::adopt(pin);
        const auto pinned_source = p.identity(source_inode.get());
        if (!(original.stx_mask & STATX_GID) || !(pinned_source.stx_mask & STATX_GID) || !same(original, pinned_source) || original.stx_mode != pinned_source.stx_mode ||
            original.stx_uid != pinned_source.stx_uid || original.stx_gid != pinned_source.stx_gid)
            bad(LegacyCause::UnsafeSource, "Source changed before pinning");
        const auto store_mount = p.identity(p.store_.get());
        if (original.stx_mnt_id != store_mount.stx_mnt_id || original.stx_uid != ::geteuid())
            bad(LegacyCause::UnsafeSource, "Legacy source mount/owner mismatch");
        std::string target, object_id;
        open_how h{}; h.flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
        h.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_XDEV;
        d::Fd root;
        std::unique_ptr<store::StoreGcLock> gc;
        if (S_ISDIR(original.stx_mode)) {
            const int fd = p.io_.open(parent.get(), leaf.c_str(), h);
            if (fd < 0) bad(LegacyCause::UnsafeSource, "Unsafe source directory", errno);
            root = d::Fd::adopt(fd);
            if (!same(original, p.identity(root.get()))) bad(LegacyCause::UnsafeSource, "Substituted source");
        } else if (S_ISLNK(original.stx_mode)) {
            artifact::validate_alias_metadata(parent.get(), leaf);
            target = p.literal(parent.get(), leaf);
            if (target.empty() || target.size() > max_path_size || target.find('\0') != target.npos)
                bad(LegacyCause::UnsafeSource, "Invalid alias literal");
            const auto resolved = (std::filesystem::path(std::string(locator)).parent_path() / target).lexically_normal();
            const auto objects = std::filesystem::path(p.binding_.store_authority) / "objects/v1/sha256";
            if (resolved.parent_path() != objects || !store::administrative::hex_name(resolved.filename().native(), 64))
                bad(LegacyCause::UnsafeSource, "Alias must name one exact authoritative object");
            object_id = "sha256:" + resolved.filename().native();
            gc = std::make_unique<store::StoreGcLock>(p.binding_.store_authority);
            store::AuthorityContext(p.binding_.store_authority, p.binding_.generation_authority).validate_binding(false);
            // Follow no symlink, including intermediate components concealed by ..;
            // literal is retained separately. NO_XDEV restricts resolution to parent mount.
            h.resolve = RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV;
            const int fd = p.io_.open(parent.get(), target.c_str(), h);
            if (fd < 0) bad(LegacyCause::UnsafeSource, "Unsafe alias resolution", errno);
            root = d::Fd::adopt(fd);
            auto named = d::open_directory(resolved);
            if (!same(p.identity(root.get()), p.identity(named.get()))) bad(LegacyCause::UnsafeSource, "Alias object substitution");
        } else bad(LegacyCause::UnsafeSource, "Unsupported legacy source type");
        std::vector<artifact::ScanObservation> observed;
        auto tree = artifact::scan_bounded(root.get(), limits, &observed);
        std::vector<LegacyEntryMetadata> metadata_entries;
        for (const auto& o : observed) {
            if (o.status.st_uid != ::geteuid() || (!S_ISLNK(o.status.st_mode) && (o.status.st_mode & 0022)))
                bad(LegacyCause::UnsafeSource, "Legacy inventory must exclude untrusted payload writers");
            metadata_entries.push_back({o.path,
                static_cast<std::uint32_t>(o.status.st_mode & S_IFMT), static_cast<std::uint32_t>(o.status.st_mode & 07777),
                static_cast<std::uint32_t>(o.status.st_uid), static_cast<std::uint32_t>(o.status.st_gid)});
        }
        std::sort(metadata_entries.begin(), metadata_entries.end(), [](const auto& a, const auto& b) { return artifact::path_bytes_less(a.path, b.path); });
        std::string metadata_bytes;
        c::append_u64(metadata_bytes, metadata_entries.size());
        for (const auto& e : metadata_entries) {
            frame(metadata_bytes, e.path); c::append_u64(metadata_bytes, e.type); c::append_u64(metadata_bytes, e.permissions);
            c::append_u64(metadata_bytes, e.uid); c::append_u64(metadata_bytes, e.gid);
        }
        std::vector<std::string> projection;
        if (object_id.empty() || has(tree, ".athena/manifest.toml")) {
            projection = manifest_projection(tree); metadata(tree, package);
        } else if (has(tree, ".athena")) bad(LegacyCause::Manifest, "Conflicting alias administrative metadata");
        if (!object_id.empty() && artifact::derive_artifact_id(tree).value() != object_id)
            bad(LegacyCause::UnsafeSource, "Alias artifact identity mismatch");
        std::string encoding = "athena.legacy-seal.v1\n";
        frame(encoding, p.binding_.generation_authority); frame(encoding, p.binding_.store_authority);
        frame(encoding, locator); frame(encoding, object_id.empty() ? "legacy_tree" : "legacy_alias");
        frame(encoding, target); frame(encoding, object_id);
        std::string locator_metadata;
        c::append_u64(locator_metadata, original.stx_mode); c::append_u64(locator_metadata, original.stx_uid); c::append_u64(locator_metadata, original.stx_gid);
        frame(encoding, locator_metadata);
        frame(encoding, package); frame(encoding, artifact::canonical_bytes(tree)); frame(encoding, metadata_bytes);
        c::append_u64(encoding, projection.size()); for (const auto& path : projection) frame(encoding, path);
        const auto digest = LegacySealDigest(hash::sha256(encoding));
        LegacySource source = object_id.empty() ? LegacySource{LegacyTreeSource{std::string(locator), digest}} :
            LegacySource{LegacyAliasSource{std::string(locator), target, core::ArtifactId(object_id), digest}};
        if (expected && source != *expected) bad(LegacyCause::SealMismatch, "Legacy seal identity mismatch");
        c::Writer seal_record;
        seal_record.number("schema_version", 1);
        seal_record.text("seal_digest", digest.value());
        seal_record.text("generation_authority_hex", encode_hex(p.binding_.generation_authority));
        seal_record.text("store_authority_hex", encode_hex(p.binding_.store_authority));
        seal_record.text("kind", object_id.empty() ? "legacy_tree" : "legacy_alias");
        seal_record.number("source_mode", original.stx_mode); seal_record.number("source_uid", original.stx_uid); seal_record.number("source_gid", original.stx_gid);
        seal_record.text("locator_hex", encode_hex(locator));
        seal_record.text("alias_target_hex", encode_hex(target));
        seal_record.text("artifact_id", object_id);
        seal_record.text("package_hex", encode_hex(package));
        std::string inventory_encoding = "athena.legacy-inventory.v1\n";
        frame(inventory_encoding, artifact::canonical_bytes(tree)); frame(inventory_encoding, metadata_bytes);
        seal_record.text("inventory_digest", hash::sha256(inventory_encoding));
        std::vector<std::pair<std::string, d::Fd>> source_ancestors;
        std::vector<std::pair<std::string, d::Fd>> alias_ancestors;
        auto store_namespace_parent = d::open_directory(std::filesystem::path(p.binding_.store_authority).parent_path(), false, true);
        auto filesystem_root = d::open_directory("/");
        const auto root_owner = d::stat_fd(filesystem_root.get()).st_uid;
        const auto trusted_ancestor = [&](int fd) {
            const auto ancestor = d::stat_fd(fd);
            if ((ancestor.st_uid != root_owner && ancestor.st_uid != ::geteuid()) ||
                ((ancestor.st_mode & 0022) && !(ancestor.st_mode & S_ISVTX)))
                bad(LegacyCause::UnsafeSource, "Unsafe source namespace ancestor ownership/permissions");
            if (p.identity(fd).stx_mnt_id != store_mount.stx_mnt_id)
                bad(LegacyCause::UnsafeSource, "Source namespace ancestor mount mismatch");
        };
        const auto retain_ancestors = [&](std::string path) {
            for (;;) {
                open_how how{}; how.flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
                how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_XDEV;
                const int raw = p.io_.open(p.store_.get(), path.empty() ? "." : path.c_str(), how);
                if (raw < 0) bad(LegacyCause::UnsafeSource, "Unsafe source namespace ancestor", errno);
                auto fd = d::Fd::adopt(raw); trusted_ancestor(fd.get());
                source_ancestors.emplace_back(path, std::move(fd));
                if (path.empty()) break;
                const auto last = path.rfind('/'); path = last == path.npos ? "" : path.substr(0, last);
            }
        };
        retain_ancestors(slash == relative.npos ? "" : relative.substr(0, slash));
        if (!object_id.empty()) retain_ancestors("objects/v1/sha256");
        if (!target.empty()) {
            for (auto slash_position = target.find('/'); slash_position != target.npos;
                 slash_position = target.find('/', slash_position + 1)) {
                if (alias_ancestors.size() >= limits.depth) bad(LegacyCause::UnsafeSource, "Alias namespace depth bound");
                const auto prefix_path = slash_position ? target.substr(0, slash_position) : "/";
                const int raw = p.io_.open(parent.get(), prefix_path.c_str(), h);
                if (raw < 0) bad(LegacyCause::UnsafeSource, "Unsafe alias literal ancestor", errno);
                auto fd = d::Fd::adopt(raw); trusted_ancestor(fd.get());
                alias_ancestors.emplace_back(prefix_path, std::move(fd));
            }
        }
        auto check_named = [&] {
            p.revalidate();
            const auto after = p.identity(parent.get(), leaf, false);
            if (!same(original, after) || original.stx_mode != after.stx_mode || original.stx_uid != after.stx_uid || original.stx_gid != after.stx_gid ||
                (!target.empty() && p.literal(parent.get(), leaf) != target)) bad(LegacyCause::UnsafeSource, "Source substituted");
            if (!target.empty()) {
                artifact::validate_alias_metadata(parent.get(), leaf);
                const int fd = p.io_.open(parent.get(), target.c_str(), h);
                if (fd < 0) bad(LegacyCause::UnsafeSource, "Alias target changed", errno);
                auto fresh = d::Fd::adopt(fd);
                if (!same(p.identity(fresh.get()), p.identity(root.get())))
                    bad(LegacyCause::UnsafeSource, "Alias object substituted");
            }
            if (generation_document(p, generation_id).digest != doc.digest)
                bad(LegacyCause::HistoricalAuthority, "Generation changed during sealing");
            // Reopen the parent through store authority, rejecting renamed ancestry.
            if (slash != relative.npos) {
                open_how ph{}; ph.flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
                ph.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_XDEV;
                auto path = relative.substr(0, slash); const int fd = p.io_.open(p.store_.get(), path.c_str(), ph);
                if (fd < 0) bad(LegacyCause::UnsafeSource, "Source ancestry changed", errno);
                auto fresh = d::Fd::adopt(fd); if (!same(p.identity(fresh.get()), p.identity(parent.get())))
                    bad(LegacyCause::UnsafeSource, "Source parent substituted");
            }
            for (const auto& [path, fd] : source_ancestors) {
                trusted_ancestor(fd.get());
                open_how how{}; how.flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
                how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_XDEV;
                const int raw = p.io_.open(p.store_.get(), path.empty() ? "." : path.c_str(), how);
                if (raw < 0) bad(LegacyCause::UnsafeSource, "Source namespace ancestor changed", errno);
                auto named = d::Fd::adopt(raw);
                if (!same(p.identity(fd.get()), p.identity(named.get()))) bad(LegacyCause::UnsafeSource, "Source namespace ancestor substituted");
            }
            for (const auto& [literal_prefix, fd] : alias_ancestors) {
                trusted_ancestor(fd.get());
                const int raw = p.io_.open(parent.get(), literal_prefix.c_str(), h);
                if (raw < 0) bad(LegacyCause::UnsafeSource, "Alias literal ancestry changed", errno);
                auto named = d::Fd::adopt(raw);
                if (!same(p.identity(fd.get()), p.identity(named.get()))) bad(LegacyCause::UnsafeSource, "Alias literal ancestor substituted");
            }
            if (store_namespace_parent.get() >= 0) {
                auto named = d::open_directory(std::filesystem::path(p.binding_.store_authority).parent_path(), false, true);
                if (!same(p.identity(store_namespace_parent.get()), p.identity(named.get())))
                    bad(LegacyCause::UnsafeSource, "Store namespace parent substituted");
            }
        };
        check_named();
        // Content identity is not durability evidence. Both publication and
        // verification synchronize THIS pinned instance, including fresh copies
        // with an identical seal digest. No prior instance's barriers are reused.
        {
            d::require_durable_directory(root.get(), p.io_.durable);
            auto sync = [&](int fd) {
                if (S_ISDIR(d::stat_fd(fd).st_mode)) d::require_durable_directory(fd, p.io_.durable);
                barrier_started = true; int rc; do { rc = p.io_.fsync(fd); } while (rc < 0 && errno == EINTR);
                if (rc < 0) bad(LegacyCause::Durability, "Legacy seal barrier failed", errno, ProjectionOutcome::OutcomeUncertain);
            };
            auto open_entry = [&](const std::string& path, bool directory) {
                open_how how{}; how.flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | (directory ? O_DIRECTORY : 0);
                how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_XDEV;
                const int fd = p.io_.open(root.get(), path.c_str(), how);
                if (fd < 0) bad(LegacyCause::UnsafeSource, "Source changed before barrier", errno);
                return d::Fd::adopt(fd);
            };
            std::map<std::string, const struct stat*> observations;
            for (const auto& o : observed) observations.emplace(o.path, &o.status);
            const auto sync_entry = [&](const std::string& path, bool directory) {
                auto fd = open_entry(path, directory);
                const auto* original_entry = observations.at(path);
                const auto check = [&] {
                    const auto current_entry = d::stat_fd(fd.get());
                    if (!same_observations({{path, *original_entry}}, {{path, current_entry}}))
                        bad(LegacyCause::UnsafeSource, "Inventory entry substituted before/during barrier");
                    const auto named = p.identity(root.get(), path, false);
                    if (!same(p.identity(fd.get()), named)) bad(LegacyCause::UnsafeSource, "Synchronized entry name substituted");
                };
                check(); sync(fd.get()); check();
            };
            for (const auto& e : tree.entries) if (std::holds_alternative<artifact::RegularFile>(e.value)) {
                sync_entry(e.path, false);
            }
            for (auto it = tree.entries.rbegin(); it != tree.entries.rend(); ++it)
                if (std::holds_alternative<artifact::Directory>(it->value)) sync_entry(it->path, true);
            sync(root.get()); sync(parent.get());
            // Literal aliases can traverse real directories before '..'. Those
            // namespace entries are required even when lexical resolution has
            // the same final object. Retain and sync every literal prefix rather
            // than borrowing evidence for only the normalized object path.
            for (auto it = alias_ancestors.rbegin(); it != alias_ancestors.rend(); ++it) sync(it->second.get());
            // All store-relative administrative ancestors are part of the seal's
            // durability chain, including a nested alias object namespace.
            for (const auto& [path, fd] : source_ancestors) sync(fd.get());
            sync(store_namespace_parent.get());
            check_named();
            std::vector<artifact::ScanObservation> after;
            if (artifact::scan_bounded(root.get(), limits, &after) != tree || after.size() != observed.size())
                bad(LegacyCause::SealMismatch, "Source changed across seal barriers");
            if (!same_observations(observed, after)) bad(LegacyCause::UnsafeSource, "Source inode/metadata substituted across barriers");
            check_named();
        }
        // The canonical record proves explicit historical sealing authorization,
        // not completion of barriers for the instance reopened in this operation.
        const std::string seal_name = digest.value().substr(7) + ".toml";
        d::Fd seals;
        if (publish) {
            if (p.io_.mkdir(p.generation_.get(), ".athena-legacy-seals", 0700) == 0) barrier_started = true;
            else if (errno != EEXIST) bad(LegacyCause::Io, "Create legacy seal namespace", errno);
        }
        struct stat seal_namespace{};
        if (!publish && ::fstatat(p.generation_.get(), ".athena-legacy-seals", &seal_namespace, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT)
            bad(LegacyCause::SealMismatch, "Missing durable seal namespace");
        seals = d::open_at(p.generation_.get(), ".athena-legacy-seals", O_RDONLY | O_DIRECTORY);
        store::administrative::private_directory(seals.get()); store::administrative::same_mount(p.generation_.get(), seals.get());
        store::administrative::unchanged(p.generation_.get(), ".athena-legacy-seals", seals.get());
        struct stat present{};
        const bool exists = ::fstatat(seals.get(), seal_name.c_str(), &present, AT_SYMLINK_NOFOLLOW) == 0;
        if (!exists && errno != ENOENT) bad(LegacyCause::Io, "Inspect seal record", errno);
        if (exists) {
            if (store::administrative::read_file(seals.get(), seal_name) != seal_record.bytes())
                bad(LegacyCause::SealMismatch, "Conflicting published seal record");
        } else if (!publish) bad(LegacyCause::SealMismatch, "Missing durable seal publication");
        else {
            barrier_started = true;
            const auto temp = d::private_directory(seals.get(), p.io_.durable);
            auto preparation = d::open_at(seals.get(), temp, O_RDONLY | O_DIRECTORY);
            store::administrative::private_directory(preparation.get()); store::administrative::same_mount(seals.get(), preparation.get());
            auto file = create_record(p, preparation.get(), "seal.toml");
            d::complete_write(file.get(), seal_record.bytes(), p.io_.durable);
            int rc; do { rc = p.io_.fsync(file.get()); } while (rc < 0 && errno == EINTR);
            if (rc < 0) bad(LegacyCause::Durability, "Seal record fsync", errno);
            p.barrier(preparation.get(), true);
            check_named();
            store::administrative::unchanged(seals.get(), temp, preparation.get());
            store::administrative::unchanged(preparation.get(), "seal.toml", file.get());
            if (p.io_.rename(preparation.get(), "seal.toml", seals.get(), seal_name.c_str(), RENAME_NOREPLACE) < 0)
                bad(LegacyCause::Io, "Seal NOREPLACE publication", errno);
            p.barrier(seals.get(), true); p.barrier(preparation.get(), true);
            // Private empty preparation is retained; D3 does not add a cleanup
            // engine or infer publication from residue. Exact retries use only
            // the canonical published record, never an arbitrary temp witness.
        }
        d::Fd published_record;
        {
            barrier_started = true;
            published_record = d::open_at(seals.get(), seal_name, O_RDONLY);
            store::administrative::private_file(published_record.get());
            store::administrative::same_mount(seals.get(), published_record.get());
            store::administrative::unchanged(seals.get(), seal_name, published_record.get());
            int rc; do { rc = p.io_.fsync(published_record.get()); } while (rc < 0 && errno == EINTR);
            if (rc < 0) bad(LegacyCause::Durability, "Published seal record fsync", errno);
            p.barrier(seals.get(), true); p.barrier(p.generation_.get(), true);
            auto generation_parent = d::open_directory(std::filesystem::path(p.binding_.generation_authority).parent_path(), false, true);
            d::require_durable_directory(generation_parent.get(), p.io_.durable);
            p.barrier(generation_parent.get(), true);
            check_named();
            if (store::administrative::read_file(seals.get(), seal_name) != seal_record.bytes())
                bad(LegacyCause::SealMismatch, "Seal record changed after administrative barriers");
            store::administrative::unchanged(p.generation_.get(), ".athena-legacy-seals", seals.get());
            store::administrative::unchanged(seals.get(), seal_name, published_record.get());
            check_named();
        }
        std::vector<artifact::ScanObservation> final_observations;
        if (artifact::scan_bounded(root.get(), limits, &final_observations) != tree)
            bad(LegacyCause::SealMismatch, "Source changed during seal publication/verification");
        if (!same_observations(observed, final_observations)) bad(LegacyCause::UnsafeSource, "Source substituted during seal publication/verification");
        check_named();
        store::administrative::unchanged(seals.get(), seal_name, published_record.get());
        return {std::move(source), std::move(tree), std::move(projection), std::move(metadata_entries)};
    } catch (LegacyError& e) { if (barrier_started) e.outcome = ProjectionOutcome::OutcomeUncertain; throw; }
    catch (const d::PersistenceError& e) { throw LegacyError(LegacyCause::Durability,
        barrier_started ? ProjectionOutcome::OutcomeUncertain : ProjectionOutcome::NotMutated, e.error, e.what(), std::current_exception()); }
    catch (const ProjectionError& e) { throw LegacyError(e.cause == ProjectionCause::DurabilityFailure ? LegacyCause::Durability : LegacyCause::UnsafeSource,
        barrier_started ? ProjectionOutcome::OutcomeUncertain : ProjectionOutcome::NotMutated, e.error, e.what(), std::current_exception()); }
    catch (const std::exception& e) { throw LegacyError(LegacyCause::UnsafeSource,
        barrier_started ? ProjectionOutcome::OutcomeUncertain : ProjectionOutcome::NotMutated, 0, e.what(), std::current_exception()); }
}
LegacyInventory LegacyAccess::inspect(ProjectionContext& p, const ProjectionState& s, std::uint64_t g,
    std::string_view locator, bool publish, const artifact::ScanLimits& limits, const LegacySource* expected) {
    return inspect_impl(p, &s, g, locator, publish, limits, expected);
}
LegacyInventory seal_legacy(ProjectionContext& p, const ProjectionState& s, std::uint64_t g,
    std::string_view locator, const artifact::ScanLimits& limits) { return LegacyAccess::inspect(p, s, g, locator, true, limits); }
LegacyInventory verify_legacy_seal(ProjectionContext& p, const ProjectionState& s, std::uint64_t g,
    const LegacySource& source, const artifact::ScanLimits& limits) {
    return LegacyAccess::inspect(p, s, g, loc(source), false, limits, &source);
}
}
