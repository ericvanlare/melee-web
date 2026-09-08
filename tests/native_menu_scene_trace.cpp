#include "dat_native_menu.hpp"
#include "dat_sis.hpp"
#include "dat_menu_support.hpp"
extern "C" int melee_web_test_card_scene_consume(void);
extern "C" void melee_web_test_card_scene_forget(void);
#include "hsd_texture_bounds.h"
#include <cstring>
extern "C" int melee_web_test_texture_bounds(unsigned);
#include "gameplay_archive_sections.h"
#include "gameplay_bootstrap.h"
extern "C" int melee_web_test_menu_scene_consume(void*, unsigned);
extern "C" int melee_web_test_sis_consume(void*, unsigned);
#include <fstream>
#include <iostream>
int main(int argc,char** argv){try{
 if(argc==1 || (argc==2 && (std::strcmp(argv[1],"--bad-image-index")==0 || std::strcmp(argv[1],"--bad-palette-index")==0))){
  char error[256];
  if(!melee_web_gameplay_startup(4*1024*1024,error,sizeof(error)))throw std::runtime_error(error);
  if(!melee_web_native_world_enable(error,sizeof(error)))throw std::runtime_error(error);
  const unsigned bad=argc==1?0:std::strcmp(argv[1],"--bad-image-index")==0?1:10;
  if(!melee_web_test_texture_bounds(bad))throw std::runtime_error("Original texture bounds guard failed");
  if(!melee_web_test_sis_consume(nullptr,0))throw std::runtime_error("Original SIS bytecode consumer failed");
  if(!melee_web_gameplay_shutdown(error,sizeof(error)))throw std::runtime_error(error);
  std::cout<<"Original SIS big-endian layout and style-stack trace passed\n";return 0;
 }
 if(argc<2 || (argc>4 && argc!=6))throw std::runtime_error("Expected local CSS, optional SIS and SSS asset paths");
 std::ifstream file(argv[1],std::ios::binary);if(!file)throw std::runtime_error("Menu file unavailable");
 std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
 auto archive=std::make_shared<melee_web::DatArchive>(bytes);
 for(unsigned cycle=0;cycle<2;cycle++){
  char error[256];
  if(!melee_web_gameplay_startup(32*1024*1024,error,sizeof(error)))throw std::runtime_error(error);
  if(!melee_web_native_world_enable(error,sizeof(error)))throw std::runtime_error(error);
  melee_web::DatNativeMenu menu(archive,melee_web::NativeMenuKind::Characters);
  if(menu.model_count()!=9)throw std::runtime_error("CSS model count mismatch");
  MeleeWebArchiveSymbol symbol={"MnSlChr.usd","MnSelectChrDataTable",menu.descriptor()};
  auto* scope=melee_web_archive_sections_register(&symbol,1,error,sizeof(error));if(!scope)throw std::runtime_error(error);
  void* handle=melee_web_archive_sections_open("MnSlChr.usd");
  if(melee_web_archive_sections_public(handle,"MnSelectChrDataTable")!=menu.descriptor())throw std::runtime_error("Menu publication mismatch");
  if(!melee_web_test_menu_scene_consume(menu.descriptor(),menu.model_count()))throw std::runtime_error("Original HSD scene descriptor consumers failed");
  if(argc>=3){
   std::ifstream sisfile(argv[2],std::ios::binary);if(!sisfile)throw std::runtime_error("SIS file unavailable");
   std::vector<uint8_t> sisbytes((std::istreambuf_iterator<char>(sisfile)),{});
   melee_web::DatSis sis(std::make_shared<melee_web::DatArchive>(sisbytes),"SIS_SelCharData");
   MeleeWebArchiveSymbol sis_symbol={"SdSlChr.usd","SIS_SelCharData",sis.descriptor()};
   auto* sis_scope=melee_web_archive_sections_register(&sis_symbol,1,error,sizeof(error));
   if(!sis_scope)throw std::runtime_error(error);
   if(!melee_web_test_sis_consume(sis.descriptor(),sis.entry_count()))throw std::runtime_error("Original SIS layout/stack consumers failed");
   if(!melee_web_archive_sections_close(sis_scope,error,sizeof(error)))throw std::runtime_error(error);
   std::cout<<"SIS entries="<<sis.entry_count()<<"; original layout and style stack passed\n";
  }
  std::unique_ptr<melee_web::DatNativeMenu> stage_menu;
  if(argc>=4){
   std::ifstream stagefile(argv[3],std::ios::binary);if(!stagefile)throw std::runtime_error("SSS file unavailable");
   std::vector<uint8_t> stagebytes((std::istreambuf_iterator<char>(stagefile)),{});
   stage_menu=std::make_unique<melee_web::DatNativeMenu>(std::make_shared<melee_web::DatArchive>(stagebytes),melee_web::NativeMenuKind::Stages);
   if(stage_menu->model_count()!=12 || !melee_web_test_menu_scene_consume(stage_menu->descriptor(),12))throw std::runtime_error("Original SSS descriptor consumers failed");
   std::cout<<"SSS twelve model groups with original material/shape animation consumers passed\n";
  }
  std::unique_ptr<melee_web::DatMenuSupport> card_icons,card_scene;
  MeleeWebArchiveSections* card_scope=nullptr;
  if(argc==6){
   auto load=[](const char* path){
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Card support file unavailable");
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),{});
    return std::make_shared<melee_web::DatArchive>(raw);
   };
   card_icons=std::make_unique<melee_web::DatMenuSupport>(load(argv[4]),melee_web::DatMenuSupportKind::CardIcons);
   card_scene=std::make_unique<melee_web::DatMenuSupport>(load(argv[5]),melee_web::DatMenuSupportKind::CardScene);
   MeleeWebArchiveSymbol cards[]={
    {"LbMcGame.usd","MemCardIconData",card_icons->descriptor()},
    {"NtMemAc.usd","ScNtcCommon_scene_data",card_scene->descriptor()},
   };
   card_scope=melee_web_archive_sections_register_heap(cards,2,error,sizeof(error));
   if(!card_scope)throw std::runtime_error(error);
   if(!melee_web_test_card_scene_consume())throw std::runtime_error("Original card scene consumers failed");
   if(melee_web_archive_sections_close(card_scope,error,sizeof(error)))throw std::runtime_error("Card scope released before source heap teardown");
  }
  if(!melee_web_gameplay_shutdown(error,sizeof(error)))throw std::runtime_error(error);
  if(card_scope){
   melee_web_test_card_scene_forget();
   if(!melee_web_archive_sections_close(card_scope,error,sizeof(error)))throw std::runtime_error(error);
   std::cout<<"Original card archive loader, camera/model/animation consumers and scene-heap handle teardown passed\n";
  }
  if(melee_web_texture_bounds_live())throw std::runtime_error("Native texture bounds survived world teardown");
  melee_web_archive_sections_release(handle);
  if(!melee_web_archive_sections_close(scope,error,sizeof(error)))throw std::runtime_error(error);
 }
 std::cout<<"Original CSS nine complete model/animation descriptors loaded and animated by HSD, with camera/lights/fog and two world lifetimes; CSS callbacks/rendering not tested\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
