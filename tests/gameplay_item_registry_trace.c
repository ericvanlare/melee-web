#include "dat_item_registry.h"
#include <melee/it/it_3F14.h>
#include <melee/it/it_26B1.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"CHECK failed at %d: %s\n",__LINE__,#c);abort();}}while(0)
int main(void){
 char error[256];void* table[MELEE_WEB_ITEM_REGISTRY_COUNT]={0};Article seed={0},fire={0},cape={0};table[0]=&seed;
 Article** saved=it_804D6D38;
 for(int pass=0;pass<2;pass++){
  MeleeWebItemRegistry* h=melee_web_item_registry_begin(table,MELEE_WEB_ITEM_REGISTRY_COUNT,error,sizeof(error));CHECK(h);
  CHECK(!melee_web_item_registry_begin(table,MELEE_WEB_ITEM_REGISTRY_COUNT,error,sizeof(error)));
  void* article;CHECK(melee_web_item_registry_lookup(h,It_Kind_Kuriboh,&article,error,sizeof(error))&&article==&seed);
  CHECK(melee_web_item_registry_lookup(h,It_Kind_Mario_Fire,&article,error,sizeof(error))&&article==NULL);
  it_8026B3F8(&fire,It_Kind_Mario_Fire);it_8026B3F8(&cape,It_Kind_Mario_Cape);
  CHECK(melee_web_item_registry_lookup(h,It_Kind_Mario_Fire,&article,error,sizeof(error))&&article==&fire);
  CHECK(melee_web_item_registry_lookup(h,It_Kind_Mario_Cape,&article,error,sizeof(error))&&article==&cape);
  CHECK(!melee_web_item_registry_lookup(h,It_PKind_Start,&article,error,sizeof(error)));
  CHECK(!melee_web_item_registry_lookup(h,It_Kind_Kuriboh-1,&article,error,sizeof(error)));
  CHECK(melee_web_item_registry_end(h,error,sizeof(error)));CHECK(it_804D6D38==saved);
 }
 puts("Original item registration writes, source range, scoped restore and restart passed");return 0;
}
