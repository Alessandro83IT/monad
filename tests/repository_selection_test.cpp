// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include <athena/core/selection.hpp>

#include <cassert>
#include <iostream>

using namespace athena::core;

static RepositoryCandidate candidate(const char* repo, const char* version,
                                     std::vector<std::string> architectures = {})
{
    PackageMetadata p{PackageId("demo"), PackageVersion(version), SchemaVersion(1), "Demo"};
    p.architectures = std::move(architectures);
    return {p, RepositoryProvenance{RepositoryId(repo), {}, MetadataReference("meta")}};
}

static RepositorySnapshot snapshot(const char* repo, const char* revision,
                                   std::vector<RepositoryCandidate> candidates)
{
    return RepositorySnapshot::create(RepositoryId(repo), RepositoryRevision(revision), std::move(candidates));
}

int main()
{
    const auto old = snapshot("core", "1", {candidate("core", "1.9"), candidate("core", "1.10")});
    const auto arch = snapshot("arch", "a", {candidate("arch", "1:1.0-1")});
    RepositorySelectionRequest req{{PackageId("demo")}, {old},
        {{RepositoryId("core"), {VersionScheme::Athena, 0}}}, {"linux", "x86_64", "gnu", {}}};
    assert(select_candidates(req).selected[0].package.version.value() == "1.10");
    assert(select_candidates(req) == select_candidates(req));

    // Arch ordering, then priority over a same-version record from core.
    req.snapshots = {old, arch};
    req.repositories[RepositoryId("arch")] = {VersionScheme::Arch, 1};
    assert(select_candidates(req).selected[0].provenance.repository_id == RepositoryId("core"));
    req.snapshots = {snapshot("arch", "versions", {candidate("arch", "1:1.0-1"), candidate("arch", "1:1.0-2")})};
    assert(select_candidates(req).selected[0].package.version.value() == "1:1.0-2");

    // Vector order cannot change repository priority outcome.
    const auto equal_a = snapshot("a", "r1", {candidate("a", "2.0")});
    const auto equal_b = snapshot("b", "r2", {candidate("b", "2.0")});
    req.snapshots = {equal_a, equal_b};
    req.repositories = {{RepositoryId("a"), {VersionScheme::Athena, 9}},
                        {RepositoryId("b"), {VersionScheme::Athena, 1}}};
    const auto selected = select_candidates(req);
    req.snapshots = {equal_b, equal_a};
    assert(select_candidates(req) == selected);
    assert(selected.selected.front().provenance.repository_id == RepositoryId("b"));

    req.repositories[RepositoryId("a")].priority = 1;
    req.repositories[RepositoryId("b")].priority = 1;
    assert(select_candidates(req).selected.front().provenance.repository_id == RepositoryId("a"));

    // Identical repeated snapshot/candidate is collapsed deterministically.
    req.snapshots = {equal_a, equal_a};
    req.repositories = {{RepositoryId("a"), {VersionScheme::Athena, 0}}};
    assert(select_candidates(req).selected.size() == 1);

    bool rejected = false;
    auto changed_candidate = candidate("a", "2.0");
    changed_candidate.package.description = "different metadata";
    const auto conflicting = snapshot("a", "r2", {changed_candidate});
    req.snapshots = {equal_a, conflicting};
    rejected = false;
    try { (void)select_candidates(req); }
    catch (const SelectionError& e) { rejected = e.code() == SelectionErrorCode::ConflictingMetadata; }
    assert(rejected);

    rejected = false;
    req.repositories[RepositoryId("a")].version_scheme = VersionScheme::Unsupported;
    try { (void)select_candidates(req); }
    catch (const SelectionError& e) { rejected = e.code() == SelectionErrorCode::UnsupportedVersionScheme; }
    assert(rejected);

    req.repositories[RepositoryId("a")].version_scheme = VersionScheme::Athena;
    req.target.architecture = "arm64";
    req.snapshots = {snapshot("a", "target", {candidate("a", "2.0", {"x86_64"})})};
    rejected = false;
    try { (void)select_candidates(req); }
    catch (const SelectionError& e) { rejected = e.code() == SelectionErrorCode::IncompatibleTarget; }
    assert(rejected);
    req.target.architecture = "x86_64";

    req.package_ids = {PackageId("missing")};
    req.snapshots = {equal_a};
    rejected = false;
    try { (void)select_candidates(req); }
    catch (const SelectionError& e) { rejected = e.code() == SelectionErrorCode::NoEligibleCandidate; }
    assert(rejected);

    std::cout << "repository_selection_test: passed\n";
}
