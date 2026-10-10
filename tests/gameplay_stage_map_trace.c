#include "gameplay_stage_map.h"
#include "gameplay_bootstrap.h"
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
#include "gameplay_source_files.h"
#endif
#include <melee/gr/ground.h>
#include <melee/gr/grdatfiles.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/archive.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int c,const char* e){if(!c){fprintf(stderr,"%s\n",e);exit(1);}}
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
extern int melee_web_stadium_c1_grdatfiles_copy(UnkArchiveStruct slots[4],
        UnkArchiveStruct** ordinary_table,void** native_table,
        UnkArchiveStruct** effective_table);
static void copy_grdatfiles_rows(UnkArchiveStruct slots[4]){
 UnkArchiveStruct* ordinary=NULL;UnkArchiveStruct* effective=NULL;void* native=NULL;
 check(melee_web_stadium_c1_grdatfiles_copy(slots,&ordinary,&native,&effective)&&
       ordinary!=NULL,"existing C1 grDatFiles snapshot is unavailable");
}
static int same_grdatfiles_rows(const UnkArchiveStruct* a,const UnkArchiveStruct* b){
 for(unsigned i=0;i<4;i++)if(a[i].unk0!=b[i].unk0||a[i].unk4!=b[i].unk4||a[i].unk8!=b[i].unk8)return 0;
 return 1;
}
static int grdatfiles_stage_map_registry_lifetime(void){
 static const char filename[]="stage-map-control.dat";
 static const uint8_t source_bytes[]={0x53,0x54,0x41,0x47,0x45,0x01};
 char error[256]={0};
 MeleeWebSourceFileInput source={filename,source_bytes,sizeof(source_bytes)};
 MeleeWebSourceFileScope* source_scope=NULL;
 MeleeWebStageMap* owner=NULL;
 void* extra=NULL;
 void* replacement=NULL;
 void* preload=NULL;
 void* native_owner=NULL;
 check(melee_web_gameplay_startup(8*1024*1024,error,sizeof(error)),error);
 source_scope=melee_web_source_files_begin(&source,1,error,sizeof(error));check(source_scope!=NULL,error);
 size_t source_size=0;
 check(melee_web_source_file_size(filename,&source_size)&&source_size==sizeof(source_bytes),
       "typed source fixture is not registered at its exact supplied size");
 HSD_Joint joint={0};
 struct UnkStageDat_x8_t entries[2]={{0},{0}};entries[0].unk0=&joint;
 UnkStageDat map={0};map.unk8=entries;map.unkC=2;
 owner=melee_web_stage_map_publish(&map,error,sizeof(error));check(owner!=NULL,error);
 MeleeWebArchiveSymbol symbols[]={{filename,"map_head",&map}};
 check(melee_web_stage_map_set_public(owner,symbols,1,error,sizeof(error)),error);
 check(melee_web_stage_map_grdatfiles_arm(owner,error,sizeof(error)),error);
 check(melee_web_stage_map_grdatfiles_begin(),"StageMap grDatFiles begin did not accept the empty original table");

 /* Exercise the original reset and load functions over a deliberately small
  * typed catalog. The source bytes only select the registered preload path;
  * this tests the actual preload/map_head path, not the full E8 routine,
  * a parsed Stadium asset or a full stage load. */
 Ground_801BFFB0();
 UnkArchiveStruct rows[4];copy_grdatfiles_rows(rows);
 UnkArchiveStruct zero[4]={{0}};
 check(same_grdatfiles_rows(rows,zero),"original reset did not leave four empty rows");
 grDatFiles_801C6038((void*)filename,1,1);
 copy_grdatfiles_rows(rows);
 check(rows[0].unk0!=NULL&&rows[0].unk4==&map&&rows[0].unk8==0,
       "original grDatFiles loader did not publish its typed preload row");
 check(rows[1].unk0==NULL&&rows[1].unk4==NULL&&rows[1].unk8==0&&
       rows[2].unk0==NULL&&rows[2].unk4==NULL&&rows[2].unk8==0&&
       rows[3].unk0==NULL&&rows[3].unk4==NULL&&rows[3].unk8==0,
       "original grDatFiles loader changed an unused slot");
 check(melee_web_stage_map_grdatfiles_capture(),
       "StageMap grDatFiles capture did not accept the original registry-backed row");

 UnkArchiveStruct captured[4];memcpy(captured,rows,sizeof(captured));
 UnkArchiveStruct* stage_archives=(UnkArchiveStruct*)melee_web_stage_map_archives();
 check(stage_archives!=NULL&&stage_archives[0].unk0!=NULL&&stage_archives[0].unk4==&map,
       "StageMap native archive owner is missing");
 native_owner=stage_archives[0].unk0;
 preload=rows[0].unk0;
 check(melee_web_archive_sections_is_handle(native_owner)&&
       melee_web_archive_sections_is_handle(preload)&&
       melee_web_archive_sections_open_preloaded(filename)==preload,
       "typed native/preload handles are not both registered");
 check(HSD_ArchiveGetPublicAddress((HSD_Archive*)native_owner,"map_head")==&map&&
       HSD_ArchiveGetPublicAddress((HSD_Archive*)preload,"map_head")==&map,
       "native and preload handles do not resolve the same typed map head");

 extra=melee_web_archive_sections_open(filename);check(extra!=NULL,"could not open the extra scoped consumer");
 error[0]=0;
 check(!melee_web_stage_map_close(owner,error,sizeof(error))&&
       strcmp(error,"Original StageMap archive scope has another open handle")==0,
       "extra scoped consumer did not trigger the StageMap close refusal");
 copy_grdatfiles_rows(rows);
 check(same_grdatfiles_rows(rows,captured),
       "extra-handle refusal mutated one of the four original rows");
 check(melee_web_archive_sections_is_handle(native_owner)&&
       melee_web_archive_sections_is_handle(preload)&&
       melee_web_archive_sections_open_preloaded(filename)==preload&&
       HSD_ArchiveGetPublicAddress((HSD_Archive*)preload,"map_head")==&map,
       "extra-handle refusal changed the currently registered opaque owners");
 melee_web_archive_sections_release(extra);extra=NULL;

 /* A valid replacement handle is temporarily presented in the StageMap's
  * public native-owner slot. The captured native registry incarnation must
  * reject it before retiring the original preload row. */
 replacement=melee_web_archive_sections_open(filename);check(replacement!=NULL,"could not open a replacement native handle");
 check(replacement!=native_owner,"replacement open unexpectedly reused a live handle pointer");
 stage_archives[0].unk0=replacement;
 error[0]=0;
 check(!melee_web_stage_map_close(owner,error,sizeof(error))&&
       strcmp(error,"Original grDatFiles preload ownership or incarnation changed")==0,
       "replacement native owner did not trigger the captured-incarnation refusal");
 copy_grdatfiles_rows(rows);
 check(same_grdatfiles_rows(rows,captured),
       "replacement-owner refusal mutated one of the four original rows");
 check(melee_web_archive_sections_is_handle(native_owner)&&
       melee_web_archive_sections_is_handle(replacement)&&
       melee_web_archive_sections_is_handle(preload)&&
       melee_web_archive_sections_open_preloaded(filename)==preload&&
       HSD_ArchiveGetPublicAddress((HSD_Archive*)native_owner,"map_head")==&map&&
       HSD_ArchiveGetPublicAddress((HSD_Archive*)preload,"map_head")==&map,
       "replacement-owner refusal changed a registered handle");
 stage_archives[0].unk0=native_owner;
 melee_web_archive_sections_release(replacement);replacement=NULL;

 error[0]=0;
 check(melee_web_stage_map_close(owner,error,sizeof(error)),error);owner=NULL;
 copy_grdatfiles_rows(rows);
 check(same_grdatfiles_rows(rows,zero),
       "successful StageMap close did not retire and zero all four original rows");
 check(!melee_web_archive_sections_is_handle(native_owner)&&
       !melee_web_archive_sections_is_handle(preload)&&
       !melee_web_archive_sections_open_preloaded(filename),
       "successful StageMap close left a scoped native or preload handle registered");
 check(melee_web_stage_map_archives()==NULL&&!grDatFiles_801C6330(0),
       "successful StageMap close left its map owner or original lookup published");
 check(melee_web_source_files_end(source_scope,error,sizeof(error)),error);source_scope=NULL;
 check(melee_web_gameplay_shutdown(error,sizeof(error)),error);
 puts("Synthetic typed-catalog StageMap/original grDatFiles retirement control passed");
 return 0;
}
#endif
static int unregistered_light_abort(void){
 char error[256]={0};int registered_descriptor=0,unregistered_descriptor=0;
 int found=0;uint8_t flags=0;HSD_Joint joint={0};
 struct UnkStageDat_x8_t entries[2]={{0},{0}};entries[1].unk0=&joint;
 UnkStageDat map={0};map.unk8=entries;map.unkC=2;
 check(melee_web_gameplay_startup(8*1024*1024,error,sizeof(error)),error);
 MeleeWebStageMap* h=melee_web_stage_map_publish(&map,error,sizeof(error));check(h!=NULL,error);
 MeleeWebMapLightOverride overrides[]={{&registered_descriptor,1,0xe0}};
 check(melee_web_stage_map_set_overrides(h,overrides,1,error,sizeof(error)),error);
 (void)melee_web_stage_map_lookup_override(&unregistered_descriptor,&found,&flags);
 return 0;
}
int main(int argc,char** argv){char error[256];
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 if(argc==2&&strcmp(argv[1],"--grdatfiles-retirement-control")==0)return grdatfiles_stage_map_registry_lifetime();
#endif
 if(argc==2&&strcmp(argv[1],"--unregistered-light")==0)return unregistered_light_abort();
 if(argc!=1){fprintf(stderr,"unknown gameplay_stage_map_trace argument\n");return 2;}
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
  int light_descriptor=0,found=0;uint8_t flags=0;
  MeleeWebMapLightOverride overrides[]={{&light_descriptor,1,0xe0}};
  check(melee_web_stage_map_set_overrides(h,overrides,1,error,sizeof(error)),error);
  check(melee_web_stage_map_lookup_override(&light_descriptor,&found,&flags)&&found==1&&flags==0xe0,"registered light identity returns its exact found/flags");
  check(HSD_ArchiveGetPublicAddress(a->unk0,"map_head")==&map&&HSD_ArchiveGetPublicAddress(a->unk0,"extra_joint")==&joint,"original public queries preserve descriptor identity");
  check(HSD_ArchiveGetPublicAddress(a->unk0,"absent")==NULL,"source-absent public remains null");
  HSD_GObj* g=GObj_Create(HSD_GOBJ_CLASS_STAGE,5,0);check(g!=NULL,"real source stage object allocation");stage_info.map_gobjs[1]=g;
  check(!melee_web_stage_map_close(h,error,sizeof(error)),"live stage prevents borrowed descriptor release");
  check(melee_web_stage_map_lookup_override(&light_descriptor,&found,&flags)&&found==1&&flags==0xe0,"refused live-stage close preserves light descriptor identity");
  stage_info.map_gobjs[1]=NULL;
  check(!melee_web_stage_map_close(h,error,sizeof(error)),"unregistered source stage instance still owns descriptors");
  check(HSD_ArchiveGetPublicAddress(a->unk0,"extra_joint")==&joint,"unregistered stage rejection preserves public descriptor identity");
  HSD_GObjPLink_80390228(g);
  HSD_GObj* light=GObj_Create(HSD_GOBJ_CLASS_GROUND,3,0);check(light!=NULL,"real source map light owner allocation");
  check(!melee_web_stage_map_close(h,error,sizeof(error)),"live map light prevents borrowed animation descriptor release");
  check(melee_web_stage_map_lookup_override(&light_descriptor,&found,&flags)&&found==1&&flags==0xe0,"refused live-light close preserves light descriptor identity");
  check(HSD_ArchiveGetPublicAddress(a->unk0,"extra_joint")==&joint,"map light rejection preserves public descriptor identity");
  HSD_GObjPLink_80390228(light);
  void* consumer=melee_web_archive_sections_open("checked-stage.dat");
  check(!melee_web_stage_map_close(h,error,sizeof(error)),"external source handle prevents stage catalog release");
  check(melee_web_stage_map_lookup_override(&light_descriptor,&found,&flags)&&found==1&&flags==0xe0,"refused consumer close preserves light descriptor identity");
  check(HSD_ArchiveGetPublicAddress(a->unk0,"extra_joint")==&joint,"rejected close preserves the original source archive handle");
  melee_web_archive_sections_release(consumer);
  check(melee_web_stage_map_close(h,error,sizeof(error)),error);
  check(!melee_web_stage_map_lookup_override(&light_descriptor,&found,&flags),"closed map owner no longer serves borrowed light identities");
  check(!melee_web_stage_map_archives()&&!grDatFiles_801C6330(1),"native map removal restores original storage lookup");
  check(melee_web_gameplay_shutdown(error,sizeof(error)),error);
 }
 puts("Original native map publication/lookup/lifetime/restart passed");return 0;
}
