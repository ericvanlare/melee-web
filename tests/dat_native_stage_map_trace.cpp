#include "dat_native_stage.hpp"
#include "gameplay_bootstrap.h"
#include <fstream>
#include <iostream>
extern "C" int melee_web_test_native_stage_map(void*,void*);
static void check(bool c,const char* e){if(!c)throw std::runtime_error(e);}
int main(int argc,char** argv){try{
 check(argc==2,"expected local GrNLa.dat path");std::ifstream f(argv[1],std::ios::binary);check(bool(f),"open GrNLa.dat");
 std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
 auto archive=std::make_shared<melee_web::DatArchive>(bytes);char error[256];
 for(unsigned cycle=0;cycle<2;cycle++){
  check(melee_web_gameplay_startup(32*1024*1024,error,sizeof(error)),error);
  check(melee_web_native_world_enable(error,sizeof(error)),error);
  {
   melee_web::DatNativeStage stage(archive);
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
