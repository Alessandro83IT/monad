// SPDX-License-Identifier: GPL-3.0-or-later
#include "legacy.hpp"
#include "codec_detail.hpp"
#include "../store/transaction_pins.hpp"
#include "../artifact/identity.hpp"
#include <algorithm>
#include <fcntl.h>
#include <linux/fs.h>
#include <map>
#include <set>
#include <unistd.h>
#include <dirent.h>
namespace athena::activation {
namespace d = core::detail;
namespace a = store::administrative;
namespace {
[[noreturn]] void reject(LegacyCause cause, std::string_view message, int e = 0) {
    throw LegacyError(cause, ProjectionOutcome::NotMutated, e, message);
}
bool same(const struct statx& x, const struct statx& y) {
    return x.stx_ino == y.stx_ino && x.stx_mnt_id == y.stx_mnt_id &&
        x.stx_dev_major == y.stx_dev_major && x.stx_dev_minor == y.stx_dev_minor;
}
bool same_record_observation(const struct statx& x, const struct statx& y) {
    constexpr unsigned required = STATX_SIZE | STATX_MTIME | STATX_CTIME;
    return (x.stx_mask & required) == required && (y.stx_mask & required) == required && same(x, y) &&
        x.stx_mode == y.stx_mode && x.stx_uid == y.stx_uid && x.stx_nlink == y.stx_nlink && x.stx_size == y.stx_size &&
        x.stx_mtime.tv_sec == y.stx_mtime.tv_sec && x.stx_mtime.tv_nsec == y.stx_mtime.tv_nsec &&
        x.stx_ctime.tv_sec == y.stx_ctime.tv_sec && x.stx_ctime.tv_nsec == y.stx_ctime.tv_nsec;
}
std::string loc(const LegacySource& s) { return std::visit([](const auto& v) { return v.locator; }, s); }
std::vector<LegacySource> legacy_sources(const AdoptionRecord& record) {
    std::vector<LegacySource> result;
    for (const auto& s : record.sources) std::visit([&](const auto& v) {
        if constexpr (!std::is_same_v<std::decay_t<decltype(v)>, ArtifactSource>) result.emplace_back(v);
    }, s);
    return result;
}
HistoricalCurrent historical(int fd) {
    auto file = d::open_at(fd, "current", O_RDONLY | O_NONBLOCK);
    const auto before = d::stat_fd(file.get());
    if (!S_ISREG(before.st_mode) || before.st_uid != ::geteuid() ||
        before.st_nlink != 1 || (before.st_mode & 07022) || before.st_size < 0 || before.st_size > max_current_size)
        reject(LegacyCause::HistoricalAuthority, "Unsafe historical current metadata");
    a::same_mount(fd, file.get()); a::unchanged(fd, "current", file.get());
    std::string bytes; char buffer[1024];
    for (;;) {
        auto n = ::read(file.get(), buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) reject(LegacyCause::Io, "Read historical current", errno);
        if (!n) break;
        if (bytes.size() + n > max_current_size) reject(LegacyCause::HistoricalAuthority, "Oversized current");
        bytes.append(buffer, n);
    }
    a::unchanged(fd, "current", file.get());
    const auto after = d::stat_fd(file.get());
    if (before.st_size != after.st_size || before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
        before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
        reject(LegacyCause::HistoricalAuthority, "Current changed during read");
    auto selection = parse_current_selection(bytes);
    if (const auto* h = std::get_if<HistoricalCurrent>(&selection)) return *h;
    reject(LegacyCause::HistoricalAuthority, "Adoption requires historical current");
}
AdoptionRecord read_record(int fd) { return parse_adoption_record(a::read_file(fd, "adoption.toml")); }
std::vector<std::string> entries(int fd) {
    const int copy = ::openat(fd, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (copy < 0) reject(LegacyCause::Io, "Open bounded directory stream", errno);
    DIR* raw = ::fdopendir(copy);
    if (!raw) { ::close(copy); reject(LegacyCause::Io, "Bounded fdopendir", errno); }
    const auto close = [](DIR* value) { ::closedir(value); };
    std::unique_ptr<DIR, decltype(close)> stream(raw, close);
    std::vector<std::string> result;
    for (;;) {
        errno = 0; auto* node = ::readdir(raw);
        if (!node) { if (errno) reject(LegacyCause::Io, "Bounded readdir", errno); break; }
        std::string name(node->d_name);
        if (name == "." || name == "..") continue;
        if (result.size() >= max_operations || name.size() > max_component_size)
            reject(LegacyCause::ProjectionConflict, "Directory inventory bound");
        result.push_back(std::move(name));
    }
    std::sort(result.begin(), result.end()); return result;
}
}
AdoptionRecord LegacyAccess::expected(ProjectionContext& p, const ProjectionState* state, const AdoptionId& id,
    HistoricalCurrent current, const std::vector<LegacySource>& sources) {
    if (state) p.state(*state);
    p.revalidate();
    const auto doc = generation_document(p, current.generation);
    if (sources.size() != doc.generation.entries.size() || sources.size() > max_sources)
        reject(LegacyCause::HistoricalAuthority, "Incomplete/oversized source seal inventory");
    AdoptionRecord record{id, p.binding_, std::move(current), doc.digest, {}, {}};
    for (const auto& s : sources) std::visit([&](const auto& v) { record.sources.emplace_back(v); }, s);
    std::set<std::string> seen;
    for (std::size_t n = 0; n < sources.size(); ++n) {
        if (loc(sources[n]) != doc.generation.entries[n].store_path || !seen.insert(loc(sources[n])).second)
            reject(LegacyCause::HistoricalAuthority, "Sources must match exact generation membership order");
        auto inventory = inspect_impl(p, state, doc.generation.id, loc(sources[n]), false, {}, &sources[n]);
        // The historical activation API requires a manifest, including for an
        // alias. Modern objects without manifests can be sealed, but cannot be
        // claimed as having a historical path-only projection.
        if (std::holds_alternative<LegacyAliasSource>(sources[n]) &&
            !std::any_of(inventory.tree.entries.begin(), inventory.tree.entries.end(), [](const auto& e) { return e.path == ".athena/manifest.toml"; }))
            reject(LegacyCause::HistoricalAuthority, "Alias has no historical manifest projection");
        for (const auto& path : inventory.projection) {
            if (record.entries.size() >= max_operations) reject(LegacyCause::HistoricalAuthority, "Expected projection entry bound");
            record.entries.push_back({0, n, path, loc(sources[n]) + '/' + path, {id, 0}});
        }
    }
    if (!doc.generation.realizations.empty()) {
        store::StoreGcLock gc(p.binding_.store_authority);
        store::AuthorityContext(p.binding_.store_authority, p.binding_.generation_authority).validate_binding(false);
        std::set<std::string> artifacts;
        for (const auto& realization : doc.generation.realizations) {
            if (!artifacts.insert(realization.artifact_id.value()).second) continue;
            if (record.sources.size() >= max_sources) reject(LegacyCause::HistoricalAuthority, "Expected source bound");
            const auto source_index = record.sources.size(); record.sources.emplace_back(ArtifactSource{realization.artifact_id});
            const auto directory = "objects/v1/sha256/" + realization.artifact_id.value().substr(7);
            open_how how{}; how.flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
            how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_XDEV;
            const int fd = p.io_.open(p.store_.get(), directory.c_str(), how);
            if (fd < 0) reject(LegacyCause::HistoricalAuthority, "Unsafe historical artifact source", errno);
            auto root = d::Fd::adopt(fd); auto tree = artifact::scan_bounded(root.get());
            if (artifact::derive_artifact_id(tree) != realization.artifact_id)
                reject(LegacyCause::HistoricalAuthority, "Historical artifact content mismatch");
            for (const auto& e : tree.entries) {
                if (record.directories.size() >= max_operations || record.entries.size() >= max_operations)
                    reject(LegacyCause::HistoricalAuthority, "Expected artifact projection bound");
                if (std::holds_alternative<artifact::Directory>(e.value)) record.directories.push_back(e.path);
                else record.entries.push_back({0, source_index, e.path, p.binding_.store_authority + '/' + directory + '/' + e.path, {id, 0}});
            }
            const int fresh = p.io_.open(p.store_.get(), directory.c_str(), how);
            if (fresh < 0) reject(LegacyCause::HistoricalAuthority, "Artifact source disappeared", errno);
            auto named = d::Fd::adopt(fresh);
            if (!same(p.identity(root.get()), p.identity(named.get()))) reject(LegacyCause::HistoricalAuthority, "Artifact source substituted");
        }
    }
    std::set<std::string, decltype(&artifact::path_bytes_less)> directories(&artifact::path_bytes_less);
    std::size_t directory_bytes = 0;
    auto add_directory = [&](const std::string& path) {
        if (directories.insert(path).second) {
            directory_bytes += path.size();
            if (directories.size() > max_operations || directory_bytes > 64 * 1024 * 1024)
                reject(LegacyCause::HistoricalAuthority, "Projection directory expansion bound");
        }
    };
    for (const auto& path : record.directories) add_directory(path);
    for (const auto& e : record.entries) {
        auto path = e.path;
        for (auto slash = path.rfind('/'); slash != path.npos; slash = path.rfind('/')) {
            path.resize(slash); add_directory(path);
        }
    }
    record.directories.assign(directories.begin(), directories.end());
    std::sort(record.entries.begin(), record.entries.end(), [](const auto& x, const auto& y) { return artifact::path_bytes_less(x.path, y.path); });
    for (std::size_t n = 0; n < record.entries.size(); ++n) { record.entries[n].id = n; record.entries[n].witness = {id, n}; }
    record.validate();
    if (generation_document(p, record.current.generation).digest != doc.digest)
        reject(LegacyCause::HistoricalAuthority, "Generation changed during historical authorization");
    return record;
}
void LegacyAccess::namespace_shape(const ProjectionContext& p) {
    auto root = p.directory(p.admin_.get(), "adoptions", true);
    auto prep = p.directory(root.get(), ".preparations", true);
    // Partial preparation is opaque here and never ownership evidence. Adoption
    // itself validates exact shape and records before reusing it.
    for (const auto& id : entries(prep.get())) { AdoptionId check(id); auto dir = p.directory(prep.get(), id, true); }
    for (const auto& name : entries(root.get())) {
        if (name == ".preparations") continue;
        AdoptionId id(name); auto dir = p.directory(root.get(), name, true);
        if (entries(dir.get()) != std::vector<std::string>{"adoption.toml", "witnesses"})
            reject(LegacyCause::PreparationConflict, "Published adoption has unexpected shape");
        auto record = read_record(dir.get());
        if (record.id != id || record.binding != p.binding_) reject(LegacyCause::HistoricalAuthority, "Adoption namespace authority mismatch");
        auto witnesses = p.directory(dir.get(), "witnesses", true);
        std::set<std::string> names;
        for (const auto& e : record.entries) names.insert(std::to_string(e.id));
        const auto actual = entries(witnesses.get());
        if (std::set<std::string>(actual.begin(), actual.end()) != names)
            reject(LegacyCause::WitnessMismatch, "Incomplete published witnesses");
        for (const auto& e : record.entries) p.link_identity(witnesses.get(), std::to_string(e.id), e.target, ProjectionCause::MissingWitness);
    }
}
d::Fd LegacyAccess::resolve(const ProjectionContext& const_p, std::string_view path, std::string_view target,
    const WitnessReference& reference) {
    (void)serialize(reference);
    const auto* id = std::get_if<AdoptionId>(&reference.owner);
    if (!id) reject(LegacyCause::WitnessMismatch, "Adoption reference required");
    auto& p = const_cast<ProjectionContext&>(const_p);
    struct statx present{};
    if (p.io_.stat(p.admin_.get(), "adoptions", AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_MNT_ID, &present) < 0 && errno == ENOENT)
        throw ProjectionError(ProjectionCause::UnsupportedWitnessKind, ProjectionOutcome::NotMutated, 0, "No authorized D3 adoption namespace");
    auto root = p.directory(p.admin_.get(), "adoptions", true);
    auto container = p.directory(root.get(), id->value(), true);
    auto record = read_record(container.get());
    if (record.id != *id || record.binding != p.binding_ || reference.entry_id >= record.entries.size())
        reject(LegacyCause::HistoricalAuthority, "Adoption witness authority mismatch");
    const auto& entry = record.entries[reference.entry_id];
    if (entry.path != path || entry.target != target || entry.witness != reference)
        reject(LegacyCause::WitnessMismatch, "Witness does not authorize this exact entry");
    if (expected(p, nullptr, *id, record.current, legacy_sources(record)) != record)
        reject(LegacyCause::HistoricalAuthority, "Adoption expected projection/source authority changed");
    auto witnesses = p.directory(container.get(), "witnesses", true);
    p.unchanged(root.get(), id->value(), container.get()); p.unchanged(container.get(), "witnesses", witnesses.get());
    return witnesses;
}
ProjectionOutcome LegacyAccess::adopt(ProjectionContext& p, const ProjectionState& state,
    const AdoptionId& id, const std::vector<LegacySource>& sources) {
    bool touched = false;
    try {
        p.state(state); p.revalidate();
        auto current_inode = d::open_at(p.generation_.get(), "current", O_RDONLY | O_NONBLOCK);
        const auto current_observation = p.identity(current_inode.get());
        const auto current = historical(p.generation_.get());
        const auto generation_name = std::to_string(current.generation);
        auto generation_directory = d::open_at(p.generation_.get(), generation_name, O_RDONLY | O_DIRECTORY);
        a::same_mount(p.generation_.get(), generation_directory.get());
        auto document_inode = d::open_at(generation_directory.get(), "generation.toml", O_RDONLY | O_NONBLOCK);
        a::same_mount(generation_directory.get(), document_inode.get());
        const auto document_observation = p.identity(document_inode.get());
        auto authority_unchanged = [&] {
            p.unchanged(p.generation_.get(), generation_name, generation_directory.get());
            if (!same_record_observation(current_observation, p.identity(p.generation_.get(), "current", false)) ||
                !same_record_observation(document_observation, p.identity(generation_directory.get(), "generation.toml", false)) ||
                historical(p.generation_.get()) != current)
                reject(LegacyCause::HistoricalAuthority, "Historical selection/document substituted or changed");
        };
        const auto desired = expected(p, &state, id, current, sources);
        const auto bytes = serialize(desired);
        // Complete root inventory: every symlink must be expected; regular files
        // and foreign directory trees conflict. Shared empty scaffolding is kept.
        std::map<std::string, const AdoptedEntry*, decltype(&artifact::path_bytes_less)> wanted(&artifact::path_bytes_less);
        for (const auto& e : desired.entries) wanted.emplace(e.path, &e);
        std::vector<d::Fd> pinned;
        std::vector<struct statx> identities;
        auto projection = [&] {
            std::size_t count = 0;
            std::set<std::string> observed;
            std::function<void(int, std::string, unsigned)> walk = [&](int fd, std::string prefix, unsigned depth) {
                if (depth > 128) reject(LegacyCause::ProjectionConflict, "Projection depth bound");
                for (const auto& name : entries(fd)) {
                    if (prefix.empty() && name == ".athena-activation") continue;
                    if (++count > max_operations) reject(LegacyCause::ProjectionConflict, "Projection entry bound");
                    const auto path = prefix.empty() ? name : prefix + '/' + name; validate_projection_path(path);
                    const auto s = p.identity(fd, name, false); p.same_mount(s);
                    if (S_ISDIR(s.stx_mode)) { auto dir = p.directory(p.root_.get(), path); walk(dir.get(), path, depth + 1); }
                    else {
                        auto it = wanted.find(path);
                        if (it == wanted.end()) reject(LegacyCause::ProjectionConflict, "Unexpected foreign projection entry");
                        p.link_identity(fd, name, it->second->target, ProjectionCause::MissingManaged);
                        observed.insert(path);
                    }
                }
            };
            walk(p.root_.get(), {}, 0);
            for (const auto& path : desired.directories) { auto directory = p.directory(p.root_.get(), path); }
            if (observed.size() != wanted.size()) reject(LegacyCause::ProjectionConflict, "Missing historical managed entry");
            authority_unchanged();
            if (generation_document(p, current.generation).digest != desired.generation_digest)
                reject(LegacyCause::HistoricalAuthority, "Historical current/generation changed");
            p.revalidate();
        };
        projection();
        for (const auto& e : desired.entries) {
            auto [parent, leaf] = p.parent(e.path);
            open_how h{}; h.flags = O_PATH | O_NOFOLLOW | O_CLOEXEC; h.resolve = RESOLVE_BENEATH | RESOLVE_NO_XDEV;
            const int fd = p.io_.open(parent.get(), leaf.c_str(), h);
            if (fd < 0) reject(LegacyCause::ProjectionConflict, "Pin historical symlink", errno);
            pinned.push_back(d::Fd::adopt(fd)); identities.push_back(p.identity(pinned.back().get()));
        }
        auto unchanged = [&] {
            projection();
            for (std::size_t n = 0; n < desired.entries.size(); ++n) {
                auto [parent, leaf] = p.parent(desired.entries[n].path);
                if (!same(identities[n], p.identity(parent.get(), leaf, false)))
                    reject(LegacyCause::WitnessMismatch, "Historical symlink substituted during adoption");
            }
        };
        auto exists = [&](int parent, const std::string& name) {
            struct statx s{};
            if (p.io_.stat(parent, name.c_str(), AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_MNT_ID, &s) == 0) return true;
            if (errno == ENOENT) return false; reject(LegacyCause::Io, "Adoption namespace inspection", errno);
        };
        d::Fd root, preparations;
        if (!exists(p.admin_.get(), "adoptions")) {
            root = p.mkdir_private(p.admin_.get(), "adoptions"); touched = true;
            preparations = p.mkdir_private(root.get(), ".preparations");
        } else { root = p.directory(p.admin_.get(), "adoptions", true); preparations = p.directory(root.get(), ".preparations", true); }
        auto publication_barriers = [&] {
            // A retry must restore both sides of the publication rename, even
            // when its source name is already absent after an uncertain attempt.
            p.unchanged(p.admin_.get(), "adoptions", root.get());
            p.unchanged(root.get(), ".preparations", preparations.get());
            p.barrier(root.get(), true); p.barrier(preparations.get(), true); p.barrier(p.admin_.get(), true);
            p.unchanged(p.admin_.get(), "adoptions", root.get());
            p.unchanged(root.get(), ".preparations", preparations.get());
        };
        auto check_witnesses = [&](int container, bool complete) {
            auto witnesses = p.directory(container, "witnesses", true);
            const auto names = entries(witnesses.get());
            if (complete && names.size() != desired.entries.size()) reject(LegacyCause::PreparationConflict, "Incomplete adoption witnesses");
            for (const auto& name : names) {
                const auto n = codec_detail::decimal(name);
                if (n >= desired.entries.size()) reject(LegacyCause::PreparationConflict, "Unexpected preparation witness");
                const auto& e = desired.entries[n];
                p.link_identity(witnesses.get(), name, e.target, ProjectionCause::MissingWitness);
                auto [parent, leaf] = p.parent(e.path);
                if (!same(p.identity(witnesses.get(), name, false), p.identity(parent.get(), leaf, false)) ||
                    !same(identities[n], p.identity(witnesses.get(), name, false)))
                    reject(LegacyCause::WitnessMismatch, "Same-target foreign witness/managed inode");
            }
            return witnesses;
        };
        if (exists(root.get(), id.value())) {
            auto published = p.directory(root.get(), id.value(), true);
            if (exists(preparations.get(), id.value()) || read_record(published.get()) != desired)
                reject(LegacyCause::PreparationConflict, "Conflicting published adoption retry");
            auto witnesses = check_witnesses(published.get(), true); unchanged();
            // A previous final barrier may have failed. Exact retry re-establishes
            // all barriers before returning NotMutated.
            auto published_record = d::open_at(published.get(), "adoption.toml", O_RDONLY);
            a::private_file(published_record.get()); a::same_mount(published.get(), published_record.get());
            p.unchanged(published.get(), "adoption.toml", published_record.get());
            touched = true;
            int rc; do { rc = p.io_.fsync(published_record.get()); } while (rc < 0 && errno == EINTR);
            if (rc < 0) reject(LegacyCause::Durability, "Published adoption record fsync", errno);
            p.barrier(witnesses.get(), true); p.barrier(published.get(), true);
            publication_barriers();
            if (expected(p, &state, id, current, sources) != desired)
                reject(LegacyCause::HistoricalAuthority, "Source/generation changed during published adoption retry");
            p.unchanged(published.get(), "adoption.toml", published_record.get());
            if (read_record(published.get()) != desired) reject(LegacyCause::PreparationConflict, "Published adoption record changed during retry");
            p.unchanged(root.get(), id.value(), published.get());
            p.unchanged(published.get(), "witnesses", witnesses.get());
            unchanged();
            return ProjectionOutcome::NotMutated;
        }
        d::Fd preparation;
        if (exists(preparations.get(), id.value())) {
            preparation = p.directory(preparations.get(), id.value(), true);
            if (entries(preparation.get()) != std::vector<std::string>{"adoption.toml", "witnesses"} || read_record(preparation.get()) != desired)
                reject(LegacyCause::PreparationConflict, "Unexpected/incomplete adoption preparation");
            auto witnesses = check_witnesses(preparation.get(), false);
        } else {
            preparation = p.mkdir_private(preparations.get(), id.value()); touched = true;
            auto witnesses = p.mkdir_private(preparation.get(), "witnesses");
            auto file = create_record(p, preparation.get(), "adoption.toml");
            d::complete_write(file.get(), bytes, p.io_.durable);
            int rc; do { rc = p.io_.fsync(file.get()); } while (rc < 0 && errno == EINTR);
            if (rc < 0) reject(LegacyCause::Durability, "Adoption record fsync", errno);
            p.barrier(preparation.get(), true);
        }
        touched = true;
        // Exact recovered record must be resynchronized; a visible valid record
        // alone does not prove that its earlier file barrier succeeded.
        auto file = d::open_at(preparation.get(), "adoption.toml", O_RDONLY);
        a::private_file(file.get()); a::same_mount(preparation.get(), file.get());
        p.unchanged(preparation.get(), "adoption.toml", file.get());
        int rc; do { rc = p.io_.fsync(file.get()); } while (rc < 0 && errno == EINTR);
        if (rc < 0) reject(LegacyCause::Durability, "Recovered adoption record fsync", errno);
        p.unchanged(preparation.get(), "adoption.toml", file.get());
        auto witnesses = check_witnesses(preparation.get(), false);
        p.barrier(preparation.get(), true); p.barrier(preparations.get(), true);
        for (const auto& e : desired.entries) {
            authority_unchanged();
            auto [parent, leaf] = p.parent(e.path); const auto name = std::to_string(e.id);
            p.link_identity(parent.get(), leaf, e.target, ProjectionCause::MissingManaged);
            if (!same(identities[e.id], p.identity(parent.get(), leaf, false)))
                reject(LegacyCause::WitnessMismatch, "Historical link changed before witness creation");
            p.unchanged(preparations.get(), id.value(), preparation.get()); p.unchanged(preparation.get(), "witnesses", witnesses.get());
            if (p.io_.link(parent.get(), leaf.c_str(), witnesses.get(), name.c_str(), 0) < 0 && errno != EEXIST)
                reject(LegacyCause::Io, "Adoption symlink witness linkat", errno);
            p.barrier(witnesses.get(), true);
            p.link_identity(witnesses.get(), name, e.target, ProjectionCause::MissingWitness);
            p.link_identity(parent.get(), leaf, e.target, ProjectionCause::MissingManaged);
            if (!same(identities[e.id], p.identity(witnesses.get(), name, false)) ||
                !same(p.identity(witnesses.get(), name, false), p.identity(parent.get(), leaf, false)))
                reject(LegacyCause::WitnessMismatch, "Adoption witness identity changed after barrier");
        }
        auto verified = check_witnesses(preparation.get(), true);
        p.barrier(witnesses.get(), true); p.barrier(preparation.get(), true);
        const auto final = expected(p, &state, id, current, sources);
        if (final != desired) reject(LegacyCause::HistoricalAuthority, "Source/generation changed before adoption publication");
        unchanged(); p.unchanged(preparations.get(), id.value(), preparation.get());
        p.unchanged(preparation.get(), "witnesses", witnesses.get());
        if (read_record(preparation.get()) != desired) reject(LegacyCause::PreparationConflict, "Preparation record substituted");
        p.unchanged(preparation.get(), "adoption.toml", file.get());
        if (p.io_.rename(preparations.get(), id.value().c_str(), root.get(), id.value().c_str(), RENAME_NOREPLACE) < 0)
            reject(LegacyCause::Io, "Adoption NOREPLACE publication", errno);
        publication_barriers();
        if (expected(p, &state, id, current, sources) != desired)
            reject(LegacyCause::HistoricalAuthority, "Source/generation changed after adoption publication");
        p.unchanged(root.get(), id.value(), preparation.get());
        p.unchanged(preparation.get(), "adoption.toml", file.get());
        p.unchanged(preparation.get(), "witnesses", witnesses.get());
        unchanged(); return ProjectionOutcome::MutatedDurably;
    } catch (LegacyError& e) { if (touched) e.outcome = ProjectionOutcome::OutcomeUncertain; throw; }
    catch (const ProjectionError& e) { throw LegacyError(e.cause == ProjectionCause::DurabilityFailure ? LegacyCause::Durability : LegacyCause::ProjectionConflict,
        touched ? ProjectionOutcome::OutcomeUncertain : e.outcome, e.error, e.what(), std::current_exception()); }
    catch (const d::PersistenceError& e) { throw LegacyError(LegacyCause::Durability,
        touched ? ProjectionOutcome::OutcomeUncertain : ProjectionOutcome::NotMutated, e.error, e.what(), std::current_exception()); }
    catch (const std::exception& e) { throw LegacyError(LegacyCause::PreparationConflict,
        touched ? ProjectionOutcome::OutcomeUncertain : ProjectionOutcome::NotMutated, 0, e.what(), std::current_exception()); }
}
ProjectionOutcome adopt_legacy_projection(ProjectionContext& p, const ProjectionState& state, const AdoptionId& id,
    const std::vector<LegacySource>& sources) { return LegacyAccess::adopt(p, state, id, sources); }
}
