#include "dat_native_stage.hpp"
#include "dat_stage.hpp"
#include "gameplay_bootstrap.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string_view>
extern "C" int melee_web_test_native_stage_map(void*,void*);
extern "C" int melee_web_test_native_stadium_map(void*);
extern "C" const uint8_t* melee_web_test_native_stadium_flags(void*,int);
extern "C" void* melee_web_test_native_stadium_flag(void*,int);
static void check(bool c,const char* e){if(!c)throw std::runtime_error(e);}
namespace {
constexpr std::array<uint8_t,10> stadium_animation_counts={1,1,1,1,1,1,1,1,1,1};
constexpr std::array<uint32_t,4> stadium_resident_ids={0,1,2,5};
constexpr std::array<uint32_t,10> stadium_animation_flag_consumers={0,1,2,3,4,5,6,7,8,9};
constexpr std::array<melee_web::DatNativeMapExternalReference,26> stadium_external_references={{
 {3,0,"GrdPStadiumFire_TopN_joint"},
 {3,8,"GrdPStadiumFire_TopN_matanim_joint_list"},
 {3,16,"GrdPStadium_Fire_SEDUNIQUEfire_cam_int1_camera"},
 {3,24,"GrdPStadium_Fire_SEDUNIQUEfire_scene_lights"},
 {4,0,"GrdPStadiumGrass_TopN_joint"},
 {4,8,"GrdPStadiumGrass_TopN_matanim_joint_list"},
 {4,16,"GrdPStadium_Grass_SEDUNIQUEgrass_cam_int1_camera"},
 {4,24,"GrdPStadium_Grass_SEDUNIQUEgrass_scene_lights"},
 {6,0,"GrdPStadiumRock_TopN_joint"},
 {6,4,"GrdPStadiumRock_TopN_animjoint_list"},
 {6,16,"GrdPStadium_Rock_SEDUNIQUErock_cam_int1_camera"},
 {6,24,"GrdPStadium_Rock_SEDUNIQUErock_scene_lights"},
 {7,0,"GrdPStadiumWaterFunsuiA_TopN_joint"},
 {7,4,"GrdPStadiumWaterFunsuiA_TopN_animjoint_list"},
 {7,8,"GrdPStadiumWaterFunsuiA_TopN_matanim_joint_list"},
 {7,16,"GrdPStadium_Water_SEDUNIQUEfunsui_a_cam_int1_camera"},
 {7,24,"GrdPStadium_Water_SEDUNIQUEfunsui_a_scene_lights"},
 {8,0,"GrdPStadiumWaterFunsuiB_TopN_joint"},
 {8,4,"GrdPStadiumWaterFunsuiB_TopN_animjoint_list"},
 {8,8,"GrdPStadiumWaterFunsuiB_TopN_matanim_joint_list"},
 {8,16,"GrdPStadium_Water_SEDUNIQUEfunsui_b_cam_int1_camera"},
 {8,24,"GrdPStadium_Water_SEDUNIQUEfunsui_b_scene_lights"},
 {9,0,"GrdPStadiumWater_TopN_joint"},
 {9,8,"GrdPStadiumWater_TopN_matanim_joint_list"},
 {9,16,"GrdPStadium_Water_SEDUNIQUEwater_cam_int1_camera"},
 {9,24,"GrdPStadium_Water_SEDUNIQUEwater_scene_lights"},
}};
constexpr auto local_flag=melee_web::DatNativeMapFlagKind::LocalMaterial;
constexpr auto external_flag=melee_web::DatNativeMapFlagKind::ExternalNull;
constexpr std::array<melee_web::DatNativeMapFlagExpectation,44> stadium_flag_expectations={{
 {0,local_flag,95896,{}},
 {1,local_flag,96584,{}},
 {2,external_flag,0,"GrdPStadiumFire_ATree_FShadowmat3_mobjdesc"},
 {3,external_flag,0,"GrdPStadiumFire_ATree_FShadowmat4_mobjdesc"},
 {4,external_flag,0,"GrdPStadiumFire_ATree_FShadowmat5_mobjdesc"},
 {5,external_flag,0,"GrdPStadiumFire_CTerrace_FShadowmat6_mobjdesc"},
 {6,external_flag,0,"GrdPStadiumFire_CTerrace_FShadowmat7_mobjdesc"},
 {7,external_flag,0,"GrdPStadiumFire_ZGround_FShadowmat1_mobjdesc"},
 {8,external_flag,0,"GrdPStadiumFire_ZGround_FShadowmat2_mobjdesc"},
 {9,external_flag,0,"GrdPStadiumGrass_SYaguraB_GShadowmat10_mobjdesc"},
 {10,external_flag,0,"GrdPStadiumGrass_SyaguraA_GShadowmat11_mobjdesc"},
 {11,external_flag,0,"GrdPStadiumGrass_TreeB_GShadowmat8_mobjdesc"},
 {12,external_flag,0,"GrdPStadiumGrass_TreeB_GShadowmat9_mobjdesc"},
 {13,external_flag,0,"GrdPStadiumGrass_UGround_GShadowmat12_mobjdesc"},
 {14,external_flag,0,"GrdPStadiumGrass_UGround_GShadowmat13_mobjdesc"},
 {15,external_flag,0,"GrdPStadiumGrass_UGround_GShadowmat14_mobjdesc"},
 {16,external_flag,0,"GrdPStadiumGrass_UGround_GShadowmat15_mobjdesc"},
 {17,external_flag,0,"GrdPStadiumGrass_UGround_GShadowmat16_mobjdesc"},
 {18,local_flag,9564,{}},
 {19,local_flag,9404,{}},
 {20,local_flag,9856,{}},
 {21,local_flag,9696,{}},
 {22,local_flag,9272,{}},
 {23,local_flag,8996,{}},
 {24,external_flag,0,"GrdPStadiumRock_BStandA_RShadowmat2_mobjdesc"},
 {25,external_flag,0,"GrdPStadiumRock_BStandB_RShadowmat6_mobjdesc"},
 {26,external_flag,0,"GrdPStadiumRock_BStandB_RShadowmat7_mobjdesc"},
 {27,external_flag,0,"GrdPStadiumRock_YRock_RShadowmat3_mobjdesc"},
 {28,external_flag,0,"GrdPStadiumRock_ZGround_RShadowmat4_mobjdesc"},
 {29,external_flag,0,"GrdPStadiumRock_ZGround_RShadowmat5_mobjdesc"},
 {30,external_flag,0,"GrdPStadiumRock_ZGround_shadowmat1_mobjdesc"},
 {31,external_flag,0,"GrdPStadiumWater_ABox_WShadowmat14_mobjdesc"},
 {32,external_flag,0,"GrdPStadiumWater_BBox_WShadowmat15_mobjdesc"},
 {33,external_flag,0,"GrdPStadiumWater_BStand_WShadowmat10_mobjdesc"},
 {34,external_flag,0,"GrdPStadiumWater_BStand_WShadowmat8_mobjdesc"},
 {35,external_flag,0,"GrdPStadiumWater_BStand_WShadowmat9_mobjdesc"},
 {36,external_flag,0,"GrdPStadiumWater_CWing_WShadowmat3_mobjdesc"},
 {37,external_flag,0,"GrdPStadiumWater_CWing_WShadowmat4_mobjdesc"},
 {38,external_flag,0,"GrdPStadiumWater_CWing_WShadowmat5_mobjdesc"},
 {39,external_flag,0,"GrdPStadiumWater_CWing_WShadowmat6_mobjdesc"},
 {40,external_flag,0,"GrdPStadiumWater_CWing_WShadowmat7_mobjdesc"},
 {41,external_flag,0,"GrdPStadiumWater_YGround_WShadowmat11_mobjdesc"},
 {42,external_flag,0,"GrdPStadiumWater_YGround_WShadowmat12_mobjdesc"},
 {43,external_flag,0,"GrdPStadiumWater_YGround_WShadowmat13_mobjdesc"},
}};
const melee_web::DatNativeMapContract stadium_contract{
 10,stadium_animation_counts,stadium_resident_ids,stadium_external_references,
 stadium_animation_flag_consumers,stadium_flag_expectations
};
std::vector<uint8_t> read_bytes(const char* path){
 std::ifstream file(path,std::ios::binary);
 check(bool(file),"open local stage archive");
 return {std::istreambuf_iterator<char>(file),{}};
}
void write_be32(std::vector<uint8_t>& bytes,uint32_t offset,uint32_t value){
 check(size_t(offset)+4<=bytes.size(),"corrupt source pointer remains within test input");
 for(unsigned i=0;i<4;i++)bytes[offset+i]=uint8_t(value>>(24-8*i));
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
void stadium_map_trace(const char* path){
 auto bytes=read_bytes(path);
 auto archive=std::make_shared<melee_web::DatArchive>(bytes,melee_web::DatExternalPolicy::ResolveNull);
 melee_web::DatStage metadata(*archive);
 check(metadata.entries.size()==10&&metadata.flagged_object_table.count==44,
       "C0 authored Stadium map row/flag counts");
 for(const auto& expected:stadium_external_references){
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
   const auto light_counts=owner.source_light_counts();
   check(light_counts.size()==metadata.entries.size(),"C0 map source-light row count");
   for(const auto& entry:metadata.entries){
    uint32_t expected=0;
    if(entry.light_table_offset){
     const auto end=archive->next_target_offset(*entry.light_table_offset);
     for(;*entry.light_table_offset+expected*4<end;expected++)
      if(!archive->pointer(*entry.light_table_offset+expected*4,4))break;
     check(*entry.light_table_offset+expected*4<end&&
               !archive->pointer(*entry.light_table_offset+expected*4,4),
           "C0 authored Ground light list has a bounded null terminator");
    }
    check(light_counts[entry.index]==expected,"C0 map light row matches source table");
    if(entry.animation_flags_offset){
     const auto* flags=melee_web_test_native_stadium_flags(map,int(entry.index));
     const auto source=archive->range(*entry.animation_flags_offset,stadium_animation_counts[entry.index]);
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
  const auto retained=archive->data();
  check(retained.size()==bytes.size()&&std::equal(bytes.begin(),bytes.end(),retained.begin()),
        "C0 immutable input remains byte-identical");
 }

 // Contract failures are deterministic input-boundary checks and do not
 // widen the owner into a stage profile.
 auto wrong_count=stadium_contract;wrong_count.entry_count=9;
 expect_error([&]{melee_web::DatNativeMap rejected(archive,wrong_count);},"entry count");
 auto bad_counts=stadium_animation_counts;bad_counts[3]=65;
 auto bad_bound=stadium_contract;bad_bound.animation_consumer_counts=bad_counts;
 expect_error([&]{melee_web::DatNativeMap rejected(archive,bad_bound);},"animation consumer count");
 constexpr std::array<uint32_t,3> missing_resident={0,1,2};
 auto missing=stadium_contract;missing.resident_entry_ids=missing_resident;
 expect_error([&]{melee_web::DatNativeMap rejected(archive,missing);},"Unexpected local joint");
 auto wrong_imports=stadium_external_references;
 wrong_imports[0].symbol="GrdPStadiumWrong_TopN_joint";
 auto wrong_import=stadium_contract;wrong_import.external_references=wrong_imports;
 expect_error([&]{melee_web::DatNativeMap rejected(archive,wrong_import);},"symbol differs");
 auto wrong_flag_name=stadium_flag_expectations;
 wrong_flag_name[2].symbol="GrdPStadiumWrong_FShadowmat3_mobjdesc";
 auto wrong_flag_identity=stadium_contract;wrong_flag_identity.flagged_objects=wrong_flag_name;
 expect_error([&]{melee_web::DatNativeMap rejected(archive,wrong_flag_identity);},"flagged external slot or name");
 auto wrong_flag_slot=stadium_flag_expectations;wrong_flag_slot[2].index=3;
 auto wrong_flag_order=stadium_contract;wrong_flag_order.flagged_objects=wrong_flag_slot;
 expect_error([&]{melee_web::DatNativeMap rejected(archive,wrong_flag_order);},"expectations changed authored order");

 // Move one retained external-chain head to an otherwise unused null word.
 // The resulting archive remains parseable, but its flag identity is now at
 // the wrong source slot and must fail the map contract before hydration.
 const uint32_t external_flag_slot=*metadata.flagged_object_table.data_offset+2*4;
 size_t external_index=archive->external_symbols().size();
 for(size_t i=0;i<archive->external_symbols().size();i++){
  const auto& symbol=archive->external_symbols()[i];
  if(symbol.name==stadium_flag_expectations[2].symbol&&
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
 auto unowned_expectations=stadium_flag_expectations;
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
   check(light_counts.size()==stage_metadata.entries.size(),
         "Native stage source-light bounds differ from the authored entry table");
   for(const auto& entry:stage_metadata.entries){
    uint32_t expected=0;
    if(entry.light_table_offset){
     const auto end=archive->next_target_offset(*entry.light_table_offset);
     for(;*entry.light_table_offset+expected*4<end;expected++)
      if(!archive->pointer(*entry.light_table_offset+expected*4,4))break;
     check(*entry.light_table_offset+expected*4<end&&
               !archive->pointer(*entry.light_table_offset+expected*4,4),
           "Authored Ground light list lacks a bounded null terminator");
    }
    check(light_counts[entry.index]==expected,
          "Native Ground light bound differs from its null-terminated DAT table");
   }
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
