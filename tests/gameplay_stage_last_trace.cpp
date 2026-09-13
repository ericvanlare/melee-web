#include "gameplay_world.hpp"
#include "gameplay_match_context.h"
#include "gameplay_render.h"
#include "dat_archive.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <charconv>
using namespace melee_web;
extern "C" int melee_web_test_stage_last_state(unsigned*,unsigned*);
extern "C" void melee_web_test_stage_last_animation(void);
static void check(int c,const char* e){if(!c){std::cerr<<e<<'\n';throw DatError(e);}}
static std::vector<uint8_t> read(const std::filesystem::path& p){
 std::ifstream f(p,std::ios::binary|std::ios::ate);auto length=f.tellg();
 if(length<=0||length>64*1024*1024)throw DatError("Invalid local asset: "+p.string());
 f.seekg(0);std::vector<uint8_t> b(static_cast<size_t>(length));if(!f.read(reinterpret_cast<char*>(b.data()),length))throw DatError("Truncated local asset");return b;
}
int main(int argc,char** argv){try{
 if(argc<2||argc>3)throw DatError("Expected local runtime asset directory and optional tick limit");
 unsigned tick_limit=3600;
 if(argc==3){std::string_view value(argv[2]);auto parsed=std::from_chars(value.data(),value.data()+value.size(),tick_limit);
  if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||tick_limit<3600||tick_limit>60000)throw DatError("Stage tick limit must be 3600..60000");}
 RuntimeFiles files;
 for(const char* name:{"PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","ItCo.usd","EfMrData.dat","EfCoData.dat","PdPm.dat","LbRb.dat","sislib_font.bin"})files[name]=read(std::filesystem::path(argv[1])/name);
 char error[256];
 for(unsigned cycle=0;cycle<2;cycle++){
  GameplayWorld world(files);
  MeleeWebMatchSettings match_settings{};match_settings.player={0,0,4,{0,world.floor_height(0)+1,0},1};match_settings.camera_subjects=70;match_settings.random_seed=0x13579bdf;
  auto* match=melee_web_match_begin(&match_settings,world.collision(),error,sizeof(error));check(match!=nullptr,error);
  check(melee_web_match_create_fighter(match,error,sizeof(error)),error);
  MeleeWebRenderSettings render_settings{640,480,{0,35,190},{0,5,0},45,1,2000,(UINT64_C(1)<<3)|(UINT64_C(1)<<5)};
  auto* camera=melee_web_render_begin_match(&render_settings,error,sizeof(error));check(camera!=nullptr,error);
  world.enable_full_stage();unsigned previous=0,transitions=0,seen=0,restarts=0;
  try {
  for(unsigned tick=0;tick<tick_limit;tick++){
   check(melee_web_match_step(match,1,error,sizeof(error)),error);
   unsigned objects=0,state=0;check(melee_web_test_stage_last_state(&objects,&state),"Original FD stage registry/state invalid");
   seen|=1u<<state;
   if(state!=previous){if(state==1)++restarts;std::cout<<"FD cycle="<<cycle<<" tick="<<tick<<" state="<<state<<" objects="<<objects<<'\n';previous=state;++transitions;}
  }
  melee_web_test_stage_last_animation();
  check(transitions>=3,"Original stage scheduler did not complete its first transition animation");
  if(tick_limit>=36000)check((seen&0x3fffe)==0x3fffe&&restarts>=2,"Original FD full background cycle did not complete");
  world.verify_immutable_archives();world.end_stage();
  }catch(const std::exception& e){std::cerr<<"Stage trace: "<<e.what()<<'\n';throw;}
  unsigned objects=1,state=1;check(!melee_web_test_stage_last_state(&objects,&state)&&objects==0,"Original FD teardown left map objects");
  check(melee_web_render_end(camera,error,sizeof(error)),error);check(melee_web_match_end(match,error,sizeof(error)),error);world.close();
 }
 std::cout<<"Original FD OnInit, scheduled background transitions, camera, immutable assets and two-world teardown passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
