#include "gameplay_stage_map.h"
#include "gameplay_bootstrap.h"
#include <melee/gr/ground.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/jobj.h>
#include <stdio.h>
#include <stdlib.h>
static void check(int c,const char* e){if(!c){fprintf(stderr,"%s\n",e);exit(1);}}
int main(void){char error[256];
 for(unsigned cycle=0;cycle<2;cycle++){
  check(melee_web_gameplay_startup(8*1024*1024,error,sizeof(error)),error);
  HSD_Joint joint={0};struct UnkStageDat_x8_t entries[2]={{0},{0}};entries[1].unk0=&joint;
  UnkStageDat map={0};map.unk8=entries;map.unkC=2;
  MeleeWebStageMap* h=melee_web_stage_map_publish(&map,error,sizeof(error));check(h!=NULL,error);
  check(!melee_web_stage_map_publish(&map,error,sizeof(error)),"duplicate map owner rejected");
  UnkArchiveStruct* a=grDatFiles_GetArchive();
  check(a&&a->unk4==&map&&a->unk0==NULL,"native map does not fabricate a raw HSD archive handle");
  check(!grDatFiles_801C6330(-1)&&!grDatFiles_801C6330(0)&&grDatFiles_801C6330(1)==a&&!grDatFiles_801C6330(2),"original exact native map lookup bounds");
  HSD_GObj* g=GObj_Create(HSD_GOBJ_CLASS_STAGE,5,0);check(g!=NULL,"real source stage object allocation");stage_info.map_gobjs[1]=g;
  check(!melee_web_stage_map_close(h,error,sizeof(error)),"live stage prevents borrowed descriptor release");
  stage_info.map_gobjs[1]=NULL;HSD_GObjPLink_80390228(g);
  check(melee_web_stage_map_close(h,error,sizeof(error)),error);
  check(!melee_web_stage_map_archives()&&!grDatFiles_801C6330(1),"native map removal restores original storage lookup");
  check(melee_web_gameplay_shutdown(error,sizeof(error)),error);
 }
 puts("Original native map publication/lookup/lifetime/restart passed");return 0;
}
