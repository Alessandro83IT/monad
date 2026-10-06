// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "executor.hpp"
#include "../installed/environment.hpp"
#include "../artifact/scanner.hpp"
#include "../artifact/identity.hpp"
#include "../build/builder.hpp"
#include "../hash/sha256.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <fstream>
#include <memory>
#include <set>
#include <unistd.h>

namespace athena::realization {
namespace {
namespace fs = std::filesystem;
using Code = ExecutionErrorCode;
void fail(Code code, const std::string& text) { throw ExecutionError(code, text); }
bool safe(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '/' || c == '.' || c == '_' || c == '-';
    });
}
template<class F> auto step(Code code, F operation) {
    try { return operation(); }
    catch (const ExecutionError&) { throw; }
    catch (const std::exception& e) {
        throw ExecutionError(code, e.what(), std::current_exception());
    }
}
// Deliberately narrower than legacy extraction: no links, special entries,
// extended permissions, traversal or shell-sensitive source path bytes.
fs::path extract_source(const fs::path& input, const fs::path& root) {
    fs::create_directory(root);
    std::unique_ptr<::archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
    if (!reader) fail(Code::Extraction, "Cannot allocate archive reader");
    archive_read_support_filter_gzip(reader.get());
    archive_read_support_format_tar(reader.get());
    auto check = [&](int status) {
        if (status != ARCHIVE_OK) fail(Code::Extraction,
            archive_error_string(reader.get()) ? archive_error_string(reader.get()) : "Invalid archive");
    };
    check(archive_read_open_filename(reader.get(), input.c_str(), 16384));
    std::set<std::string> tops, seen;
    archive_entry* entry;
    for (;;) {
        const int status = archive_read_next_header(reader.get(), &entry);
        if (status == ARCHIVE_EOF) break;
        check(status);
        if (archive_filter_code(reader.get(), 0) != ARCHIVE_FILTER_GZIP)
            fail(Code::Extraction, "Only gzip tar sources are supported");
        const char* name = archive_entry_pathname(entry);
        if (!name) fail(Code::Extraction, "Missing archive path");
        fs::path relative(name);
        if (!safe(name) || relative.is_absolute()) fail(Code::Extraction, "Unsupported archive path");
        for (const auto& component : relative)
            if (component == "..") fail(Code::Extraction, "Archive traversal rejected");
        relative = relative.lexically_normal();
        // lexically_normal retains a trailing separator (including from '/.').
        // Use one spelling for the same filesystem entry before duplicate checks.
        while (!relative.empty() && relative.filename().empty())
            relative = relative.parent_path();
        if (relative.empty() || relative == ".") fail(Code::Extraction, "Invalid archive root entry");
        tops.insert(relative.begin()->string());
        if (tops.size() != 1) fail(Code::Extraction, "Expected exactly one source directory");
        const auto type = archive_entry_filetype(entry);
        if (archive_entry_symlink(entry) || archive_entry_hardlink(entry) ||
            (type != AE_IFREG && type != AE_IFDIR) || (archive_entry_perm(entry) & 07000))
            fail(Code::Extraction, "Unsupported source entry type or permissions");
        if (!seen.insert(relative.generic_string()).second) fail(Code::Extraction, "Duplicate source entry");
        const auto output = root / relative;
        if (type == AE_IFDIR) { fs::create_directories(output); continue; }
        fs::create_directories(output.parent_path());
        std::ofstream stream(output, std::ios::binary);
        char buffer[16384];
        for (;;) {
            const auto count = archive_read_data(reader.get(), buffer, sizeof(buffer));
            if (count < 0) fail(Code::Extraction, "Cannot read source entry");
            if (count == 0) break;
            stream.write(buffer, count);
        }
        stream.close();
        if (!stream) fail(Code::Extraction, "Cannot write source entry");
        fs::permissions(output, (archive_entry_perm(entry) & 0111) ?
            fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec :
            fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read);
    }
    check(archive_read_close(reader.get()));
    if (tops.size() != 1 || !fs::is_directory(root / *tops.begin()))
        fail(Code::Extraction, "Expected one top-level source directory");
    return root / *tops.begin();
}
void supported(const core::RepositoryCandidate& candidate) {
    if (!candidate.source_recipe) fail(Code::UnsupportedKind, "C.4 requires a SourceRecipe; binary/artifact realization is unsupported");
    const auto& recipe = *candidate.source_recipe;
    if (recipe.sources.size() != 1) fail(Code::UnsupportedRecipe, "C.4 requires exactly one source");
    if (recipe.build_ir_reference) fail(Code::UnsupportedKind, "Athena IR execution is unsupported");
    if (!recipe.build_parameters.empty()) fail(Code::UnsupportedRecipe, "Build parameters are unsupported");
    if (recipe.build_backend != "cmake") fail(Code::UnsupportedRecipe, "C.4 supports only the dedicated-directory CMake backend");
    if (recipe.sources[0].digest &&
        (!recipe.sources[0].digest->value().starts_with("sha256:") || recipe.sources[0].digest->value().size() != 71))
        fail(Code::UnsupportedRecipe, "C.4 supports only full SHA-256 transport digests");
    const auto& reference = recipe.sources[0].reference.value();
    if (!reference.starts_with("file:///") || reference.find_first_of("%?#") != std::string::npos || reference.find('\0') != std::string::npos)
        fail(Code::UnsupportedRecipe, "C.4 supports only literal local file:/// URIs; repository keys and other transports are unresolved");
}
}
std::vector<RealizationRecord> realize(const core::InstallPlan& plan,
    const store::ObjectStore& objects, const ExecutionOptions& options) {
    step(Code::InvalidInput, [&] { plan.validate(); });
    std::vector<RealizationRecord> records;
    if (plan.selections().empty()) return records;
    // Validate all semantic state before any acquisition/build/publication.
    (void)installed::prepare_install(generation::Generation{0, {}}, plan);
    step(Code::InvalidInput, [&] {
        if (!options.workspace_root.is_absolute() || !safe(options.workspace_root.native()) ||
            !fs::is_directory(options.workspace_root)) fail(Code::InvalidInput, "Workspace must be an existing safe absolute directory");
        // Do not accept symlink components in the execution locator.
        fs::path prefix;
        for (const auto& component : options.workspace_root) {
            if (component == "..") fail(Code::InvalidInput, "Workspace traversal rejected");
            prefix /= component;
            if (fs::is_symlink(fs::symlink_status(prefix))) fail(Code::InvalidInput, "Workspace symlink rejected");
        }
    });
    for (const auto& selection : plan.selections()) {
        supported(selection.candidate);
        const auto& recipe = *selection.candidate.source_recipe;
        std::string pattern = (options.workspace_root / "realize-XXXXXX").native();
        if (!::mkdtemp(pattern.data())) fail(Code::Acquisition, "Cannot create private execution directory");
        const fs::path work(pattern);
        auto cleanup = [&] { std::error_code error; fs::remove_all(work, error); return error; };
        try {
            const auto input = work / "source.tar.gz";
            step(Code::Acquisition, [&] {
                const fs::path source(recipe.sources[0].reference.value().substr(7));
                if (!fs::is_regular_file(fs::symlink_status(source))) fail(Code::Acquisition, "Source is not a regular local file");
                fs::copy_file(source, input);
            });
            step(Code::SourceVerification, [&] {
                const auto& source = recipe.sources[0];
                if (source.size_bytes && fs::file_size(input) != *source.size_bytes) fail(Code::SourceVerification, "Source size mismatch");
                if (source.digest) {
                    std::ifstream stream(input, std::ios::binary);
                    const std::string bytes{std::istreambuf_iterator<char>(stream), {}};
                    if (!stream || stream.bad()) fail(Code::SourceVerification, "Cannot read source digest input");
                    if (hash::sha256(bytes) != source.digest->value()) fail(Code::SourceVerification, "Source digest mismatch");
                }
            });
            const auto source = step(Code::Extraction, [&] { return extract_source(input, work / "source"); });
            step(Code::Build, [&] {
                build::build(source, work / "build", recipe.build_backend);
                build::install(source, work / "build", work / "staging", recipe.build_backend);
            });
            const auto id = step(Code::ArtifactIdentity, [&] { return artifact::derive_artifact_id(artifact::scan(work / "staging")); });
            step(Code::Publication, [&] { objects.publish(work / "staging", id); });
            RealizationRecord record{selection.identity(), id, std::nullopt, core::ArtifactOrigin::AmbientSourceBuild,
                installed::from_selection(plan, selection)};
            step(Code::InvalidInput, [&] { record.validate(); });
            records.push_back(std::move(record));
        } catch (ExecutionError& error) {
            if (auto problem = cleanup()) error.record_cleanup_failure(problem.message());
            throw;
        } catch (...) { cleanup(); throw; }
        if (auto error = cleanup()) fail(Code::Cleanup, error.message());
    }
    return records;
}
}
