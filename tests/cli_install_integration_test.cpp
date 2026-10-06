#include "cli_install_support.hpp"
using namespace cli_test;
int main() {
    StoreFixture fixture;
    fs::create_directory(fixture.root / "probe");
    try { (void)artifact::scan(fixture.root / "probe"); }
    catch (const artifact::Error& error) {
        if (error.code() != artifact::ErrorCode::InspectionFailure ||
            std::string(error.what()).find("hidden trusted.*") == std::string::npos) throw;
        std::cout << "SKIP CLI installation: " << error.what() << '\n'; return 77;
    }
    const auto paths = core::PathLayout::from_root(fixture.root);
    const auto archive = archive_fixture(fixture, "cli payload");
    manifest(paths, "app", archive, "[\"dependency\"]");
    manifest(paths, "dependency", archive);
    auto result = invoke(paths, {"app"});
    if (result.status != 0) throw std::runtime_error("CLI supported install failed: " + result.output);
    auto generations = manager(paths);
    const auto active = generations.current();
    require(active.entries.empty() && active.realizations.size() == 2,
            "resolver dependency selections missing from content-addressed membership");
    store::ObjectStore objects(paths.store);
    for (const auto& record : active.realizations) {
        require(record.candidate.repository == core::RepositoryId("local") &&
                record.candidate.snapshot.snapshot_id.has_value(), "public planner identity missing");
        objects.verify(record.artifact_id);
        require(!fs::exists(paths.store / (record.candidate.package.value() + "-1")),
                "legacy store entry created");
    }
    const auto payload = paths.target_root / "usr/share/fixture/payload";
    const auto link = fs::read_symlink(payload);
    require(link == objects.object_path(active.realizations.front().artifact_id) /
                    "usr/share/fixture/payload", "activation not from ObjectStore");
    require(read_bytes(payload) == "cli payload", "wrong activated payload");
    const auto count = generations.list().size();
    manifest(paths, "broken", archive, "[\"absent\"]");
    manifest(paths, "missing", fixture.root / "missing");
    manifest(paths, "unsupported", archive, "[]", "autotools");
    for (const auto& name : {"broken", "missing", "unsupported"}) {
        result = invoke(paths, {name});
        require(result.status != 0 && !result.output.empty(), "failure status/diagnostic missing");
        require(generation::serialize(generations.current()) == generation::serialize(active) && generations.list().size() == count &&
                fs::read_symlink(payload) == link && read_bytes(payload) == "cli payload",
                "failed CLI request changed active state");
        require(!fs::exists(paths.store / (std::string(name) + "-1")), "legacy fallback");
    }
}
