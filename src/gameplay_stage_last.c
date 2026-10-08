#include "gameplay_stage_last.h"
#include "gameplay_stage_map.h"
#include "gameplay_effect_runtime.h"
#include "gameplay_stage_context.h"
#include "gameplay_bootstrap.h"
#include "gameplay_collision.h"
#include "gameplay_stage_profile.h"
#include <melee/gr/ground.h>
#include <melee/gr/grlast.h>
#include <melee/gr/stage.h>
#include <melee/gr/types.h>
#include <melee/ft/ftdevice.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern int melee_web_stage_selection_begin(int);
extern int melee_web_stage_selection_end(void);
extern int melee_web_ground_remove_unmapped(HSD_GObj*);
extern void melee_web_ground_remove_camera(HSD_GObj*);
struct MeleeWebStageLast {
    StageInfo saved;
    struct ftDeviceUnk3 device1[1],device3[1];
    struct ftDeviceUnk5 device2[2];
    struct ftDeviceUnk4 device4;
    int count1,count2;
    void* yaku;
    const MeleeWebStageProfile* definition;
    HSD_GObj* manager;
    HSD_GObj* map_lights;
    uint64_t generation;
    int source_ordered;
    int lights_adopted;
    const void* collision_map;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    MeleeWebStadiumDisplayOwner* stadium_display_owner;
#endif
};
static MeleeWebStageLast* active;
static int fail(char* e,size_t n,const char* m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
static MeleeWebStageLast* begin_stage(const MeleeWebStageProfile* definition,void* yaku,MeleeWebEffectBank* map_bank,int defer_start,int source_ordered,int on_init_diagnostic,MeleeWebStageLast** retained_owner,char* e,size_t n){
 MeleeWebEffectBankStats bank;
 if(retained_owner&&*retained_owner!=NULL){fail(e,n,"Stage retained-owner output slot must be empty");return NULL;}
 if(!definition){fail(e,n,"Stage has no complete source callback profile");return NULL;}
 if(definition->diagnostic_only&&!on_init_diagnostic){fail(e,n,"Diagnostic-only stage profile requires its explicit OnInit boundary");return NULL;}
 if(on_init_diagnostic&&(!definition->diagnostic_only||!source_ordered||definition->stage_kind!=St_Kind_PStadium)){fail(e,n,"OnInit-only stage boundary requires the diagnostic source-ordered Stadium profile");return NULL;}
 if(!map_bank&&!definition->allow_absent_particle_bank){fail(e,n,"Stage requires its actual registered particle bank64");return NULL;}
 if(map_bank&&!melee_web_effect_bank_stats(map_bank,&bank,e,n)||map_bank&&(bank.bank!=64||!bank.particle_bank_ready)){fail(e,n,"Stage requires its actual registered particle bank64");return NULL;}
 if(active||!yaku||!definition->source||!melee_web_effect_runtime_active()||!melee_web_stage_map_archives()||(!source_ordered&&(!stage_info.param||stage_info.grkind!=definition->ground_kind))){fail(e,n,"Stage requires original effects and published native map/numeric stage contexts");return NULL;}
 for(unsigned i=0;i<sizeof(stage_info.map_gobjs)/sizeof(stage_info.map_gobjs[0]);i++)if(stage_info.map_gobjs[i]){fail(e,n,"Stage requires an empty source stage object registry");return NULL;}
 for(HSD_GObj* obj=((HSD_GObj**)HSD_GObj_Entities)[5];obj;obj=obj->next)
  if(obj->classifier==HSD_GOBJ_CLASS_STAGE){fail(e,n,"Stage requires exclusive ownership of source stage objects");return NULL;}
 if(Ground_801C498C()){fail(e,n,"Stage requires exclusive ownership of selected map lights");return NULL;}
 if(source_ordered&&!melee_web_collision_source_available()){fail(e,n,"Source stage construction requires unowned collision storage");return NULL;}
 MeleeWebStageLast* h=calloc(1,sizeof(*h));if(!h){fail(e,n,"Cannot allocate stage ownership scope");return NULL;}
 h->saved=stage_info;h->generation=melee_web_gameplay_stats().generation;
 h->definition=definition;h->source_ordered=source_ordered;
 memcpy(h->device1,ft_80459A68,sizeof(h->device1));memcpy(h->device2,ftDevice_BuryThings,sizeof(h->device2));memcpy(h->device3,ft_80459A8C,sizeof(h->device3));h->device4=ft_804D6578;h->count1=ft_804D6570;h->count2=ftDevice_BuryThingCount;
 if(!melee_web_ground_map_storage_begin()){free(h);fail(e,n,"Original Ground collision-state storage is already owned");return NULL;}
 if(!melee_web_stage_selection_begin(definition->stage_kind)){melee_web_ground_map_storage_end();free(h);fail(e,n,"Original selected stage is already owned");return NULL;}
 h->yaku=definition->exchange_yakumono?definition->exchange_yakumono(yaku):NULL;stage_info.yakumono_param=yaku;active=h;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 if(on_init_diagnostic&&!melee_web_stadium_display_owner_prepare(&h->stadium_display_owner,e,n)){
  char prepare_error[160];snprintf(prepare_error,sizeof(prepare_error),"%s",e&&n?e:"Stadium display owner preparation failed");
  if(!melee_web_stage_last_end(h,e,n)){if(retained_owner)*retained_owner=h;return NULL;}
  fail(e,n,prepare_error);return NULL;
 }
#endif
 if(source_ordered){
  /* The retail scene enters Ground's state buffer and stage archive before
   * Ground_801C0800 loads collision, lights and the stage callback. */
  Stage_802251E8((StKind)definition->stage_kind,NULL);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
  if(on_init_diagnostic&&!melee_web_stadium_display_owner_bind_source(h->stadium_display_owner,e,n)){
   char bind_error[160];snprintf(bind_error,sizeof(bind_error),"%s",e&&n?e:"Stadium source SIS bind failed");
   if(!melee_web_stage_last_end(h,e,n)){if(retained_owner)*retained_owner=h;return NULL;}
   fail(e,n,bind_error);return NULL;
  }
#endif
  Stage_8022524C();
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
  if(on_init_diagnostic&&!melee_web_stadium_display_owner_capture(
       h->stadium_display_owner,Ground_GetMapGObj(1),e,n)){
   if(retained_owner)*retained_owner=h;
   return NULL;
  }
#endif
  h->collision_map=stage_info.coll_data;
 }else{
  /* Isolated native-stage probes already own collision and item setup. */
  extern void melee_web_ground_load_map_lights(void);
  melee_web_ground_load_map_lights();
 }
 h->map_lights=Ground_801C498C();
 if(!h->map_lights){if(on_init_diagnostic){if(retained_owner)*retained_owner=h;fail(e,n,"Original selected map-light owner was not created");return NULL;}melee_web_stage_last_end(h,NULL,0);fail(e,n,"Original selected map-light owner was not created");return NULL;}
 if(source_ordered){
  if(!melee_web_stage_lights_adopt_source(h->map_lights,e,n)){if(on_init_diagnostic){if(retained_owner)*retained_owner=h;return NULL;}melee_web_stage_last_end(h,NULL,0);return NULL;}
  h->lights_adopted=1;
 }else definition->source->on_init();
 for(unsigned i=0;i<definition->required_map_count;i++)if(definition->required_map_ids[i]>=sizeof(stage_info.map_gobjs)/sizeof(stage_info.map_gobjs[0])||!stage_info.map_gobjs[definition->required_map_ids[i]]){if(on_init_diagnostic){if(retained_owner)*retained_owner=h;fail(e,n,"Original stage initializer did not create every required map object");return NULL;}melee_web_stage_last_end(h,NULL,0);fail(e,n,"Original stage initializer did not create every required map object");return NULL;}
 if(on_init_diagnostic){if(retained_owner)*retained_owner=h;ok(e,n);return h;}
 Stage_80225298();
 if(!defer_start)Stage_802252E4((StKind)definition->stage_kind,NULL);
 ok(e,n);return h;
}
MeleeWebStageLast* melee_web_stage_begin_kind(int stage_kind,void* yaku,MeleeWebEffectBank* bank,int defer_start,int source_ordered,char* e,size_t n){
 const MeleeWebStageProfile* definition=melee_web_stage_profile(stage_kind);
 return begin_stage(definition,yaku,bank,defer_start,source_ordered,0,NULL,e,n);
}
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
MeleeWebStageLast* melee_web_stage_begin_kind_on_init_diagnostic(int stage_kind,void* yaku,MeleeWebEffectBank* bank,MeleeWebStageLast** owner_out,char* e,size_t n){
 if(owner_out==NULL){fail(e,n,"OnInit-only boundary requires a retained-owner output slot");return NULL;}
 if(*owner_out!=NULL){fail(e,n,"OnInit-only retained-owner output slot must be empty");return NULL;}
 if(stage_kind!=St_Kind_PStadium){fail(e,n,"OnInit-only stage boundary is limited to the diagnostic Stadium profile");return NULL;}
 const MeleeWebStageProfile* definition=melee_web_stage_profile(stage_kind);
 if(!definition||!definition->diagnostic_only){fail(e,n,"Diagnostic Stadium source profile is unavailable");return NULL;}
 return begin_stage(definition,yaku,bank,1,1,1,owner_out,e,n);
}
#endif
MeleeWebStageLast* melee_web_stage_last_begin(void* yaku,MeleeWebEffectBank* bank,char* e,size_t n){
 return begin_stage(melee_web_stage_profile(St_Kind_Last),yaku,bank,0,0,0,NULL,e,n);
}
MeleeWebStageLast* melee_web_stage_last_begin_intro(void* yaku,MeleeWebEffectBank* bank,char* e,size_t n){
 return begin_stage(melee_web_stage_profile(St_Kind_Last),yaku,bank,1,0,0,NULL,e,n);
}
int melee_web_stage_last_end(MeleeWebStageLast* h,char* e,size_t n){
 if(!h)return ok(e,n);
 if(h!=active||h->generation!=melee_web_gameplay_stats().generation)return fail(e,n,"Stage scope lost its original world ownership");
 if(HSD_GObj_804D781C||HSD_GObj_804D7814)return fail(e,n,"Stage teardown must run outside source object callbacks");
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 if(h->stadium_display_owner){
  if(!melee_web_stadium_display_owner_end(h->stadium_display_owner,e,n))return 0;
  h->stadium_display_owner=NULL;
 }
#endif
 /* Ready completion owns creation of this source scheduler. Early unload
  * before Ready is allowed to have none; multiple schedulers are an error. */
 h->manager=NULL;
 for(HSD_GObj* obj=((HSD_GObj**)HSD_GObj_Entities)[5];obj;obj=obj->next){
  if(obj->classifier==HSD_GOBJ_CLASS_STAGE&&!obj->user_data){
   if(h->manager)return fail(e,n,"Original stage started more than once");
   h->manager=obj;
  }
 }
 if(h->manager){HSD_GObjPLink_80390228(h->manager);h->manager=NULL;}
 /* x18 is a shared camera reference on some stages: Fountain's two
  * platforms and reflection surface all reference the same camera. End its
  * lifetime once after all consumers; never let per-map removal free it
  * while another source object still references it. No source tick runs here. */
 HSD_GObj* cameras[64];size_t camera_count=0;
 for(HSD_GObj* obj=((HSD_GObj**)HSD_GObj_Entities)[5];obj;obj=obj->next){
  if(obj->classifier!=HSD_GOBJ_CLASS_STAGE||!obj->user_data)continue;
  Ground* gp=obj->user_data;
  if(!gp->x18)continue;
  size_t i=0;for(;i<camera_count;i++)if(cameras[i]==gp->x18)break;
  if(i==camera_count){
   if(camera_count==sizeof(cameras)/sizeof(cameras[0]))return fail(e,n,"Stage camera ownership exceeds source map capacity");
   cameras[camera_count++]=gp->x18;
  }
 }
 for(HSD_GObj* obj=((HSD_GObj**)HSD_GObj_Entities)[5];obj;obj=obj->next)
  if(obj->classifier==HSD_GOBJ_CLASS_STAGE&&obj->user_data)((Ground*)obj->user_data)->x18=NULL;
 /* A map descriptor may create several live objects (Fountain creates two
  * map-4 platforms). The source registry retains only the latest one. Retire
  * every instance in the established reverse-map order while descriptor and
  * collision owners are still alive. Rescan after each callback can unlink. */
 for(int i=(int)(sizeof(stage_info.map_gobjs)/sizeof(stage_info.map_gobjs[0]))-1;i>=0;i--){
  for(;;){
   HSD_GObj* found=NULL;
   for(HSD_GObj* obj=((HSD_GObj**)HSD_GObj_Entities)[5];obj;obj=obj->next)
    if(obj->classifier==HSD_GOBJ_CLASS_STAGE&&obj->user_data&&((Ground*)obj->user_data)->map_id==i){found=obj;break;}
   if(!found)break;
   Ground_801C4A08(found);
  }
 }
 for(;;){
  HSD_GObj* found=NULL;
  for(HSD_GObj* obj=((HSD_GObj**)HSD_GObj_Entities)[5];obj;obj=obj->next)
   if(obj->classifier==HSD_GOBJ_CLASS_STAGE){found=obj;break;}
  if(!found)break;
  if(!melee_web_ground_remove_unmapped(found))return fail(e,n,"Unexpected source stage object remains during teardown");
 }
 for(size_t i=0;i<camera_count;i++)melee_web_ground_remove_camera(cameras[i]);
 if(h->map_lights){
  if(Ground_801C498C()!=h->map_lights)return fail(e,n,"Original selected map-light owner was replaced");
  if(h->lights_adopted&&!melee_web_stage_lights_retire_source(h->map_lights,e,n))return 0;
  h->lights_adopted=0;
  HSD_GObjPLink_80390228(h->map_lights);h->map_lights=NULL;
 }
 if(h->collision_map&&!melee_web_collision_retire_unadopted(h->collision_map,e,n))return 0;
 if(!melee_web_ground_map_storage_end())return fail(e,n,"Original stage objects remain during collision-state release");
 memcpy(ft_80459A68,h->device1,sizeof(h->device1));memcpy(ftDevice_BuryThings,h->device2,sizeof(h->device2));memcpy(ft_80459A8C,h->device3,sizeof(h->device3));ft_804D6578=h->device4;ft_804D6570=h->count1;ftDevice_BuryThingCount=h->count2;
 if(h->definition&&h->definition->exchange_yakumono)h->definition->exchange_yakumono(h->yaku);
 if(!melee_web_stage_selection_end())return fail(e,n,"Original selected stage lost ownership");
 stage_info=h->saved;active=NULL;free(h);return ok(e,n);
}
