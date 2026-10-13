// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "activation/selection.hpp"
#include "activation_schema_support.hpp"
#include "store/transaction_pins.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
#include <unistd.h>
#include <cstdlib>
#include <vector>
namespace execution_test {
using namespace athena::activation;
namespace fs = std::filesystem;
using d1_test::require;
ActivationId id(unsigned n) { char bytes[33]; std::snprintf(bytes,sizeof(bytes),"%032x",n); return ActivationId(bytes); }
void write(const fs::path& path, std::string_view bytes) { std::ofstream out(path); out << bytes; require(bool(out),"fixture write"); }
struct Fixture {
    fs::path root;
    ProjectionBinding binding;
    athena::store::ObjectStore objects;
    std::unique_ptr<ProjectionState> state;
    Fixture() : root([] { const auto parent=::getenv("ATHENA_EXECUTION_TEST_TMPDIR"); std::string pattern=std::string(parent?parent:"/tmp")+"/athena-d6-XXXXXX"; std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0); auto p=::mkdtemp(name.data()); require(p,"mkdtemp"); return fs::path(p); }()),
        binding{d1_test::projection_id,(root/"generations").native(),(root/"store").native(),(root/"projection").native()}, objects(root/"store") {
        for (auto name : {"generations","store","projection"}) fs::create_directory(root/name);
        state = std::make_unique<ProjectionState>(binding.generation_authority);
        auto context = ProjectionContext::create_binding(binding,objects,*state);
        for (unsigned n=0;n<3;++n) { fs::create_directory(root/"generations"/std::to_string(n));
            fs::permissions(root/"generations"/std::to_string(n),fs::perms::owner_all);
            write(root/"generations"/std::to_string(n)/"generation.toml",athena::generation::serialize(athena::generation::make_generation(n,{})));
            fs::permissions(root/"generations"/std::to_string(n)/"generation.toml",fs::perms::owner_read|fs::perms::owner_write); }
    }
    ~Fixture() { state.reset(); fs::remove_all(root); }
    auto open(ProjectionIo io={}) { return ProjectionContext::open(binding,objects,std::move(io)); }
    auto digest(unsigned n) { return athena::generation::read_generation_document(binding.generation_authority,n).digest; }
    PlanIdentity plan(unsigned activation,unsigned generation,ActivationBase base) {
        auto p=open(); ActivationIntent intent{id(activation),binding.generation_authority,binding.store_authority,binding.projection_root,binding.projection_id,generation,digest(generation),std::move(base)};
        ActivationPlan plan{id(activation),{}, {}};
        return {id(activation),publish_activation_plan(p,*state,intent,plan).document.digest};
    }
    SelectedBase base(unsigned activation,unsigned generation,const PlanIdentity& plan) {
        return {CurrentV1{generation,id(activation),plan.digest},digest(generation),ActivationOwnership{id(activation)}};
    }
    PlanIdentity bootstrap_source_authority() {
        // A projection-bound namespace must select generation 0 through D6
        // before TransactionPins can bind the store/generation authorities.
        auto zero=plan(1,0,AbsentBase{});
        { auto p=open(); commit_selection(p,*state,zero); }
        // TransactionPins acquires State itself, before StoreGc and its lease.
        state.reset();
        { athena::store::TransactionPins pins({binding.store_authority,binding.generation_authority}); pins.abandon(); }
        state=std::make_unique<ProjectionState>(binding.generation_authority);
        return zero;
    }
};
template<class F> void rejected(F fn) { try { fn(); } catch(const std::exception&) { return; } throw std::runtime_error("Expected rejection"); }
}
