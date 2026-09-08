#include "dat_native_menu.hpp"
#include "gameplay_archive_sections.h"
#include "gameplay_audio_bank.hpp"
#include "gameplay_audio_stream_asset.hpp"
#include "gameplay_bootstrap.h"
#include "hsd_texture_bounds.h"
#include <fstream>
#include <iostream>
#include <filesystem>
extern "C" int melee_web_test_sss_enter(void);
extern "C" int melee_web_test_sss_tick(void);
extern "C" int melee_web_test_sss_exit(void);
static std::vector<uint8_t> read(const std::filesystem::path& p){
 std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Required local fixture unavailable");
 return {(std::istreambuf_iterator<char>(f)),{}};
}
int main(int argc,char** argv){try{
 if(argc!=3)throw std::runtime_error("Expected local menu and audio bundle directories");
 const std::filesystem::path menu_dir=argv[1],audio_dir=argv[2];
 auto bytes=read(menu_dir/"MnSlMap.usd"),sem=read(audio_dir/"smash2.sem"),bank=read(audio_dir/"main.ssm");
 auto coefficients=read(audio_dir/"dsp_coef.bin"),music_bytes=read(menu_dir/"menu01.hps");
 auto archive=std::make_shared<melee_web::DatArchive>(bytes);
 for(unsigned cycle=0;cycle<2;cycle++){
  char error[256];
  if(!melee_web_gameplay_startup(32*1024*1024,error,sizeof(error)) ||
     !melee_web_native_world_enable(error,sizeof(error)))throw std::runtime_error(error);
  melee_web::DatNativeMenu menu(archive,melee_web::NativeMenuKind::Stages);
  MeleeWebArchiveSymbol symbol={"MnSlMap.usd","MnSelectStageDataTable",menu.descriptor()};
  auto* scope=melee_web_archive_sections_register(&symbol,1,error,sizeof(error));if(!scope)throw std::runtime_error(error);
  {
   melee_web::GameplayAudioBank audio(sem,{bank},coefficients);
   if(!melee_web_audio_enable_effects(audio.get(),error,sizeof(error)))throw std::runtime_error(error);
   melee_web::GameplayAudioStream music(audio.get(),"/audio/menu01.hps",music_bytes);
   if(!melee_web_test_sss_enter())throw std::runtime_error("Original SSS did not create its objects");
   float pcm[1068];
   for(unsigned tick=0;tick<120;tick++){
    if(!melee_web_test_sss_tick() || !melee_web_audio_render(audio.get(),pcm, tick%3==2?534:533,error,sizeof(error)))
     throw std::runtime_error("Original SSS tick/audio failed");
   }
   if(!melee_web_test_sss_exit())throw std::runtime_error("Neutral SSS unexpectedly selected a stage");
  }
  if(!melee_web_gameplay_shutdown(error,sizeof(error)))throw std::runtime_error(error);
  if(melee_web_texture_bounds_live())throw std::runtime_error("Texture bounds outlived SSS scene");
  if(!melee_web_archive_sections_close(scope,error,sizeof(error)))throw std::runtime_error(error);
 }
 std::cout<<"Original SSS enter, 120 neutral input/scheduler/audio ticks and exit passed in two worlds; rendering/selection not tested\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
