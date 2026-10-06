#include "realization_executor_support.hpp"
#include "install/transaction.hpp"
using namespace execution_test;
int main() {
    StoreFixture fixture;
    fs::create_directory(fixture.root / "objects");
    store::ObjectStore objects(fixture.root / "objects");
    generation::GenerationManager generations(fixture.root / "state");
    const auto target = fixture.root / "target";
    const realization::ExecutionOptions execution{fixture.root / "missing-workspace"};
    const auto empty = plan({});
    install::install_plan(empty, generations, objects, execution, target);
    require(!fs::exists(fixture.root / "state") && !fs::exists(target), "empty transaction mutated state");
    fs::create_directory(fixture.root / "workspace");
    const realization::ExecutionOptions valid_execution{fixture.root / "workspace"};
    auto unsupported = plan({recipe(fixture.root / "missing")}, false);
    bool uninitialized = false;
    try { install::install_plan(unsupported, generations, objects, valid_execution, target); }
    catch (const std::runtime_error&) { uninitialized = true; }
    require(uninitialized && !fs::exists(fixture.root / "state"), "uninitialized transaction created state");
    generations.initialize();
    const auto before = generations.current();
    auto bad = plan({recipe(fixture.root / "missing")}, false);
    const auto original = bad.result();
    rejects(realization::ExecutionErrorCode::InvalidInput, [&] {
        install::install_plan(bad, generations, objects, execution, target);
    });
    require(bad.result() == original, "plan mutated");
    require(generations.current().id == before.id && generations.list().size() == 1, "failure changed generation");
    require(!fs::exists(target), "failure changed target");
    rejects(realization::ExecutionErrorCode::UnsupportedKind, [&] {
        install::install_plan(unsupported, generations, objects, valid_execution, target);
    });
    require(generations.current().id == before.id && generations.list().size() == 1,
            "unsupported realization changed generations");
    install::install_plan(empty, generations, objects, execution, target);
    require(generations.list().size() == 1, "empty created generation");
}
