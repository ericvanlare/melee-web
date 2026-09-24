#include "gameplay_stage_map.h"
#include "gameplay_bootstrap.h"
#include <melee/gr/ground.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/archive.h>
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
  MeleeWebArchiveSymbol symbols[]={{"checked-stage.dat","map_head",&map},{"checked-stage.dat","extra_joint",&joint}};
  MeleeWebArchiveSymbol mixed[]={{"first.dat","map_head",&map},{"second.dat","extra_joint",&joint}};
  check(!melee_web_stage_map_set_public(h,mixed,2,error,sizeof(error)),"mixed archive catalog rejected atomically");
  check(a->unk0==NULL,"rejected catalog leaves archive unpublished");
  check(melee_web_stage_map_set_public(h,symbols,2,error,sizeof(error)),error);
  check(!melee_web_stage_map_set_public(h,symbols,2,error,sizeof(error)),"duplicate public owner rejected");
  check(HSD_ArchiveGetPublicAddress(a->unk0,"map_head")==&map&&HSD_ArchiveGetPublicAddress(a->unk0,"extra_joint")==&joint,"original public queries preserve descriptor identity");
  check(HSD_ArchiveGetPublicAddress(a->unk0,"absent")==NULL,"source-absent public remains null");
  HSD_GObj* g=GObj_Create(HSD_GOBJ_CLASS_STAGE,5,0);check(g!=NULL,"real source stage object allocation");stage_info.map_gobjs[1]=g;
  check(!melee_web_stage_map_close(h,error,sizeof(error)),"live stage prevents borrowed descriptor release");
  stage_info.map_gobjs[1]=NULL;
  check(!melee_web_stage_map_close(h,error,sizeof(error)),"unregistered source stage instance still owns descriptors");
  check(HSD_ArchiveGetPublicAddress(a->unk0,"extra_joint")==&joint,"unregistered stage rejection preserves public descriptor identity");
  HSD_GObjPLink_80390228(g);
  HSD_GObj* light=GObj_Create(HSD_GOBJ_CLASS_GROUND,3,0);check(light!=NULL,"real source map light owner allocation");
  check(!melee_web_stage_map_close(h,error,sizeof(error)),"live map light prevents borrowed animation descriptor release");
  check(HSD_ArchiveGetPublicAddress(a->unk0,"extra_joint")==&joint,"map light rejection preserves public descriptor identity");
  HSD_GObjPLink_80390228(light);
  void* consumer=melee_web_archive_sections_open("checked-stage.dat");
  check(!melee_web_stage_map_close(h,error,sizeof(error)),"external source handle prevents stage catalog release");
  check(HSD_ArchiveGetPublicAddress(a->unk0,"extra_joint")==&joint,"rejected close preserves the original source archive handle");
  melee_web_archive_sections_release(consumer);
  check(melee_web_stage_map_close(h,error,sizeof(error)),error);
  check(!melee_web_stage_map_archives()&&!grDatFiles_801C6330(1),"native map removal restores original storage lookup");
  check(melee_web_gameplay_shutdown(error,sizeof(error)),error);
 }
 puts("Original native map publication/lookup/lifetime/restart passed");return 0;
}
