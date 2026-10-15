// SPDX-License-Identifier: GPL-3.0-or-later
#include "parsing/bounded_toml.hpp"
#include "activation_schema_support.hpp"
#include "activation/operational_request_codec.hpp"
#include "installed/semantics.hpp"
#include <array>
#include <limits>
using namespace athena::parsing;
using d1_test::require;
namespace {
void rejected(std::string_view input, Profile profile, Budget budget, Rejection reason, Stats& stats) {
 try { parse(input,profile,budget,&stats); }
 catch(const Error& e){require(e.reason==reason,"expected preallocation rejection class");return;}
 throw std::runtime_error("unprotected TOML accepted");
}
void tests() {
 for(unsigned count:{32,4096}) {
  std::string input="unexpected=[";
  for(unsigned i=0;i<count;++i){if(i)input+=',';input+='0';}input+=']';
  for(auto profile:{Profile::Intent,Profile::Plan,Profile::Generation,Profile::Record,Profile::Semantics,Profile::Policy,Profile::Package}) {
   Stats stats;rejected(input,profile,Budget::compatible(input.size()),Rejection::Structure,stats);
   require(stats.nodes==1&&stats.array_elements==0,"unknown key rejected before payload AST nodes");
  }
 }
 // Duplicate keys fail in the original parser before the second value body.
 for(auto input:{"id=2\nid=[0,0,0]", "id=2\n\"\\u0069d\"=[0,0,0]"}) {
  Stats duplicate;
  try {parse(input,Profile::Generation,Budget::compatible(100),&duplicate);throw std::runtime_error("duplicate key accepted");}
  catch(const athena_toml::parse_error&){require(duplicate.nodes==2&&duplicate.array_elements==0,"duplicate payload not materialized");}
 }
 for(auto input:{"id=2\nentries=[{package='p',store='s'", "id=2\nentries=[", "id=2\n# comment\r\nentries=[{package=\"\\uZZZZ\",store='s'}]"}) {
  try {parse(input,Profile::Generation);throw std::runtime_error("malformed input accepted");}
  catch(const athena_toml::parse_error&){}
 }
 const std::string simple="schema_version=1\nactivation_id='0123456789abcdef0123456789abcdef'\nsources=[]\noperations=[]\n";
 Stats counted;auto normal=parse(simple,Profile::Plan,Budget::compatible(simple.size()),&counted);
 auto budget=Budget::compatible(simple.size());budget.nodes=counted.nodes;
 require(parse(simple,Profile::Plan,budget)==normal,"inclusive node bound");
 --budget.nodes;Stats stats;rejected(simple,Profile::Plan,budget,Rejection::Budget,stats);
 require(stats.nodes==budget.nodes,"no node beyond budget constructed");
 const std::string a="x=[1,2,3]";
 auto tight=Budget::compatible(a.size());tight.array_elements=3;
 require(parse(a,Profile::Any,tight)==athena_toml::parse(a),"inclusive array bound");
 tight.array_elements=2;rejected(a,Profile::Any,tight,Rejection::Budget,stats);
 require(stats.array_elements==2&&stats.nodes==4,"third array value not allocated");
 auto text=Budget::compatible(20);text.token_bytes=3;
 require(parse("x='abc'",Profile::Any,text)["x"].value<std::string>()=="abc","inclusive token byte bound");
 rejected("x='abcd'",Profile::Any,text,Rejection::Budget,stats);
 auto depth=Budget::compatible(30);depth.depth=2;
 rejected("x=[[[1]]]",Profile::Any,depth,Rejection::Budget,stats);
 require(stats.max_depth<=2,"no descent past budget");
 auto work=Budget::compatible(simple.size());work.text_work=1;
 rejected(simple,Profile::Plan,work,Rejection::Budget,stats);
 auto bytes=Budget::compatible(simple.size());--bytes.input_bytes;
 rejected(simple,Profile::Plan,bytes,Rejection::Budget,stats);
 require(stats.nodes==0,"byte check before root/observer allocation");
 rejected("schema_version=[0,0,0]",Profile::Plan,Budget::compatible(30),Rejection::Structure,stats);
 require(stats.nodes==1,"scalar field cannot hide an array");
 rejected("memberships=[{}, {}, {}]",Profile::Generation,Budget::compatible(40),Rejection::Structure,stats);
 require(stats.nodes<=3&&stats.array_elements<=1,"empty table amplification stopped before second element");
 rejected("[unexpected.child]\nx=[0,0]",Profile::Generation,Budget::compatible(40),Rejection::Structure,stats);
 require(stats.nodes==1,"unknown header cannot spawn implicit parents");
 std::string long_key(4096,'x');long_key+="=[0,0]";
 rejected(long_key,Profile::Generation,Budget::compatible(long_key.size()),Rejection::Structure,stats);
 require(stats.nodes==1&&stats.text_work<200,"oversized unknown key rejected before proportional buffer growth");
 const std::array<std::string_view,9> spellings={
  "# comment\r\n\"\\u0069d\"=2\r\nentries=[] # tail",
  "id = 2\nentries=[{package='p',store='/store/p'}]",
  "id=2\n[[entries]]\npackage='p'\nstore='/store/p'",
  "id=2\nentries=[]\n# \xe2\x98\x83 UTF-8 comment\n",
  "id=2\nentries=[]\n",
  "schema_version=2\nid=2\nmemberships=[{kind='legacy',package='p',store='/store/p'}]",
  "schema_version=3\nid=2\nmemberships=[]\npolicies=[]",
  "schema_version=3\nid=2\nmemberships=[]\n[[policies]]\nschema_version=1\npolicy='p'\nselector='pkg'\n[policies.target]\nplatform=''\narchitecture=''\nabi=''\ntriple=''",
  "\xef\xbb\xbfid=2\nentries=[]"};
 for(auto input:spellings) {
  require(parse(input,Profile::Generation)==athena_toml::parse(input),"same TOML grammar/tree for historical spellings");
  auto document=athena::generation::validate_generation_document(input,2);
  require(document.bytes==input,"exact generation bytes unchanged");
 }
 // Nested semantics exercise table paths, arbitrary feature names, package
 // arrays and the indirect package/installed codecs reached by generation.
 athena::installed::SemanticState semantic{athena::core::PackageMetadata{athena::core::PackageId("pkg"),athena::core::PackageVersion("1")}};
 semantic.feature_states.emplace("unicode.\xe2\x98\x83",true);
 semantic.feature_states.emplace(std::string(256,'f'),false);
 const auto encoded=athena::installed::serialize(semantic);
 require(parse(encoded,Profile::Semantics)==athena_toml::parse(encoded),"nested semantic schema compatibility");
 require(athena::installed::deserialize_semantics(encoded)==semantic,"nested semantic roundtrip");
 for(auto bad:{"id=[[[[[0]]]]]", "id=2\nentries=[1,2]", "id=2\nentries=[{unknown=[0,0]}]", "id=2\nentries=[{package='p',store=[0,0]}]"}) {
  rejected(bad,Profile::Generation,Budget::compatible(100),Rejection::Structure,stats);
 }
 for(std::size_t n=0;n<simple.size();++n) {
  auto input=std::string_view(simple).substr(0,n);
  try {auto checked=parse(input,Profile::Plan);require(checked==athena_toml::parse(input),"valid truncated prefix agrees with original grammar");}
  catch(const Error&){}
  catch(const athena_toml::parse_error&){}
 }
 try{Budget::compatible(std::numeric_limits<std::size_t>::max());throw std::runtime_error("budget overflow accepted");}
 catch(const Error& e){require(e.reason==Rejection::Budget,"budget overflow rejected");}
}
}
int main(){return d1_test::run(tests);}
