// SPDX-License-Identifier: GPL-3.0-or-later
// Independent B4 consumer: persisted targets must equal the complete frozen delta.
#include <algorithm>
#define main b4_embedded_unit_main
#include "package_operation_test.cpp"
#undef main
namespace {
void unchanged(PackageFixture& f, const CurrentV1& before) {
    require(f.current() == before && !f.plan() && fs::is_empty(f.archive_path),
        "rejected target cannot publish retention, archive or selection");
}
void install_delta() {
    PackageFixture f; const auto before = f.current();
    auto in = request({package("app", {"dep"}), package("dep")});
    auto records = f.records(in); auto full = build_install_operation_target(f.generation(0), 1, in, records);
    auto missing = full;
    std::erase_if(missing.realizations, [](const auto& r) { return r.candidate.package.value() == "dep"; });
    f.put(missing); auto p = f.open();
    audited_rejected<SchemaError>([&] { prepare_install_operation(p, *f.state, f.input(), in); }, "Target lacks a resolved realization");
    unchanged(f, before);
    auto extra = f.records(request({package("extra")}, "extra"));
    full.realizations.push_back(extra.front()); f.put(full);
    audited_rejected<SchemaError>([&] { prepare_install_operation(p, *f.state, f.input(), in); }, "Persisted install target differs from complete dependency plan");
    unchanged(f, before);
    auto wrong = records; wrong.front().semantics->package.capabilities.push_back("unrequested-capability");
    audited_rejected<SchemaError>([&] { build_install_operation_target(f.generation(0), 1, in, wrong); }, "Realization semantics differ from frozen resolution/profile");
    unchanged(f, before);
    std::cout << "PASS independent install persisted closure/delta/semantic refusal\n";
}
void remove_delta() {
    PackageFixture f; auto in = request({package("app", {"dep"}), package("dep")});
    f.put(build_install_operation_target(f.generation(0), 1, in, f.records(in)));
    auto op = f.prepare_install(in); auto p = f.open(); auto archive = f.archive();
    persist_package_operation(p, *f.state, archive, op); activate_package_operation(p, *f.state, archive, op.reference());
    const auto before = f.current();
    f.put(generation::make_generation(2, {}, {}, {}));
    audited_rejected<SchemaError>([&] { prepare_remove_operation(p, *f.state, f.input(2, 21), {"app"}); }, "Persisted remove target differs from exact requested removal");
    require(f.current() == before && !f.plan(21) && !fs::exists(f.record(21)), "remove cannot silently autoremove dependency");
    auto invalid = f.generation(1); invalid.id = 2;
    std::erase_if(invalid.realizations, [](const auto& r) { return r.candidate.package.value() == "dep"; }); f.put(invalid);
    audited_rejected<installed::ValidationError>([&] { prepare_remove_operation(p, *f.state, f.input(2, 21), {"dep"}); }, "Unsatisfied dependency for package 'app': dep");
    require(f.current() == before && !f.plan(21), "reverse-dependency refusal before retention/selection");
    std::cout << "PASS independent remove exact delta/reverse-dependency refusal\n";
}
}
int main() {
    try { install_delta(); remove_delta(); return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
