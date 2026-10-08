#include "dat_native_stage.hpp"
#include "stadium_c0_native_map_contract.hpp"
#include "dat_stage.hpp"
#include "gameplay_bootstrap.h"
#include "gameplay_content.h"
#include "gameplay_stage_numeric.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>
extern "C" int melee_web_test_native_stage_map(void*,void*);
extern "C" int melee_web_test_native_stadium_map(void*);
extern "C" const uint8_t* melee_web_test_native_stadium_flags(void*,int);
extern "C" void* melee_web_test_native_stadium_flag(void*,int);
extern "C" int melee_web_test_native_marker_pairs(void*,const uint16_t*,int);
extern "C" int melee_web_test_ground_marker_last_write(void*);
static void check(bool c,const char* e){if(!c)throw std::runtime_error(e);}
namespace {
using namespace melee_web::test;
std::vector<uint8_t> read_bytes(const char* path){
 std::ifstream file(path,std::ios::binary);
 check(bool(file),"open local stage archive");
 return {std::istreambuf_iterator<char>(file),{}};
}
void write_be32(std::vector<uint8_t>& bytes,uint32_t offset,uint32_t value){
 check(size_t(offset)+4<=bytes.size(),"corrupt source pointer remains within test input");
 for(unsigned i=0;i<4;i++)bytes[offset+i]=uint8_t(value>>(24-8*i));
}
void write_be16(std::vector<uint8_t>& bytes,uint32_t offset,uint16_t value){
 check(size_t(offset)+2<=bytes.size(),"corrupt source halfword remains within test input");
 bytes[offset]=uint8_t(value>>8);bytes[offset+1]=uint8_t(value);
}
uint32_t read_be32(const std::vector<uint8_t>& bytes,uint32_t offset){
 check(size_t(offset)+4<=bytes.size(),"read source word remains within test input");
 return (uint32_t(bytes[offset])<<24)|(uint32_t(bytes[offset+1])<<16)|
        (uint32_t(bytes[offset+2])<<8)|uint32_t(bytes[offset+3]);
}
template<class F>void expect_error(F&& operation,std::string_view expected){
 bool rejected=false;
 try{operation();}catch(const melee_web::DatError& error){rejected=std::string_view(error.what()).find(expected)!=std::string_view::npos;}
 check(rejected,"native map negative case rejects at its declared boundary");
}
std::vector<uint8_t> marker_fixture(uint32_t node_count=13,bool null_flag=false,
                                    bool external_map_field=false,bool local_flag=false,
                                    bool external_flag=false){
 constexpr uint32_t data_size=0x600,root=0x100,references=0x140,entries=0x160;
 constexpr uint32_t pairs=0x1a0,flagged=0x1c0,tree=0x200;
 check(!(local_flag&&external_flag),"synthetic flagged slot has one authored kind");
 check(node_count>=13&&tree+node_count*64<=data_size,"synthetic marker tree fits bounded fixture");
 std::vector<uint8_t> data(data_size,0);
 write_be32(data,root,references);write_be32(data,root+4,1);
 write_be32(data,root+8,entries);write_be32(data,root+12,1);
 write_be32(data,references,tree);write_be32(data,references+4,pairs);write_be32(data,references+8,8);
 if(null_flag||local_flag||external_flag){
  write_be32(data,root+40,flagged);write_be32(data,root+44,1);
  if(local_flag)write_be32(data,flagged,tree);
  if(external_flag)write_be32(data,flagged,UINT32_MAX);
 }
 if(external_map_field)write_be32(data,entries+4,UINT32_MAX);
 write_be32(data,entries,tree);
 for(uint32_t i=0;i<node_count;i++){
  const uint32_t node=tree+64*i;
  if(i+1<node_count)write_be32(data,node+8,node+64);
  write_be32(data,node+32,0x3f800000);write_be32(data,node+36,0x3f800000);write_be32(data,node+40,0x3f800000);
 }
 constexpr std::array<std::array<uint16_t,2>,8> authored={{{3,0},{0,1},{2,2},{1,3},{12,135},{4,135},{9,134},{9,134}}};
 for(uint32_t i=0;i<authored.size();i++){
  write_be16(data,pairs+4*i,authored[i][0]);write_be16(data,pairs+4*i+2,authored[i][1]);
 }
 std::vector<uint32_t> relocations={root,root+8,references,references+4,entries};
 if(null_flag||local_flag||external_flag)relocations.push_back(root+40);
 if(local_flag)relocations.push_back(flagged);
 for(uint32_t i=0;i+1<node_count;i++)relocations.push_back(tree+64*i+8);
 std::sort(relocations.begin(),relocations.end());
 std::vector<uint8_t> names={'m','a','p','_','h','e','a','d',0};
 const auto add_name=[&](std::string_view name){
  const uint32_t offset=uint32_t(names.size());
  for(char value:name)names.push_back(uint8_t(value));
  names.push_back(0);
  return offset;
 };
 const uint32_t external_map_name=external_map_field?add_name("SyntheticExternal"):0;
 const uint32_t external_flag_name=external_flag?add_name("SyntheticFlag"):0;
 const uint32_t external_count=uint32_t(external_map_field)+uint32_t(external_flag);
 const size_t total=0x20+data.size()+relocations.size()*4+8+
                    size_t(external_count)*8+names.size();
 std::vector<uint8_t> archive(total,0);
 write_be32(archive,0,uint32_t(total));write_be32(archive,4,data_size);
 write_be32(archive,8,uint32_t(relocations.size()));write_be32(archive,12,1);
 write_be32(archive,16,external_count);
 std::copy(data.begin(),data.end(),archive.begin()+0x20);
 size_t cursor=0x20+data.size();
 for(uint32_t slot:relocations){write_be32(archive,uint32_t(cursor),slot);cursor+=4;}
 write_be32(archive,uint32_t(cursor),root);write_be32(archive,uint32_t(cursor+4),0);cursor+=8;
 if(external_map_field){
  write_be32(archive,uint32_t(cursor),entries+4);
  write_be32(archive,uint32_t(cursor+4),external_map_name);
  cursor+=8;
 }
 if(external_flag){
  write_be32(archive,uint32_t(cursor),flagged);
  write_be32(archive,uint32_t(cursor+4),external_flag_name);
  cursor+=8;
 }
 std::copy(names.begin(),names.end(),archive.begin()+static_cast<std::ptrdiff_t>(cursor));
 return archive;
}
const melee_web::DatNativeMapContract& marker_contract(){
 static constexpr std::array<uint8_t,1> animation_counts={1};
 static constexpr std::array<uint32_t,1> residents={0};
 static const melee_web::DatNativeMapContract contract{1,animation_counts,residents,{},{},{}};
 return contract;
}
void expect_marker_map_error(const std::vector<uint8_t>& bytes,std::string_view message){
 auto archive=std::make_shared<melee_web::DatArchive>(bytes);
 expect_error([&]{melee_web::DatNativeMap rejected(archive,marker_contract());},message);
}
void profile_map_contract_controls(const std::vector<uint8_t>& legacy_bytes){
 using namespace melee_web;
 static constexpr std::array<uint8_t,1> animation_counts={1};
 auto legacy_archive=std::make_shared<DatArchive>(legacy_bytes);
 DatStage legacy_metadata(*legacy_archive);
 MeleeWebStageProfile legacy_profile{};
 legacy_profile.entry_count=1;
 legacy_profile.animation_counts=animation_counts.data();
 legacy_profile.animation_count_count=animation_counts.size();
 legacy_profile.map_ownership_policy=MELEE_WEB_STAGE_MAP_OWNERSHIP_CURRENT_ALL_RESIDENT;
 auto legacy=dat_native_stage_map_contract_from_profile(
     legacy_profile,*legacy_archive,legacy_metadata);
 const auto legacy_view=legacy.view();
 check(legacy_view.entry_count==1&&legacy_view.animation_consumer_counts.size()==1&&
       legacy_view.animation_consumer_counts[0]==1,
       "explicit current policy retains authored animation consumer counts");
 check(legacy_view.resident_entry_ids.size()==1&&legacy_view.resident_entry_ids[0]==0&&
       legacy_view.external_references.empty()&&legacy_view.animation_flag_entry_ids.empty()&&
       legacy_view.flagged_objects.empty(),
       "explicit current policy preserves prior all-resident/archive-derived map contract");
 auto derived_bytes=marker_fixture(13,false,true,true);
 auto derived_archive=std::make_shared<DatArchive>(derived_bytes,DatExternalPolicy::ResolveNull);
 DatStage derived_metadata(*derived_archive);
 auto derived=dat_native_stage_map_contract_from_profile(
     legacy_profile,*derived_archive,derived_metadata);
 const auto derived_view=derived.view();
 check(derived_view.resident_entry_ids.size()==1&&derived_view.resident_entry_ids[0]==0&&
       derived_view.external_references.size()==1&&
       derived_view.external_references[0].entry_index==0&&
       derived_view.external_references[0].field_offset==4&&
       derived_view.external_references[0].symbol=="SyntheticExternal"&&
       derived_view.animation_flag_entry_ids.empty()&&
       derived_view.flagged_objects.size()==1&&
       derived_view.flagged_objects[0].index==0&&
       derived_view.flagged_objects[0].kind==DatNativeMapFlagKind::LocalMaterial&&
       derived_view.flagged_objects[0].target_offset==0x200,
       "current policy retains exact archive-derived external and local-flag identities");
 {
  DatNativeMap owner(legacy_archive,legacy_view);
  check(owner.map_head(),"explicit current profile policy hydrates the existing synthetic map");
 }

 const std::array<int,7> current_profile_kinds={
  St_Kind_Last,St_Kind_Battle,St_Kind_Story,St_Kind_OldPupupu,
  St_Kind_Shrine,St_Kind_Izumi,St_Kind_OldYoshi,
 };
 for(int kind:current_profile_kinds){
  const auto* profile=melee_web_stage_profile(kind);
  check(profile&&profile->map_ownership_policy==MELEE_WEB_STAGE_MAP_OWNERSHIP_CURRENT_ALL_RESIDENT&&
        !profile->map_ownership,
        "each existing complete stage profile explicitly selects the current map policy");
 }

 auto unspecified=legacy_profile;
 unspecified.map_ownership_policy=MELEE_WEB_STAGE_MAP_OWNERSHIP_UNSPECIFIED;
 expect_error([&]{(void)dat_native_stage_map_contract_from_profile(
     unspecified,*legacy_archive,legacy_metadata);},"no explicit map ownership policy");
 static const MeleeWebStageMapOwnership empty_declaration{};
 auto contradictory=legacy_profile;
 contradictory.map_ownership=&empty_declaration;
 expect_error([&]{(void)dat_native_stage_map_contract_from_profile(
     contradictory,*legacy_archive,legacy_metadata);},"contradicts an authored declaration");
 auto missing_authored=legacy_profile;
 missing_authored.map_ownership_policy=MELEE_WEB_STAGE_MAP_OWNERSHIP_AUTHORED;
 expect_error([&]{(void)dat_native_stage_map_contract_from_profile(
     missing_authored,*legacy_archive,legacy_metadata);},"lacks its declaration");

 static constexpr std::array<uint32_t,1> resident_ids={0};
 static constexpr std::array<uint32_t,1> flag_consumers={0};
 static constexpr std::array<MeleeWebStageMapExternalReference,1> external_references={{{
  0,4,"SyntheticExternal"
 }}};
 static constexpr std::array<MeleeWebStageMapFlagExpectation,1> null_flag={{{
  0,MELEE_WEB_STAGE_MAP_FLAG_NULL,0,nullptr
 }}};
 static const MeleeWebStageMapOwnership authored_ownership={
  resident_ids.data(),resident_ids.size(),
  external_references.data(),external_references.size(),
  flag_consumers.data(),flag_consumers.size(),
  null_flag.data(),null_flag.size(),
 };
 auto authored_bytes=marker_fixture(13,true,true);
 auto authored_archive=std::make_shared<DatArchive>(authored_bytes,DatExternalPolicy::ResolveNull);
 DatStage authored_metadata(*authored_archive);
 MeleeWebStageProfile authored_profile{};
 authored_profile.entry_count=1;
 authored_profile.animation_counts=animation_counts.data();
 authored_profile.animation_count_count=animation_counts.size();
 authored_profile.map_ownership_policy=MELEE_WEB_STAGE_MAP_OWNERSHIP_AUTHORED;
 authored_profile.map_ownership=&authored_ownership;
 auto authored=dat_native_stage_map_contract_from_profile(
     authored_profile,*authored_archive,authored_metadata);
 const auto authored_view=authored.view();
 check(authored_view.entry_count==1&&authored_view.animation_consumer_counts.size()==1&&
       authored_view.animation_consumer_counts[0]==1&&
       std::equal(authored_view.resident_entry_ids.begin(),authored_view.resident_entry_ids.end(),resident_ids.begin())&&
       std::equal(authored_view.animation_flag_entry_ids.begin(),authored_view.animation_flag_entry_ids.end(),flag_consumers.begin())&&
       authored_view.external_references.size()==1&&
       authored_view.external_references[0].entry_index==0&&
       authored_view.external_references[0].field_offset==4&&
       authored_view.external_references[0].symbol=="SyntheticExternal"&&
       authored_view.flagged_objects.size()==1&&
       authored_view.flagged_objects[0].index==0&&
       authored_view.flagged_objects[0].kind==DatNativeMapFlagKind::Null&&
       authored_view.flagged_objects[0].target_offset==0&&
       authored_view.flagged_objects[0].symbol.empty(),
       "authored profile maps exact row, flag-consumer and null-slot declarations");
 {
  DatNativeMap owner(authored_archive,authored_view);
  check(owner.map_head(),"complete authored profile declaration reaches the checked map hydrator");
 }

 static constexpr std::array<MeleeWebStageMapExternalReference,1> wrong_external={{{
  0,4,"WrongExternal"
 }}};
 auto wrong_external_ownership=authored_ownership;
 wrong_external_ownership.external_references=wrong_external.data();
 auto wrong_external_profile=authored_profile;
 wrong_external_profile.map_ownership=&wrong_external_ownership;
 auto wrong_external_contract=dat_native_stage_map_contract_from_profile(
     wrong_external_profile,*authored_archive,authored_metadata);
 expect_error([&]{DatNativeMap rejected(authored_archive,wrong_external_contract.view());},
              "external entry symbol differs");
 auto missing_external_ownership=authored_ownership;
 missing_external_ownership.external_references=nullptr;
 missing_external_ownership.external_reference_count=0;
 auto missing_external_profile=authored_profile;
 missing_external_profile.map_ownership=&missing_external_ownership;
 auto missing_external_contract=dat_native_stage_map_contract_from_profile(
     missing_external_profile,*authored_archive,authored_metadata);
 expect_error([&]{DatNativeMap rejected(authored_archive,missing_external_contract.view());},
              "external entry fields differ");
 static constexpr std::array<MeleeWebStageMapExternalReference,1> extra_external={{{
  0,8,"SyntheticExtra"
 }}};
 auto extra_external_ownership=authored_ownership;
 extra_external_ownership.external_references=extra_external.data();
 auto extra_external_profile=authored_profile;
 extra_external_profile.map_ownership=&extra_external_ownership;
 auto extra_external_contract=dat_native_stage_map_contract_from_profile(
     extra_external_profile,*authored_archive,authored_metadata);
 expect_error([&]{DatNativeMap rejected(authored_archive,extra_external_contract.view());},
              "external entry fields differ");

 auto incomplete_declaration=authored_ownership;
 incomplete_declaration.external_references=nullptr;
 incomplete_declaration.external_reference_count=1;
 auto incomplete_profile=authored_profile;
 incomplete_profile.map_ownership=&incomplete_declaration;
 expect_error([&]{(void)dat_native_stage_map_contract_from_profile(
     incomplete_profile,*authored_archive,authored_metadata);},"external-reference list is missing");
 auto incomplete_flags=authored_ownership;
 incomplete_flags.flagged_objects=nullptr;
 auto incomplete_flags_profile=authored_profile;
 incomplete_flags_profile.map_ownership=&incomplete_flags;
 expect_error([&]{(void)dat_native_stage_map_contract_from_profile(
     incomplete_flags_profile,*authored_archive,authored_metadata);},"flagged-object list is missing");
 static constexpr std::array<uint32_t,1> invalid_resident={1};
 auto invalid_resident_ownership=authored_ownership;
 invalid_resident_ownership.resident_entry_ids=invalid_resident.data();
 auto invalid_resident_profile=authored_profile;
 invalid_resident_profile.map_ownership=&invalid_resident_ownership;
 auto invalid_resident_contract=dat_native_stage_map_contract_from_profile(
     invalid_resident_profile,*authored_archive,authored_metadata);
 expect_error([&]{DatNativeMap rejected(authored_archive,invalid_resident_contract.view());},
              "invalid or duplicate resident ID");
 std::array<MeleeWebStageMapFlagExpectation,1> invalid_flag{};
 invalid_flag[0]={0,static_cast<MeleeWebStageMapFlagKind>(99),0,nullptr};
 auto invalid_flag_ownership=authored_ownership;
 invalid_flag_ownership.flagged_objects=invalid_flag.data();
 auto invalid_flag_profile=authored_profile;
 invalid_flag_profile.map_ownership=&invalid_flag_ownership;
 expect_error([&]{(void)dat_native_stage_map_contract_from_profile(
     invalid_flag_profile,*authored_archive,authored_metadata);},"unknown kind");

 auto local_flag_bytes=marker_fixture(13,false,false,true);
 auto local_flag_archive=std::make_shared<DatArchive>(local_flag_bytes);
 DatStage local_flag_metadata(*local_flag_archive);
 static constexpr std::array<MeleeWebStageMapFlagExpectation,1> local_flag={{{
  0,MELEE_WEB_STAGE_MAP_FLAG_LOCAL_MATERIAL,0x200,nullptr
 }}};
 auto local_flag_ownership=authored_ownership;
 local_flag_ownership.external_references=nullptr;
 local_flag_ownership.external_reference_count=0;
 local_flag_ownership.flagged_objects=local_flag.data();
 auto local_flag_profile=authored_profile;
 local_flag_profile.map_ownership=&local_flag_ownership;
 auto local_flag_contract=dat_native_stage_map_contract_from_profile(
     local_flag_profile,*local_flag_archive,local_flag_metadata);
 const auto local_flag_view=local_flag_contract.view();
 check(local_flag_view.flagged_objects.size()==1&&
       local_flag_view.flagged_objects[0].kind==DatNativeMapFlagKind::LocalMaterial&&
       local_flag_view.flagged_objects[0].target_offset==0x200&&
       local_flag_view.flagged_objects[0].symbol.empty(),
       "authored local-material flag preserves its exact source target");
 expect_error([&]{DatNativeMap rejected(local_flag_archive,local_flag_view);},
              "flag mutation names unsupported descriptor kind");

 auto external_flag_bytes=marker_fixture(13,false,false,false,true);
 auto external_flag_archive=std::make_shared<DatArchive>(
     external_flag_bytes,DatExternalPolicy::ResolveNull);
 DatStage external_flag_metadata(*external_flag_archive);
 static constexpr std::array<MeleeWebStageMapFlagExpectation,1> external_flag={{{
  0,MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL,0,"SyntheticFlag"
 }}};
 auto external_flag_ownership=authored_ownership;
 external_flag_ownership.external_references=nullptr;
 external_flag_ownership.external_reference_count=0;
 external_flag_ownership.flagged_objects=external_flag.data();
 auto external_flag_profile=authored_profile;
 external_flag_profile.map_ownership=&external_flag_ownership;
 auto external_flag_contract=dat_native_stage_map_contract_from_profile(
     external_flag_profile,*external_flag_archive,external_flag_metadata);
 const auto external_flag_view=external_flag_contract.view();
 check(external_flag_view.flagged_objects.size()==1&&
       external_flag_view.flagged_objects[0].kind==DatNativeMapFlagKind::ExternalNull&&
       external_flag_view.flagged_objects[0].target_offset==0&&
       external_flag_view.flagged_objects[0].symbol=="SyntheticFlag",
       "authored external-null flag preserves its exact symbol identity");
 {
  DatNativeMap owner(external_flag_archive,external_flag_view);
  check(owner.map_head(),"authored external-null flag reaches the checked map hydrator");
 }
 static constexpr std::array<MeleeWebStageMapFlagExpectation,1> wrong_flag={{{
  0,MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL,0,"WrongFlag"
 }}};
 auto wrong_flag_ownership=external_flag_ownership;
 wrong_flag_ownership.flagged_objects=wrong_flag.data();
 auto wrong_flag_profile=external_flag_profile;
 wrong_flag_profile.map_ownership=&wrong_flag_ownership;
 auto wrong_flag_contract=dat_native_stage_map_contract_from_profile(
     wrong_flag_profile,*external_flag_archive,external_flag_metadata);
 expect_error([&]{DatNativeMap rejected(external_flag_archive,wrong_flag_contract.view());},
              "flagged external slot or name differs");
 static constexpr std::array<MeleeWebStageMapFlagExpectation,1> missing_flag_identity={{{
  0,MELEE_WEB_STAGE_MAP_FLAG_NULL,0,nullptr
 }}};
 auto missing_flag_ownership=external_flag_ownership;
 missing_flag_ownership.flagged_objects=missing_flag_identity.data();
 auto missing_flag_profile=external_flag_profile;
 missing_flag_profile.map_ownership=&missing_flag_ownership;
 auto missing_flag_contract=dat_native_stage_map_contract_from_profile(
     missing_flag_profile,*external_flag_archive,external_flag_metadata);
 expect_error([&]{DatNativeMap rejected(external_flag_archive,missing_flag_contract.view());},
              "unexpectedly names an external slot");
 static constexpr std::array<MeleeWebStageMapFlagExpectation,1> external_with_target={{{
  0,MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL,0x200,"SyntheticFlag"
 }}};
 auto external_with_target_ownership=external_flag_ownership;
 external_with_target_ownership.flagged_objects=external_with_target.data();
 auto external_with_target_profile=external_flag_profile;
 external_with_target_profile.map_ownership=&external_with_target_ownership;
 expect_error([&]{(void)dat_native_stage_map_contract_from_profile(
     external_with_target_profile,*external_flag_archive,external_flag_metadata);},
     "has a local target");
 std::cout<<"Explicit stage-profile map policies and authored ownership adapter controls passed\n";
}
void marker_fixture_trace(){
 const auto original=marker_fixture();
 auto archive=std::make_shared<melee_web::DatArchive>(original);
 const auto data_section=archive->data();
 check(data_section.size()==read_be32(original,4)&&data_section.size()<original.size(),
       "Synthetic DAT data view has its header-declared size and is smaller than the full file");
 check(std::equal(data_section.begin(),data_section.end(),original.begin()+0x20),
       "Synthetic DAT data view exactly matches the raw file data-section span");
 {
  melee_web::NativeDatArena arena(archive);
  expect_error([&]{melee_web_stage_markers_decode(arena.reader(),0x100);},
               "Invalid or duplicate marker binding");
 }
 auto unique_without_camera=original;
 write_be16(unique_without_camera,0x20+0x1a0+5*4+2,136);
 write_be16(unique_without_camera,0x20+0x1a0+7*4+2,137);
 auto unique_archive=std::make_shared<melee_web::DatArchive>(unique_without_camera);
 {
  melee_web::NativeDatArena arena(unique_archive);
  expect_error([&]{melee_web_stage_markers_decode(arena.reader(),0x100);},
               "Missing source camera or blast marker");
 }
 char error[256];
 check(melee_web_gameplay_startup(32*1024*1024,error,sizeof(error)),error);
 check(melee_web_native_world_enable(error,sizeof(error)),error);
 profile_map_contract_controls(original);
 constexpr std::array<uint16_t,16> authored={3,0,0,1,2,2,1,3,12,135,4,135,9,134,9,134};
 {
  melee_web::DatNativeMap owner(archive,marker_contract());
  void* map=owner.map_head();
  check(melee_web_test_native_marker_pairs(map,authored.data(),8),
        "structural owner preserves exact native joint-reference pair order");
  check(melee_web_test_ground_marker_last_write(map),
        "authored duplicate marker pairs retain original Ground last-write semantics");
  expect_error([&]{(void)owner.collision();},
               "Collision public coll_data descriptor is missing");
 }
 auto bad_pair_index=original;write_be16(bad_pair_index,0x20+0x1a0+4,13);
 expect_marker_map_error(bad_pair_index,"Invalid marker binding");
 auto bad_marker_id=original;write_be16(bad_marker_id,0x20+0x1a0+2,261);
 expect_marker_map_error(bad_marker_id,"Invalid marker binding");
 auto excessive_pair_count=original;write_be32(excessive_pair_count,0x20+0x140+8,262);
 expect_marker_map_error(excessive_pair_count,"Invalid marker tree or pair count");
 auto bad_pair_range=original;write_be32(bad_pair_range,0x20+0x140+4,0x5f0);
 expect_marker_map_error(bad_pair_range,"range");
 auto cycle=original;write_be32(cycle,0x20+0x200+8,0x200);
 expect_marker_map_error(cycle,"Marker joint cycle or shared subtree");
 auto nonfinite=original;write_be32(nonfinite,0x20+0x200+20,0x7fc00000);
 expect_marker_map_error(nonfinite,"Nonfinite marker transform");
 check(melee_web_gameplay_shutdown(error,sizeof(error)),error);
 std::cout<<"Structural marker pairs preserve duplicates/order, strict stage requirements and checked negatives passed\n";
}
void stadium_map_trace(const char* path){
 auto bytes=read_bytes(path);
 auto archive=std::make_shared<melee_web::DatArchive>(bytes,melee_web::DatExternalPolicy::ResolveNull);
  const auto original_data_section=std::vector<uint8_t>(archive->data().begin(),archive->data().end());
  melee_web::DatStage metadata(*archive);
  auto contract_data=stadium_contract_data(*archive,metadata);
  const auto stadium_contract=contract_data.view();
  check(metadata.entries.size()==10&&metadata.flagged_object_table.count==44,
       "C0 authored Stadium map row/flag counts");
 for(const auto& expected:stadium_contract.external_references){
  const auto& row=metadata.entries[expected.entry_index];
  const uint32_t slot=row.descriptor_offset+expected.field_offset;
  bool found=false;
  for(const auto& symbol:archive->external_symbols())
   if(symbol.name==expected.symbol&&std::find(symbol.slots.begin(),symbol.slots.end(),slot)!=symbol.slots.end())found=true;
  check(found,"C0 exact external row/field identity");
 }
 char error[256];
 check(melee_web_gameplay_startup(32*1024*1024,error,sizeof(error)),error);
 check(melee_web_native_world_enable(error,sizeof(error)),error);
 for(unsigned cycle=0;cycle<2;cycle++){
  {
   melee_web::DatNativeMap owner(archive,stadium_contract);
   void* map=owner.map_head();
   check(melee_web_test_native_stadium_map(map),"C0 resident map owner and imported-null rows");
   check(owner.collision(),"C0 map owner exposes its checked native coll_data view");
   const auto light_counts=owner.source_light_counts();
   const auto source_light_counts=ground_authored_light_pointer_counts(*archive,metadata);
   check(light_counts.size()==metadata.entries.size(),"C0 map source-light row count");
   for(const auto& entry:metadata.entries){
    check(light_counts[entry.index]==source_light_counts.at(entry.index),
          "C0 map light row matches source table");
    if(entry.animation_flags_offset){
     const auto* flags=melee_web_test_native_stadium_flags(map,int(entry.index));
     const auto source=archive->range(*entry.animation_flags_offset,stadium_contract.animation_consumer_counts[entry.index]);
     check(flags&&std::equal(source.begin(),source.end(),flags),
           "C0 local animation flags remain present on imported rows");
    }
   }
   std::map<uint32_t,void*> aliases;
   for(uint32_t i=0;i<metadata.flagged_object_table.count;i++){
    const uint32_t slot=*metadata.flagged_object_table.data_offset+4*i;
    const auto source=archive->pointer(slot,8);
    void* native=melee_web_test_native_stadium_flag(map,int(i));
    check(bool(source)==(native!=nullptr),"C0 flagged-object null slot order is preserved");
    if(source){
     const auto [it,inserted]=aliases.emplace(*source,native);
     if(!inserted)check(it->second==native,"C0 repeated material alias keeps native identity");
    }else{
     bool external=false;
     for(const auto& symbol:archive->external_symbols())
      if(std::find(symbol.slots.begin(),symbol.slots.end(),slot)!=symbol.slots.end())external=true;
     check(external||!archive->has_relocation(slot),
           "C0 flagged null is an authored external or null slot");
    }
   }
  }
  const auto retained_data_section=archive->data();
  check(retained_data_section.size()==original_data_section.size()&&
            std::equal(original_data_section.begin(),original_data_section.end(),retained_data_section.begin()),
        "C0 resolved archive data section remains byte-identical to its pre-owner baseline");
  const auto retained_file=read_bytes(path);
  check(retained_file.size()==bytes.size()&&std::equal(bytes.begin(),bytes.end(),retained_file.begin()),
        "C0 original full archive file remains byte-identical");
 }

 // Contract failures are deterministic input-boundary checks and do not
 // widen the owner into a stage profile.
 auto wrong_count=stadium_contract;wrong_count.entry_count=9;
 expect_error([&]{melee_web::DatNativeMap rejected(archive,wrong_count);},"entry count");
 std::vector<uint8_t> bad_counts(stadium_contract.animation_consumer_counts.begin(),stadium_contract.animation_consumer_counts.end());bad_counts[3]=65;
 auto bad_bound=stadium_contract;bad_bound.animation_consumer_counts=bad_counts;
 expect_error([&]{melee_web::DatNativeMap rejected(archive,bad_bound);},"animation consumer count");
 constexpr std::array<uint32_t,3> missing_resident={0,1,2};
 auto missing=stadium_contract;missing.resident_entry_ids=missing_resident;
 expect_error([&]{melee_web::DatNativeMap rejected(archive,missing);},"Unexpected local joint");
 std::vector<melee_web::DatNativeMapExternalReference> wrong_imports(stadium_contract.external_references.begin(),stadium_contract.external_references.end());
 wrong_imports[0].symbol="GrdPStadiumWrong_TopN_joint";
 auto wrong_import=stadium_contract;wrong_import.external_references=wrong_imports;
 expect_error([&]{melee_web::DatNativeMap rejected(archive,wrong_import);},"symbol differs");
 std::vector<melee_web::DatNativeMapFlagExpectation> wrong_flag_name(stadium_contract.flagged_objects.begin(),stadium_contract.flagged_objects.end());
 wrong_flag_name[2].symbol="GrdPStadiumWrong_FShadowmat3_mobjdesc";
 auto wrong_flag_identity=stadium_contract;wrong_flag_identity.flagged_objects=wrong_flag_name;
 expect_error([&]{melee_web::DatNativeMap rejected(archive,wrong_flag_identity);},"flagged external slot or name");
 std::vector<melee_web::DatNativeMapFlagExpectation> wrong_flag_slot(stadium_contract.flagged_objects.begin(),stadium_contract.flagged_objects.end());wrong_flag_slot[2].index=3;
 auto wrong_flag_order=stadium_contract;wrong_flag_order.flagged_objects=wrong_flag_slot;
 expect_error([&]{melee_web::DatNativeMap rejected(archive,wrong_flag_order);},"expectations changed authored order");

 // Move one retained external-chain head to an otherwise unused null word.
 // The resulting archive remains parseable, but its flag identity is now at
 // the wrong source slot and must fail the map contract before hydration.
 const uint32_t external_flag_slot=*metadata.flagged_object_table.data_offset+2*4;
 size_t external_index=archive->external_symbols().size();
 for(size_t i=0;i<archive->external_symbols().size();i++){
  const auto& symbol=archive->external_symbols()[i];
  if(symbol.name==stadium_contract.flagged_objects[2].symbol&&
     std::find(symbol.slots.begin(),symbol.slots.end(),external_flag_slot)!=symbol.slots.end())
   external_index=i;
 }
 check(external_index<archive->external_symbols().size(),"find C0 source external flagged slot");
 const uint32_t external_table=32+read_be32(bytes,4)+read_be32(bytes,8)*4+read_be32(bytes,12)*8;
 uint32_t replacement_slot=UINT32_MAX;
 const uint32_t data_size=read_be32(bytes,4);
 for(uint32_t candidate=(data_size-4)&~uint32_t(3);;candidate-=4){
  bool already_external=false;
  for(const auto& symbol:archive->external_symbols())
   if(std::find(symbol.slots.begin(),symbol.slots.end(),candidate)!=symbol.slots.end())already_external=true;
  if(candidate!=external_flag_slot&&!archive->has_relocation(candidate)&&
     !already_external&&read_be32(bytes,32+candidate)==0){replacement_slot=candidate;break;}
  if(candidate<4)break;
 }
 check(replacement_slot!=UINT32_MAX,"find unused aligned null word for external-slot mutation");
 auto shifted_flag_slot=bytes;
 write_be32(shifted_flag_slot,external_table+uint32_t(external_index)*8,replacement_slot);
 write_be32(shifted_flag_slot,32+replacement_slot,UINT32_MAX);
 auto shifted_archive=std::make_shared<melee_web::DatArchive>(shifted_flag_slot,melee_web::DatExternalPolicy::ResolveNull);
 expect_error([&]{melee_web::DatNativeMap rejected(shifted_archive,stadium_contract);},"flagged external slot or name");

 // The bad local joint target fails during typed source parsing. A bad flag
 // target fails later, after resident graph ownership has already begun.
 auto malformed_joint=bytes;
 const uint32_t joint_slot=metadata.entries[1].descriptor_offset;
 write_be32(malformed_joint,32+joint_slot,uint32_t(archive->data().size()-4));
 auto malformed_archive=std::make_shared<melee_web::DatArchive>(malformed_joint,melee_web::DatExternalPolicy::ResolveNull);
 expect_error([&]{melee_web::DatNativeMap rejected(malformed_archive,stadium_contract);},"range");

 const uint32_t local_flag_slot=*metadata.flagged_object_table.data_offset;
 auto zeroed_local_flag=bytes;
 write_be32(zeroed_local_flag,32+local_flag_slot,0);
 auto zeroed_flag_archive=std::make_shared<melee_web::DatArchive>(zeroed_local_flag,melee_web::DatExternalPolicy::ResolveNull);
 expect_error([&]{melee_web::DatNativeMap rejected(zeroed_flag_archive,stadium_contract);},"local flagged-object target differs");

 auto unowned_flag=bytes;
 write_be32(unowned_flag,32+local_flag_slot,metadata.root_offset);
 auto unowned_archive=std::make_shared<melee_web::DatArchive>(unowned_flag,melee_web::DatExternalPolicy::ResolveNull);
 std::vector<melee_web::DatNativeMapFlagExpectation> unowned_expectations(stadium_contract.flagged_objects.begin(),stadium_contract.flagged_objects.end());
 unowned_expectations[0].target_offset=metadata.root_offset;
 auto unowned_contract=stadium_contract;unowned_contract.flagged_objects=unowned_expectations;
 expect_error([&]{melee_web::DatNativeMap rejected(unowned_archive,unowned_contract);},"flag mutation");
 {
  melee_web::DatNativeMap recovered(archive,stadium_contract);
  check(melee_web_test_native_stadium_map(recovered.map_head()),"C0 map ownership recovers after partial-allocation rejection");
 }
 check(melee_web_gameplay_shutdown(error,sizeof(error)),error);
 std::cout<<"C0 Stadium map-only owner, exact imported metadata, auxiliary rows, null flags and rejection passed\n";
}
}
int main(int argc,char** argv){try{
 if(argc==2&&std::string_view(argv[1])=="--marker-fixture"){marker_fixture_trace();return 0;}
 if(argc==3&&std::string_view(argv[1])=="--stadium-map"){stadium_map_trace(argv[2]);return 0;}
 check(argc==2,"expected local GrNLa.dat path");std::ifstream f(argv[1],std::ios::binary);check(bool(f),"open GrNLa.dat");
 std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
 auto archive=std::make_shared<melee_web::DatArchive>(bytes);char error[256];
 for(unsigned cycle=0;cycle<2;cycle++){
  check(melee_web_gameplay_startup(32*1024*1024,error,sizeof(error)),error);
  check(melee_web_native_world_enable(error,sizeof(error)),error);
  {
   melee_web::DatNativeStage stage(archive);
   const auto light_counts=stage.source_light_counts();
   melee_web::DatStage stage_metadata(*archive);
   const auto source_light_counts=ground_authored_light_pointer_counts(*archive,stage_metadata);
   check(light_counts.size()==stage_metadata.entries.size(),
         "Native stage source-light bounds differ from the authored entry table");
   for(const auto& entry:stage_metadata.entries)
    check(light_counts[entry.index]==source_light_counts.at(entry.index),
          "Native Ground light bound differs from its null-terminated DAT table");
   check(!stage.particle_events().empty(),"actual stage particle events retained");
   for(const auto& event:stage.particle_events())check(event.bank==30,"actual stage generator bank selector");
   check(melee_web_test_native_stage_map(stage.map_head(),stage.yakumono()),"original native map descriptors invalid");
   auto* publication=melee_web_stage_map_publish(stage.map_head(),error,sizeof(error));check(publication,error);
   const auto& symbols=stage.public_symbols();
   check(melee_web_stage_map_set_public(publication,symbols.data(),symbols.size(),error,sizeof(error)),error);
   auto* source_archive=melee_web_archive_sections_open("GrNLa.dat");
   check(melee_web_archive_sections_public(source_archive,"map_head")==stage.map_head()&&
         melee_web_archive_sections_public(source_archive,"yakumono_param")==stage.yakumono(),
         "Source public map/yakumono symbols preserve native owner identity");
   check(!melee_web_stage_map_close(publication,error,sizeof(error)),"Public source consumer retains descriptor lifetime");
   melee_web_archive_sections_release(source_archive);
   const auto& lights=stage.light_overrides();check(melee_web_stage_map_set_overrides(publication,lights.data(),lights.size(),error,sizeof(error)),error);
   for(const auto& light:lights){int found=-1;uint8_t flags=0xff;check(melee_web_stage_map_lookup_override(light.descriptor,&found,&flags)&&found==light.found&&flags==light.flags,"bounded source light identity lookup");}
   check(melee_web_stage_map_close(publication,error,sizeof(error)),error);
  }
  if(cycle==0){
   auto corrupted=bytes;uint32_t yaku=UINT32_MAX;
   for(const auto& symbol:archive->public_symbols())if(symbol.name=="yakumono_param")yaku=symbol.data_offset;
   check(yaku!=UINT32_MAX,"exact yakumono symbol");auto program=archive->pointer(yaku,4);check(program.has_value(),"first color program");
   const uint32_t opcode=17U<<26;for(unsigned i=0;i<4;i++)corrupted[32+*program+i]=uint8_t(opcode>>(24-8*i));
   bool rejected=false;
   try{melee_web::DatNativeStage bad(std::make_shared<melee_web::DatArchive>(corrupted));}catch(const melee_web::DatError& e){rejected=std::string(e.what()).find("material program opcode")!=std::string::npos;}
   check(rejected,"unsupported native color opcode rejects and releases partial descriptor ownership");
  }
  check(melee_web_gameplay_shutdown(error,sizeof(error)),error);
 }
 std::cout<<"Complete FD native map, scene metadata, material programs, light identities and restart passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
