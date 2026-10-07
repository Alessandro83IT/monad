// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#include "realization_executor_support.hpp"
#include <iostream>
using namespace execution_test;
int main() {
    StoreFixture fixture; setup(fixture);
    store::ObjectStore objects(fixture.root / "objects");
    realization::ExecutionOptions options{fixture.root / "workspace"};
    // Only this integration executable skips; rejection/cleanup tests still run.
    fs::create_directory(fixture.root / "probe");
    try { (void)artifact::scan(fixture.root / "probe"); }
    catch (const artifact::Error& e) {
        if (e.code() != artifact::ErrorCode::InspectionFailure || std::string(e.what()).find("hidden trusted.*") == std::string::npos) throw;
        std::cout << "SKIP realization publication: " << e.what() << '\n'; return 77;
    }
    store::TransactionPins transaction({objects.root(), fixture.root / "realization-authority"});
    options.transaction = &transaction;
    auto archive = archive_fixture(fixture, "one");
    auto r = recipe(archive);
    const auto digest = hash::sha256(read_bytes(archive));
    r.sources[0].digest = core::ContentDigest(digest);
    r.build_inputs.push_back(core::BuildInputId("repository-claimed-input"));
    auto p = plan({r});
    const auto original = p.result();
    auto records = realization::realize(p, objects, options);
    require(records.size() == 1 && records[0].candidate == p.selections()[0].identity(), "selection identity");
    require(records[0].origin == core::ArtifactOrigin::AmbientSourceBuild, "ambient origin");
    require(!records[0].build_input_id, "ambient build input fabricated");
    require(p.result() == original, "plan mutated");
    fs::create_directories(fixture.root / "expected/usr/share/fixture");
    std::ofstream(fixture.root / "expected/usr/share/fixture/payload") << "one";
    const auto expected = artifact::derive_artifact_id(artifact::scan(fixture.root / "expected"));
    require(records[0].artifact_id == expected, "identity differs from staged output");
    require(records[0].artifact_id.value() != digest, "transport digest confused with artifact");
    require(fs::is_directory(objects.object_path(expected)), "object absent"); objects.verify(expected);
    boundary(fixture);
    archive_fixture(fixture, "two");
    r.sources[0].digest.reset();
    auto changed = realization::realize(plan({r}), objects, options);
    require(changed[0].artifact_id != expected, "changed output identity unchanged");
    objects.verify(changed[0].artifact_id);
    auto second = r; second.package_id = core::PackageId("another");
    auto multi = plan({r, second});
    auto ordered = realization::realize(multi, objects, options);
    for (std::size_t i = 0; i < ordered.size(); ++i)
        require(ordered[i].candidate == multi.selections()[i].identity(), "selection order changed");
    auto failing = r; failing.package_id = core::PackageId("zzz"); failing.sources[0].reference = core::MetadataReference("file:///missing");
    rejects(realization::ExecutionErrorCode::Acquisition, [&] { realization::realize(plan({r, failing}), objects, options); });
    objects.verify(changed[0].artifact_id); boundary(fixture);
    // Publication diagnostics preserve ObjectStore's typed original error.
    fs::create_directory(fixture.root / "broken-store");
    fs::create_directory(fixture.root / "broken-store/objects");
    std::ofstream(fixture.root / "broken-store/objects/v1") << "obstruction";
    store::ObjectStore broken(fixture.root / "broken-store");
    try { realization::realize(plan({r}), broken, options); throw std::runtime_error("expected publication failure"); }
    catch (const realization::ExecutionError& error) {
        require(error.code() == realization::ExecutionErrorCode::Publication && error.cause(), "publication category lost");
        try { std::rethrow_exception(error.cause()); } catch (const store::ObjectError&) {}
    }
    boundary(fixture);
}
