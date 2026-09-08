#include "dat_native_menu.hpp"
#include "dat_menu_support.hpp"
#include "dat_sis.hpp"
#include "gameplay_archive_sections.h"
#include "gameplay_audio_bank.hpp"
#include "gameplay_audio_bank_transport.h"
#include "gameplay_audio_stream_asset.hpp"
#include "gameplay_bootstrap.h"
#include "gameplay_font_atlas.h"
#include "hsd_texture_bounds.h"
#include <filesystem>
#include <fstream>
#include <iostream>
extern "C" int melee_web_test_css_enter(void);
extern "C" int melee_web_test_css_tick(void);
extern "C" int melee_web_test_css_exit(void);
extern "C" void melee_web_test_css_forget(void);
static std::vector<uint8_t> read(const std::filesystem::path& path){
 std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Required local CSS fixture unavailable");
 return {(std::istreambuf_iterator<char>(file)),{}};
}
int main(int argc,char** argv){try{
 if(argc!=3)throw std::runtime_error("Expected local menu and audio bundle directories");
 const std::filesystem::path menu_dir=argv[1],audio_dir=argv[2];
 auto load=[&](const char* name){return std::make_shared<melee_web::DatArchive>(read(menu_dir/name));};
 auto css_archive=load("MnSlChr.usd"),sis_archive=load("SdSlChr.usd"),extra_archive=load("MnExtAll.usd");
 auto icons_archive=load("LbMcGame.usd"),card_archive=load("NtMemAc.usd");
 auto sem=read(audio_dir/"smash2.sem"),main_bank=read(audio_dir/"main.ssm");
 auto coefficients=read(audio_dir/"dsp_coef.bin"),music_bytes=read(menu_dir/"menu01.hps");
 auto font_bytes=read(audio_dir/"sislib_font.bin");
 const char* bank_names[]={"main.ssm","mario.ssm","nr_select.ssm","nr_title.ssm","nr_name.ssm","pokemon.ssm","end.ssm"};
 std::vector<std::vector<uint8_t>> bank_bytes;
 for(unsigned i=0;i<7;i++)bank_bytes.push_back(read((i<2?audio_dir:menu_dir)/bank_names[i]));
 std::vector<std::span<const uint8_t>> bank_views;for(auto& bytes:bank_bytes)bank_views.push_back(bytes);
 for(unsigned cycle=0;cycle<2;cycle++){
  char error[256];
  if(!melee_web_gameplay_startup(32*1024*1024,error,sizeof(error)) ||
     !melee_web_native_world_enable(error,sizeof(error)))throw std::runtime_error(error);
  melee_web::DatNativeMenu css(css_archive,melee_web::NativeMenuKind::Characters);
  melee_web::DatSis sis(sis_archive,"SIS_SelCharData");
  melee_web::DatMenuSupport icons(icons_archive,melee_web::DatMenuSupportKind::CardIcons);
  melee_web::DatMenuSupport card(card_archive,melee_web::DatMenuSupportKind::CardScene);
  std::vector<MeleeWebArchiveSymbol> symbols={
   {"MnSlChr.usd","MnSelectChrDataTable",css.descriptor()},
   {"SdSlChr.usd","SIS_SelCharData",sis.descriptor()},
   {"LbMcGame.usd","MemCardIconData",icons.descriptor()},
   {"NtMemAc.usd","ScNtcCommon_scene_data",card.descriptor()},
  };
  for(const auto& symbol:extra_archive->public_symbols())
   symbols.push_back({"MnExtAll.usd",symbol.name.c_str(),nullptr});
  auto* scope=melee_web_archive_sections_register_heap(symbols.data(),symbols.size(),error,sizeof(error));
  if(!scope)throw std::runtime_error(error);
  auto* font=melee_web_font_atlas_register(font_bytes.data(),font_bytes.size(),error,sizeof(error));
  if(!font)throw std::runtime_error(error);
  {
   melee_web::GameplayAudioBank audio(sem,bank_views,coefficients);
   auto* registry=melee_web_audio_residency_create(error,sizeof(error));if(!registry)throw std::runtime_error(error);
   for(unsigned i=0;i<7;i++){
    const std::string name=std::string("/audio/us/")+bank_names[i];
    MeleeWebAudioResidencyAsset asset={name.c_str(),bank_bytes[i].data(),bank_bytes[i].size(),100+int(i)};
    if(!melee_web_audio_residency_register(registry,&asset,error,sizeof(error)))throw std::runtime_error(error);
   }
   if(!melee_web_audio_bank_transport_begin(audio.get(),registry,"/audio/us/smash2.sem",error,sizeof(error)))throw std::runtime_error(error);
   if(!melee_web_audio_enable_effects(audio.get(),error,sizeof(error)))throw std::runtime_error(error);
   {
   melee_web::GameplayAudioStream music(audio.get(),"/audio/menu01.hps",music_bytes);
   if(!melee_web_test_css_enter())throw std::runtime_error("Original CSS did not create its objects");
   float pcm[1068];
   for(unsigned tick=0;tick<120;tick++){
    if(!melee_web_test_css_tick() || !melee_web_audio_render(audio.get(),pcm,tick%3==2?534:533,error,sizeof(error)))
     throw std::runtime_error("Original CSS tick/audio failed");
   }
   if(!melee_web_test_css_exit())throw std::runtime_error("Neutral CSS unexpectedly requested another scene");
   }
   if(!melee_web_audio_bank_transport_end(error,sizeof(error)) ||
      !melee_web_audio_residency_destroy(registry,error,sizeof(error)))throw std::runtime_error(error);
  }
  if(!melee_web_gameplay_shutdown(error,sizeof(error)))throw std::runtime_error(error);
  melee_web_test_css_forget();
  if(melee_web_texture_bounds_live())throw std::runtime_error("Texture bounds outlived CSS scene");
  if(!melee_web_archive_sections_close(scope,error,sizeof(error)) ||
     !melee_web_font_atlas_close(font,error,sizeof(error)))throw std::runtime_error(error);
 }
 std::cout<<"Original CSS enter, 120 neutral input/scheduler/audio ticks and exit passed in two worlds; rendering/selection not tested\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
