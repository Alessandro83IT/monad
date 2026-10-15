// SPDX-License-Identifier: GPL-3.0-or-later
#include "bounded_toml.hpp"
#include "../activation/identity.hpp"
#include <athena/core/types.hpp>
#include <limits>
#include <unordered_map>
#include <algorithm>
#include <fstream>
#include <array>
namespace athena::parsing {
namespace limits = athena::activation;
Budget Budget::compatible(std::size_t n) {
 if(n > (std::numeric_limits<std::size_t>::max()-1)/8)
  throw Error(Rejection::Budget,"TOML budget arithmetic overflow");
 return {n,n+1,n,n+1,n,8*n+1};
}
namespace {
enum class S { Any,Intent,Base,Plan,Source,Operation,Status,Current,Binding,Adoption,AdoptionEntry,
 Generation,LegacyMember,Member,Record,Candidate,Snapshot,Provenance,Semantics,Environment,Provider,
 Policy,Package,Features,FeatureDependencies,Dependency,Manifest,ManifestEntry,LegacyMetadata,Provides,StoreBinding,Pins };
enum class T { Any,Int,Str,Bool,Table,Array };
struct Rule { T type=T::Any; S schema=S::Any; T element=T::Any; std::size_t limit=0; const char* diagnostic=nullptr; bool identity=false; };
Rule integer(){return {T::Int};} Rule string(){return {T::Str};} Rule boolean(){return {T::Bool};}
Rule table(S s){return {T::Table,s};}
Rule array(S s,T t,std::size_t max=0){return {T::Array,s,t,max};}
bool named(std::string_view names,std::string_view key) {
 while(!names.empty()) {
  if(names.front()=='|')names.remove_prefix(1);
  const auto end=names.find('|');
  if(names.substr(0,end)==key)return true;
  if(end==names.npos)break;
  names.remove_prefix(end+1);
 }
 return false;
}
Rule field_shape(S s,std::string_view k) {
 // Mirrors structural key/type contracts only. Identity, ownership, versions,
 // digests, enum values, and conditional requirements remain existing validators.
#define STR(names) if(named(names,k)) return string()
#define INT(names) if(named(names,k)) return integer()
#define BOOL(names) if(named(names,k)) return boolean()
 switch(s) {
 case S::Any:return {};
 case S::Intent:
  STR("|activation_id|generation_authority_hex|store_authority_hex|projection_root_hex|projection_id|target_generation_digest|"); INT("|schema_version|target_generation|"); if(k=="base")return table(S::Base); break;
 case S::Base:STR("|format|record_hex|generation_digest|ownership|"); INT("|generation|");break;
 case S::Plan:STR("|activation_id|");INT("|schema_version|");if(k=="sources")return array(S::Source,T::Table,limits::max_sources);if(k=="operations")return array(S::Operation,T::Table,limits::max_operations);break;
 case S::Source:STR("|kind|artifact_id|locator_hex|alias_target_hex|seal_digest|");break;
 case S::Operation:STR("|kind|path_hex|old_witness|new_witness|");INT("|id|old_source|new_source|");break;
 case S::Status:STR("|activation_id|plan_digest|outcome|");INT("|schema_version|");break;
 case S::Current:STR("|activation_id|plan_digest|");INT("|schema_version|generation|");break;
 case S::Binding:STR("|projection_id|generation_authority_hex|store_authority_hex|projection_root_hex|");INT("|schema_version|");break;
 case S::Adoption:STR("|adoption_id|projection_id|generation_authority_hex|store_authority_hex|projection_root_hex|historical_current_hex|generation_digest|");INT("|schema_version|generation|");if(k=="sources")return array(S::Source,T::Table,limits::max_sources);if(k=="entries")return array(S::AdoptionEntry,T::Table,limits::max_operations);if(k=="directories_hex")return array(S::Any,T::Str,limits::max_operations);break;
 case S::AdoptionEntry:INT("|id|source|");STR("|path_hex|target_hex|witness|");break;
 case S::Generation:INT("|id|schema_version|");if(k=="entries")return array(S::LegacyMember,T::Table);if(k=="memberships")return array(S::Member,T::Table);if(k=="policies")return array(S::Policy,T::Table);break;
 case S::LegacyMember:STR("|package|store|");break;
 case S::Member:STR("|kind|package|store|");if(k=="record")return table(S::Record);if(k=="semantics")return table(S::Semantics);break;
 case S::Record:STR("|artifact_id|build_input_id|origin|");INT("|schema_version|");if(k=="candidate")return table(S::Candidate);if(k=="semantics")return table(S::Semantics);break;
 case S::Candidate:STR("|package|version|repository|");if(k=="snapshot")return table(S::Snapshot);if(k=="provenance")return table(S::Provenance);break;
 case S::Snapshot:STR("|repository_id|revision|metadata_digest|snapshot_id|");INT("|schema_version|");break;
 case S::Provenance:STR("|repository_id|revision|metadata_reference|");if(k=="snapshot")return table(S::Snapshot);break;
 case S::Semantics:
  INT("|schema_version|version_scheme|");STR("|profile|");BOOL("|optional_enabled|propagated_enabled|legacy_restricted|");
  if(k=="package")return table(S::Package);if(k=="target"||k=="host"||k=="build")return table(S::Environment);
  if(k=="features")return table(S::Features);if(k=="bindings")return array(S::Provider,T::Table);
  if(k=="active_roles")return array(S::Any,T::Int,6);break;
 case S::Features:return boolean();
 case S::Environment:STR("|platform|architecture|abi|triple|");break;
 case S::Provider:STR("|selector|");INT("|role|context|");if(k=="provider")return table(S::Candidate);break;
 case S::Policy:INT("|schema_version|");STR("|policy|selector|");if(k=="target")return table(S::Environment);break;
 case S::Package:INT("|schema_version|");STR("|package_id|version|display_name|description|");
  if(k=="feature_dependencies")return table(S::FeatureDependencies);
  if(k=="dependencies"||k=="capabilities"||k=="features"||k=="conflicts"||k=="platforms"||k=="architectures"||k=="abis")return array(S::Any,T::Str);break;
 case S::FeatureDependencies:return array(S::Dependency,T::Table);
 case S::Dependency:STR("|expression|role|context|");BOOL("|enabled|");break;
 case S::Manifest:if(k=="files")return array(S::ManifestEntry,T::Table,limits::max_operations);break;
 case S::ManifestEntry:STR("|path|");break;
 case S::LegacyMetadata:STR("|name|version|source|sha256|build_system|");if(k=="dependencies")return array(S::Any,T::Str);if(k=="provides")return table(S::Provides);break;
 case S::Provides:if(k=="features"||k=="capabilities")return array(S::Any,T::Str);break;
 case S::StoreBinding:INT("|schema_version|");STR("|store_authority|generation_authority|");break;
 case S::Pins:INT("|schema_version|");STR("|transaction_id|");if(k=="artifacts")return array(S::Any,T::Str);break;
 }
#undef STR
#undef INT
#undef BOOL
 throw Error(Rejection::Structure,"Unknown TOML field before value construction");
}
Rule field(S s,std::string_view k) {
 auto rule=field_shape(s,k);
 if(s==S::Dependency&&k=="role")rule.diagnostic="Invalid feature dependency role";
 if(rule.type==T::Str) {
  if((s==S::Intent||s==S::Plan||s==S::Current||s==S::Status)&&k=="activation_id")rule.limit=limits::ActivationId::encoded_size;
  if((s==S::Intent||s==S::Binding||s==S::Adoption)&&k=="projection_id")rule.limit=limits::ProjectionId::encoded_size;
  if(s==S::Adoption&&k=="adoption_id")rule.limit=limits::AdoptionId::encoded_size;
  if((s==S::Source&&(k=="artifact_id"||k=="seal_digest"))||
     (s==S::Record&&(k=="artifact_id"||k=="build_input_id"))||
     k=="plan_digest"||k=="target_generation_digest"||k=="generation_digest")rule.limit=limits::PlanDigest::encoded_size;
  if(rule.limit==limits::ActivationId::encoded_size||rule.limit==limits::PlanDigest::encoded_size)rule.identity=true;
  if(k=="generation_authority_hex"||k=="store_authority_hex"||k=="projection_root_hex"||
     k=="path_hex"||k=="target_hex"||k=="locator_hex"||k=="alias_target_hex")rule.limit=2*limits::max_path_size;
  if(k=="record_hex"||k=="historical_current_hex")rule.limit=2*limits::max_current_size;
 }
 return rule;
}

S schema(Profile p) {
 switch(p){
#define P(x) case Profile::x:return S::x
 P(Any);P(Intent);P(Plan);P(Status);P(Current);P(Adoption);P(Generation);P(Record);P(Semantics);P(Policy);P(Package);P(Manifest);P(LegacyMetadata);P(StoreBinding);P(Pins);
 case Profile::ProjectionBinding:return S::Binding;
#undef P
 } throw Error(Rejection::Structure,"Unknown TOML profile");
}
struct Context { Rule rule; std::size_t depth; };
class Guard final : public athena_toml::parse_observer {
 Budget budget_; Context active_, key_;
 std::unordered_map<const athena_toml::node*,Context> contexts_;
 static void charge(std::size_t& value,std::size_t amount,std::size_t max) {
  if(value>max||amount>max-value) throw Error(Rejection::Budget,"TOML resource budget exceeded before allocation");
  value+=amount;
 }
 Context context(const athena_toml::node& n) const {
  const auto at=contexts_.find(&n);if(at==contexts_.end())throw Error(Rejection::Structure,"Missing parser authorization context");return at->second;
 }
 static bool matches(T t,athena_toml::node_type kind) {
  return t==T::Any||(t==T::Int&&kind==athena_toml::node_type::integer)||(t==T::Str&&kind==athena_toml::node_type::string)||
   (t==T::Bool&&kind==athena_toml::node_type::boolean)||(t==T::Table&&kind==athena_toml::node_type::table)||(t==T::Array&&kind==athena_toml::node_type::array);
 }
 [[noreturn]] void type_failure() const {
   if(active_.rule.schema==S::FeatureDependencies&&active_.rule.type==T::Table)
    throw Error(Rejection::Structure,"Feature dependencies must be a table");
   if(active_.rule.schema==S::Dependency&&active_.rule.type==T::Array)
    throw Error(Rejection::Structure,"Feature dependencies must be arrays");
   throw Error(Rejection::Structure,active_.rule.diagnostic?active_.rule.diagnostic:"Unexpected TOML value type before node allocation");

 }

public:
 Stats stats;
 Guard(Profile p,Budget b):budget_(b),active_{table(schema(p)),0},key_(active_){}
 void root(const athena_toml::table& t) override { node(athena_toml::node_type::table);attach(t); }
 void key_start(const athena_toml::table& t) override { key_=context(t); }
 void key_segment(std::string_view segment,bool more) override {
  const auto next=field(key_.rule.schema,segment);
  key_storage(segment.size());
  if(more) {
   if(next.type!=T::Table&&next.type!=T::Array&&next.type!=T::Any)throw Error(Rejection::Structure,"Scalar TOML key has descendants");
   key_={table(next.schema),key_.depth+1};depth(key_.depth);
  }
 }
 void key(const athena_toml::table& t,std::string_view k) override {auto c=context(t);active_={field(c.rule.schema,k),c.depth+1};depth(active_.depth);}
 void table_child(const athena_toml::table& t,std::string_view k,bool arr) override {
  key(t,k);if(!matches(active_.rule.type,arr?athena_toml::node_type::array:athena_toml::node_type::table))type_failure();
 }
 void node(athena_toml::node_type type) override {
  if(!matches(active_.rule.type,type))type_failure();
  charge(stats.nodes,1,budget_.nodes);
 }
 void attach(const athena_toml::node& n) override {
  if(n.is_table()||n.is_array())contexts_.emplace(&n,active_); // one entry per authorized container
 }
 void element(const athena_toml::array& a) override {
  const auto c=context(a);
  const auto bound=c.rule.limit?std::min(c.rule.limit,budget_.array_elements):budget_.array_elements;
  if(a.size()>=bound)throw Error(Rejection::Budget,"TOML array limit before element construction");
  if(!a.empty()&&a.back().is_table())complete(a.back().ref<athena_toml::table>());
  charge(stats.array_elements,1,budget_.array_elements);
  active_={{c.rule.element,c.rule.schema},c.depth+1};depth(active_.depth);
 }
 void complete(const athena_toml::table& t) override {
  // Necessary requirements only; never reject a valid table. In table arrays
  // only the latest element can receive fields: closing its predecessor here
  // stops repeated empty-table attacks before allocating the next element.
  const auto s=context(t).rule.schema;
  auto require=[&](std::string_view k){if(!t.contains(k))throw Error(Rejection::Structure,"Incomplete TOML table before next element");};
  switch(s) {
   case S::LegacyMember:require("package");require("store");break;
   case S::Member:
    require("kind");
    if(t["kind"].value<std::string_view>()=="legacy"){require("package");require("store");}
    else if(t["kind"].value<std::string_view>()=="realization")require("record");
    break;
   case S::Source:require("kind");break;
   case S::Operation:require("id");require("kind");require("path_hex");break;
   case S::ManifestEntry:require("path");break;
   case S::Policy:require("schema_version");require("policy");require("selector");require("target");break;
   case S::Provider:require("selector");require("provider");require("role");require("context");break;
   case S::Dependency:require("expression");break;
   case S::Environment:require("platform");require("architecture");require("abi");require("triple");break;
   case S::Record:require("schema_version");require("artifact_id");require("origin");require("candidate");break;
   case S::Candidate:require("package");require("version");require("repository");require("snapshot");require("provenance");break;
   case S::Snapshot:require("schema_version");require("repository_id");require("revision");break;
   case S::Provenance:require("repository_id");break;
   case S::Package:require("schema_version");require("package_id");require("version");break;
   case S::Semantics:require("schema_version");require("package");require("target");require("host");require("build");require("features");require("bindings");require("active_roles");break;
   default:break;
  }
 }
 void text(std::size_t size,bool key) override {
  const bool dynamic_key=key_.rule.schema==S::Any||key_.rule.schema==S::Features||key_.rule.schema==S::FeatureDependencies;
  if(key&&!dynamic_key&&size>32)throw Error(Rejection::Structure,"Unknown oversized TOML key before buffer growth");
  if(size>budget_.token_bytes)throw Error(Rejection::Budget,"TOML token bound before buffer growth");
  if(!key&&active_.rule.type==T::Str&&active_.rule.limit&&size>active_.rule.limit)
   throw Error(active_.rule.identity?Rejection::Identity:Rejection::Budget,"TOML field bound before buffer growth");
  if(!key&&active_.rule.type!=T::Str&&active_.rule.type!=T::Any)
   type_failure();
  charge(stats.text_work,1,budget_.text_work);
 }
 void key_storage(std::size_t n) override {charge(stats.text_work,n,budget_.text_work);}
 void depth(std::size_t n) override {if(n>budget_.depth)throw Error(Rejection::Budget,"TOML depth before descent");stats.max_depth=std::max(stats.max_depth,n);}
};
}
athena_toml::table parse(std::string_view input,Profile profile,const Budget& budget,Stats* observed) {
 if(observed)*observed={};
 if(input.size()>budget.input_bytes)throw Error(Rejection::Budget,"TOML input byte bound before parsing");
 Guard guard(profile,budget);
 try {auto result=athena_toml::parse(input,guard);if(observed)*observed=guard.stats;return result;}
 catch(...){if(observed)*observed=guard.stats;throw;}
}
athena_toml::table parse_model(std::string_view input, Profile profile) {
 try { return parse(input,profile); }
 catch(const Error& e) { throw core::TypeError(e.what()); }
}

athena_toml::table parse_file(const std::filesystem::path& path, Profile profile, std::size_t max_input_bytes) {
 // Same file-reading authority as the old parse_file. This is not inode,
 // ownership or mount authorization: D8's existing checks remain mandatory.
 std::ifstream stream(path,std::ios::binary|std::ios::ate);
 if(!stream)throw Error(Rejection::Structure,"Cannot open TOML document");
 return detail::parse_stream(stream,profile,max_input_bytes);
}

athena_toml::table detail::parse_stream(std::istream& stream, Profile profile, std::size_t max_input_bytes) {
 stream.seekg(0,std::ios::end);
 const auto length=stream.tellg();
 if(length<0||static_cast<std::uintmax_t>(length)>max_input_bytes)
  throw Error(Rejection::Budget,"TOML file bound before input allocation");
 const auto size=static_cast<std::size_t>(length);
 const auto budget=Budget::compatible(size);
 stream.seekg(0);
 std::string bytes;
 std::array<char,4096> buffer;
 while(stream) {
  stream.read(buffer.data(),buffer.size());
  const auto n=static_cast<std::size_t>(stream.gcount());
  if(n>budget.input_bytes-bytes.size())throw Error(Rejection::Budget,"TOML file changed beyond authorized bytes");
  bytes.append(buffer.data(),n);
 }
 if(!stream.eof()||bytes.size()!=size)throw Error(Rejection::Structure,"Incomplete TOML document read");
 return parse_model(bytes,profile);
}

}
