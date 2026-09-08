#include "dat_item_registry_native.hpp"
#include "gameplay_article_data.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace melee_web;
void check(bool b,const char* msg){if(!b)throw std::runtime_error(msg);}
int main(int argc,char** argv){try{
 check(argc==2,"Provide local ItCo.usd path");std::ifstream f(argv[1],std::ios::binary);check(bool(f),"Open local item archive");
 std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
 auto archive=std::make_shared<DatArchive>(bytes,DatExternalPolicy::PreserveUnresolved);
 check(archive->external_symbols().size()==6,"US item external symbols retained");
 for(int pass=0;pass<2;pass++){
  DatItemRegistryNative data(archive);char error[256];uint32_t present=0;
  auto* registry=melee_web_item_registry_begin(data.articles(),MELEE_WEB_ITEM_REGISTRY_COUNT,error,sizeof(error));check(registry,error);
  for(uint32_t i=0;i<MELEE_WEB_ITEM_REGISTRY_COUNT;i++){
   void* out;check(melee_web_item_registry_lookup(registry,MELEE_WEB_ITEM_REGISTRY_FIRST_KIND+i,&out,error,sizeof(error)),error);
   check(out==data.articles()[i],"Exact original registry pointer identity");
   if(out){++present;check(data.unresolved_masks()[i]!=0,"Registration-only root stays explicitly unresolved");check(melee_web_article_unresolved(out)==data.unresolved_masks()[i],"Stored Article graph readiness mask");}
  }
  check(present==8,"US item registry eight original Article roots");check(melee_web_item_registry_end(registry,error,sizeof(error)),error);
 }
 std::cout<<"US item registry118 slots,8 typed registration roots,6 unresolved external symbols, restore/restart passed; item creation unsupported\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
