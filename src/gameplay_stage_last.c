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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
#include "gameplay_stadium_start.h"
#include "gameplay_hud.h"
#include "gameplay_source_memory_runtime.h"
#include <melee/gr/grpstadium.h>
#include <melee/gm/gm_1879.h>
#include <melee/if/if_2F6E.h>
#include <melee/if/if_2F72.h>
#include <sysdolphin/baselib/gobjproc.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern int melee_web_stage_selection_begin(int);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
extern int melee_web_stage_selection_preflight(int);
#endif
extern int melee_web_stage_selection_end(void);
extern int melee_web_ground_remove_unmapped(HSD_GObj*);
extern void melee_web_ground_remove_camera(HSD_GObj*);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
static const HSD_GObjEvent stadium_pending_callbacks[]={fn_801D13C8,fn_801D11E4};
extern uint32_t melee_web_match_source_frames(void);
#endif
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
    MeleeWebStadiumGenerator* stadium_generator;
    MeleeWebMatchContext* stadium_camera_owner;
    CmSubject* stadium_subject;
    int stadium_started;
    int stadium_ready_route,stadium_ready_armed,stadium_loaded;
    void* stadium_ready_object;
    void* stadium_ready_proc;
    void* stadium_headers[ARRAY_SIZE(stadium_pending_callbacks)];
    MeleeWebSourceMemoryAllocation stadium_header_leases[ARRAY_SIZE(stadium_pending_callbacks)];
    MeleeWebSourceMemoryContext stadium_start_context;
    MeleeWebStadiumManagerView stadium_manager;
    MeleeWebSourceMemoryAllocation stadium_manager_lease;
    MeleeWebSourceMemoryAllocation stadium_manager_proc_lease;
    MeleeWebStadiumMap2BufferOwner stadium_map2_buffer_owner;
    MeleeWebStadiumGoAlignmentSnapshot stadium_go_alignment;
#endif
};
static MeleeWebStageLast* active;
static int fail(char* e,size_t n,const char* m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
static int stage_report_on_init_bind_failure(MeleeWebStageLast* h,
        MeleeWebStageLast** retained_owner,const char* bind_error,char* e,size_t n){
 char cleanup_error[160]={0};
 if(!melee_web_stage_last_end(h,cleanup_error,sizeof(cleanup_error))){
  if(retained_owner)*retained_owner=h;
  if(e&&n){
   const int each=(int)(n>33?(n-33)/2:0);
   snprintf(e,n,"bind failed: %.*s; cleanup refused: %.*s",each,
            bind_error?bind_error:"Stadium source SIS bind failed",each,
            cleanup_error);
  }
  return 0;
 }
 fail(e,n,bind_error?bind_error:"Stadium source SIS bind failed");
 return 1;
}

int melee_web_stage_last_on_init_bind_refusal_controls(void){
 MeleeWebStageLast* h;
 MeleeWebStageLast* retained=NULL;
 char error[256]={0};
 uint64_t generation=melee_web_gameplay_generation();
 if(active||!generation||!HSD_GObj_Entities||HSD_GObj_804D781C||
    HSD_GObj_804D7814)return 0;
 h=calloc(1,sizeof(*h));if(!h)return 0;
 h->generation=generation;
 h->definition=melee_web_stage_profile(St_Kind_PStadium);
 h->source_ordered=1;
 if(!h->definition||!h->definition->diagnostic_only||
    !melee_web_stadium_display_owner_prepare(&h->stadium_display_owner,
                                             error,sizeof(error))||
    !melee_web_stadium_display_owner_arm_source_journal(
        h->stadium_display_owner,error,sizeof(error))){
  free(h);return 0;
 }
 active=h;
 melee_web_stadium_display_owner_note_source_event(
     MELEE_WEB_STADIUM_SOURCE_EVENT_STAGE_E8,-1,NULL);
 if(stage_report_on_init_bind_failure(
        h,&retained,"Synthetic asset-free SIS bind refusal",error,
        sizeof(error))||retained!=h||active!=h||
    h->stadium_display_owner==NULL||
    strstr(error,"Synthetic asset-free SIS bind refusal")==NULL||
    strstr(error,"Partial Stadium display ownership must remain reachable")==NULL)
  return 0;
 /* The retained h and opaque display owner intentionally remain rooted by
  * active until this isolated control process exits. */
 return 1;
}
#endif
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
 if(on_init_diagnostic&&!melee_web_stadium_display_owner_arm_source_journal(h->stadium_display_owner,e,n)){
  char journal_error[160];snprintf(journal_error,sizeof(journal_error),"%s",e&&n?e:"Stadium source journal could not be armed");
  if(!melee_web_stage_last_end(h,e,n)){if(retained_owner)*retained_owner=h;return NULL;}
  fail(e,n,journal_error);return NULL;
 }
#endif
 if(source_ordered){
  /* The retail scene enters Ground's state buffer and stage archive before
   * Ground_801C0800 loads collision, lights and the stage callback. */
  Stage_802251E8((StKind)definition->stage_kind,NULL);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
  if(on_init_diagnostic&&!melee_web_stadium_display_owner_bind_source(h->stadium_display_owner,e,n)){
   char bind_error[160];snprintf(bind_error,sizeof(bind_error),"%s",e&&n?e:"Stadium source SIS bind failed");
   stage_report_on_init_bind_failure(h,retained_owner,bind_error,e,n);
   return NULL;
  }
#endif
  Stage_8022524C();
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
  if(on_init_diagnostic&&!melee_web_stadium_display_owner_capture(
       h->stadium_display_owner,Ground_GetMapGObj(1),
       &h->stadium_map2_buffer_owner,e,n)){
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
/* A GObj is a borrowed identity here: verify its live source registry before
 * reading Ground or comparing the authored display/queued callback fields. */
static int stadium_maps_live(MeleeWebStageLast* h,char* e,size_t n)
{
 MeleeWebStadiumMap2BufferOwner* maps=&h->stadium_map2_buffer_owner;
 const struct {int id;HSD_GObj* object;} expected[]={
  {0,maps->map0_ground},{1,maps->display_ground},
  {2,maps->map2_ground},{5,maps->nested_map5_ground}};
 if(!maps->captured||!HSD_GObj_Entities)return fail(e,n,"Stadium OnInit map owner is incomplete");
 for(size_t i=0;i<ARRAY_SIZE(expected);++i){
  HSD_GObj* found=NULL;
  for(HSD_GObj* g=((HSD_GObj**)HSD_GObj_Entities)[5];g;g=g->next)
   if(g==expected[i].object){found=g;break;}
  if(!found||found!=stage_info.map_gobjs[expected[i].id]||
     found->classifier!=HSD_GOBJ_CLASS_STAGE||!found->user_data||
     ((Ground*)found->user_data)->map_id!=expected[i].id)
   return fail(e,n,"Stadium original map identity changed before callback/retirement");
 }
 return 1;
}
/* SDK leases describe backing storage; source-journal/proc/root checks
 * independently identify the original manager's logical lifetime. */
static int stadium_manager_storage_preflight(MeleeWebStageLast* h,char* e,size_t n){
 MeleeWebSourceMemoryAllocation object={0},proc={0};
 MeleeWebSourceMemoryContext context;
 if(melee_web_source_memory_context_read(&context)!=MELEE_WEB_SOURCE_MEMORY_READ_OK)
  return fail(e,n,"Stadium manager source context is unavailable");
 MeleeWebSourceMemoryReadStatus os=melee_web_source_memory_allocation_read(h->stadium_manager.object,&object);
 MeleeWebSourceMemoryReadStatus ps=melee_web_source_memory_allocation_read(h->stadium_manager.proc,&proc);
 fprintf(stderr,"STADIUM_MANAGER_LEASE object=%p proc=%p object_status=%d proc_status=%d "
  "object_live=%u proc_live=%u world=%llu/%llu heap=%d/%d requested=%u/%u generation=%llu/%llu "
  "expected_world=%llu expected_heap=%d expected_requested=%u/%u expected_generation=%llu/%llu\n",
  (void*)h->stadium_manager.object,(void*)h->stadium_manager.proc,os,ps,object.live,proc.live,
  (unsigned long long)object.world_generation,(unsigned long long)proc.world_generation,
  object.source_heap_handle,proc.source_heap_handle,object.requested_bytes,proc.requested_bytes,
  (unsigned long long)object.allocation_generation,(unsigned long long)proc.allocation_generation,
  (unsigned long long)h->stadium_manager_lease.world_generation,h->stadium_manager_lease.source_heap_handle,
  h->stadium_manager_lease.requested_bytes,h->stadium_manager_proc_lease.requested_bytes,
  (unsigned long long)h->stadium_manager_lease.allocation_generation,
  (unsigned long long)h->stadium_manager_proc_lease.allocation_generation);
 if(os!=MELEE_WEB_SOURCE_MEMORY_READ_OK||ps!=MELEE_WEB_SOURCE_MEMORY_READ_OK||!object.live||!proc.live||
    object.requested_bytes!=sizeof(HSD_GObj)||proc.requested_bytes!=sizeof(HSD_GObjProc)||
    object.world_generation!=context.world_generation||proc.world_generation!=context.world_generation||
    object.source_heap_handle!=context.source_heap_handle||proc.source_heap_handle!=context.source_heap_handle||
    object.world_generation!=h->stadium_manager_lease.world_generation||
    proc.world_generation!=h->stadium_manager_proc_lease.world_generation||
    object.source_heap_handle!=h->stadium_manager_lease.source_heap_handle||
    proc.source_heap_handle!=h->stadium_manager_proc_lease.source_heap_handle||
    object.requested_bytes!=h->stadium_manager_lease.requested_bytes||
    proc.requested_bytes!=h->stadium_manager_proc_lease.requested_bytes||
    object.allocation_generation!=h->stadium_manager_lease.allocation_generation||
    proc.allocation_generation!=h->stadium_manager_proc_lease.allocation_generation)
  return fail(e,n,"Original Stadium manager storage lease changed");
 return melee_web_stadium_manager_view_preflight(&h->stadium_manager,e,n);
}
static void stadium_go_alignment_fail(MeleeWebStageLast* h,unsigned reason)
{
 if(h && h->stadium_go_alignment.armed && !h->stadium_go_alignment.failure)
  h->stadium_go_alignment.failure=reason;
}
static void stadium_go_alignment_stamp(MeleeWebStageLast* h,
        MeleeWebStadiumGoAlignmentEvent* event,int callback_index,int source_branch,
        int gate,int mode,int remap,int callback_identity,int hud_enabled,
        void* object,void* proc)
{
 MeleeWebGameplayStats stats=melee_web_gameplay_stats();
 event->generation=stats.generation;event->world_ticks=stats.ticks;
 event->source_frame=melee_web_match_source_frames();
 event->sequence=++h->stadium_go_alignment.next_sequence;
 event->object_identity=(uintptr_t)object;event->proc_identity=(uintptr_t)proc;
 event->callback_index=callback_index;event->source_branch=source_branch;
 event->map2_gate=gate;event->display_mode=mode;event->remap_branch=remap;
 event->callback_identity=callback_identity;event->hud_enabled=hud_enabled;
}
static int stadium_go_alignment_has_no_events(const MeleeWebStageLast* h)
{
 return h && !h->stadium_go_alignment.stage_before.sequence &&
        !h->stadium_go_alignment.stage_after.sequence &&
        !h->stadium_go_alignment.go_after.sequence &&
        !h->stadium_go_alignment.hud_after.sequence;
}
static int stadium_go_alignment_live(const MeleeWebStageLast* h)
{
 if(!h||h!=active||!h->stadium_go_alignment.armed||
    h->generation!=melee_web_gameplay_stats().generation||
    h->stadium_go_alignment.generation!=h->generation||
    !h->stadium_ready_route||!h->definition||!h->definition->diagnostic_only||
    h->definition->stage_kind!=St_Kind_PStadium||!h->source_ordered)return 0;
 /* Before the first Ready callback, a completed tick may legitimately leave
  * the exact OnInit owner unstarted. Keep that partial/zero-latch state
  * observable; after original OnStart, require its captured owner phase. */
 return (h->stadium_started==1&&h->stadium_ready_armed==1&&
         stadium_go_alignment_has_no_events(h))||
        (h->stadium_started==2&&h->stadium_ready_armed==0);
}
static int stadium_go_alignment_source_owner_live(
        const MeleeWebStageLast* h,int source_started)
{
 MeleeWebSourceMemoryContext context={0};
 MeleeWebSourceMemoryAllocation object={0},proc={0};
 MeleeWebGameplayStats stats=melee_web_gameplay_stats();
 if(!h||h!=active||!melee_web_gameplay_world_exists()||
    !melee_web_source_memory_healthy()||
    melee_web_source_memory_context_read(&context)!=MELEE_WEB_SOURCE_MEMORY_READ_OK||
    context.world_generation!=stats.generation||context.world_generation!=h->generation||
    context.world_generation!=h->stadium_start_context.world_generation||
    context.source_heap_handle!=h->stadium_start_context.source_heap_handle)
  return 0;
 if(!source_started){
  return h->stadium_started==1&&!h->stadium_manager.object&&
         !h->stadium_manager.proc&&!h->stadium_manager_lease.live&&
         !h->stadium_manager_proc_lease.live;
 }
 if(h->stadium_started!=2||
    melee_web_source_memory_allocation_read(h->stadium_manager.object,&object)!=
        MELEE_WEB_SOURCE_MEMORY_READ_OK||
    melee_web_source_memory_allocation_read(h->stadium_manager.proc,&proc)!=
        MELEE_WEB_SOURCE_MEMORY_READ_OK||
    !object.live||!proc.live||
    object.world_generation!=context.world_generation||
    proc.world_generation!=context.world_generation||
    object.world_generation!=h->stadium_manager_lease.world_generation||
    proc.world_generation!=h->stadium_manager_proc_lease.world_generation||
    object.source_heap_handle!=context.source_heap_handle||
    proc.source_heap_handle!=context.source_heap_handle||
    object.source_heap_handle!=h->stadium_manager_lease.source_heap_handle||
    proc.source_heap_handle!=h->stadium_manager_proc_lease.source_heap_handle||
    object.requested_bytes!=h->stadium_manager_lease.requested_bytes||
    proc.requested_bytes!=h->stadium_manager_proc_lease.requested_bytes||
    object.allocation_generation!=h->stadium_manager_lease.allocation_generation||
    proc.allocation_generation!=h->stadium_manager_proc_lease.allocation_generation)
  return 0;
 return 1;
}
static int stadium_go_alignment_stage_note(MeleeWebStageLast* h,int after,
        char* e,size_t n)
{
 if(!h)return fail(e,n,"Stadium GO trace lost its StageLast owner");
 if(!h->stadium_go_alignment.armed)return 1;
 MeleeWebStadiumGoAlignmentEvent* event=after?&h->stadium_go_alignment.stage_after:
                                                    &h->stadium_go_alignment.stage_before;
 const unsigned required_armed=after?2U:1U;
 if(event->sequence||!h||h!=active||
    h->generation!=melee_web_gameplay_stats().generation||
    !h->stadium_ready_route||!h->definition||!h->definition->diagnostic_only||
    h->definition->stage_kind!=St_Kind_PStadium||!h->source_ordered||
    h->stadium_started!=(after?2:1)||h->stadium_ready_armed!=required_armed||
    !h->stadium_ready_object||!h->stadium_ready_proc||
    !melee_web_hud_stadium_ready_context(h->stadium_ready_object,h->stadium_ready_proc)||
    !stadium_maps_live(h,e,n)){
  stadium_go_alignment_fail(h,1);
  if(e&&n&&!*e)snprintf(e,n,"Stadium GO trace lost its exact Stage Ready callback");
  return 0;
 }
 const int gate=((Ground*)h->stadium_map2_buffer_owner.map2_ground->user_data)->u.stadium.xC4_b0;
 if(gate!=(after?0:1)){
  stadium_go_alignment_fail(h,2);
  return fail(e,n,"Stadium GO trace did not observe the authored map-2 gate transition");
 }
 stadium_go_alignment_stamp(h,event,3,-1,gate,-1,-1,1,-1,
                            h->stadium_ready_object,h->stadium_ready_proc);
 return 1;
}
static int stadium_go_alignment_current_display(const MeleeWebStageLast* h,
        int* gate,int* mode,int* remap)
{
 if(!stadium_go_alignment_source_owner_live(h,h&&h->stadium_started==2)||
    !h->stadium_ready_route||!h->definition||!h->definition->diagnostic_only||
    h->definition->stage_kind!=St_Kind_PStadium||!h->source_ordered||
    !melee_web_stage_selection_preflight(St_Kind_PStadium)||
    stage_info.grkind!=h->definition->ground_kind||
    !melee_web_stadium_display_owner_go_alignment_view(
        h->stadium_display_owner,&h->stadium_map2_buffer_owner,
        h->stadium_started==2,gate,mode))return 0;
 *remap=gm_8018841C()?1:0;
 return 1;
}
int melee_web_stage_last_stadium_go_alignment_arm(
        MeleeWebStageLast* h,int expected_branch,char* e,size_t n)
{
 if(!h||h!=active||h->generation!=melee_web_gameplay_stats().generation||
    !h->definition||!h->definition->diagnostic_only||
    h->definition->stage_kind!=St_Kind_PStadium||!h->source_ordered||
    !h->stadium_ready_route||h->stadium_ready_armed!=1||h->stadium_started!=1||
    h->stadium_go_alignment.armed||expected_branch<0||expected_branch>1||
    HSD_GObj_804D781C||HSD_GObj_804D7838||HSD_GObj_804D7830||
    HSD_GObj_804D7814||HSD_GObj_804D7818)
  return fail(e,n,"Stadium GO trace arm requires the idle, started Ready owner");
 MeleeWebGameplayStats stats=melee_web_gameplay_stats();
 if(stats.ticks!=0||stats.generation!=h->generation||
    !melee_web_stage_selection_preflight(St_Kind_PStadium)||
    stage_info.grkind!=h->definition->ground_kind||
    !stadium_go_alignment_source_owner_live(h,0))
  return fail(e,n,"Stadium GO trace arm requires its zero-tick original map owner");
 int gate=-1,mode=-1,remap=-1;
 if(!stadium_go_alignment_current_display(h,&gate,&mode,&remap))
  return fail(e,n,"Stadium GO trace arm requires its exact live display owner");
 if(gate!=1)return fail(e,n,"Stadium GO trace arm requires the original closed map-2 gate");
 memset(&h->stadium_go_alignment,0,sizeof(h->stadium_go_alignment));
 h->stadium_go_alignment.armed=1;
 h->stadium_go_alignment.generation=stats.generation;
 h->stadium_go_alignment.armed_world_ticks=stats.ticks;
 h->stadium_go_alignment.armed_source_frame=melee_web_match_source_frames();
 h->stadium_go_alignment.expected_branch=(uint32_t)expected_branch;
 return ok(e,n);
}
int melee_web_stage_last_stadium_go_alignment_snapshot(
        const MeleeWebStageLast* h,MeleeWebStadiumGoAlignmentSnapshot* out,
        char* e,size_t n)
{
 int gate=-1,mode=-1,remap=-1;
 const MeleeWebGameplayStats stats=melee_web_gameplay_stats();
 if(!h||h!=active||!out||h->generation!=melee_web_gameplay_stats().generation||
    !h->stadium_go_alignment.armed||
    (HSD_GObj_804D781C||HSD_GObj_804D7838||HSD_GObj_804D7830||
     HSD_GObj_804D7814||HSD_GObj_804D7818))
  return fail(e,n,"Stadium GO trace snapshot requires its live armed StageLast owner");
 if(!stadium_go_alignment_live(h)||
    stats.ticks<h->stadium_go_alignment.armed_world_ticks||
    !stadium_go_alignment_current_display(h,&gate,&mode,&remap))
  return fail(e,n,"Stadium GO trace snapshot lost its live idle Stadium owner/world phase");
 if(h->stadium_go_alignment.failure){
  static const char* const failures[]={"none","Stage Ready callback identity changed",
   "map-2 gate phase changed","GO owner/display/mode changed","GO branch differed from the selected rules",
   "HUD callback branch differed from GO","HUD owner/callback identity changed",
   "HUD did not enable its original display"};
  unsigned code=h->stadium_go_alignment.failure;
  return fail(e,n,code<sizeof(failures)/sizeof(failures[0])?failures[code]:"Stadium GO trace failed");
 }
 *out=h->stadium_go_alignment;return ok(e,n);
}
void melee_web_stage_last_stadium_go_alignment_note_go(int source_branch)
{
 MeleeWebStageLast* h=active;
 if(!h||!h->stadium_go_alignment.armed)return;
 MeleeWebStadiumGoAlignmentEvent* event=&h->stadium_go_alignment.go_after;
 void* object=HSD_GObj_804D781C;void* proc=HSD_GObj_804D7838;
 int gate=-1,mode=-1,remap=-1;
 if(event->sequence||!stadium_go_alignment_live(h)||
    !h->stadium_go_alignment.stage_before.sequence||
    !h->stadium_go_alignment.stage_after.sequence||
    !object||!proc||!melee_web_hud_stadium_ready_context(object,proc)||
    !melee_web_stage_selection_preflight(St_Kind_PStadium)||
    stage_info.grkind!=h->definition->ground_kind||
    !stadium_go_alignment_current_display(h,&gate,&mode,&remap)){
  stadium_go_alignment_fail(h,3);return;
 }
 if(source_branch!=(int)h->stadium_go_alignment.expected_branch){
  stadium_go_alignment_fail(h,4);return;
 }
 if(gate!=0||mode!=0xB||remap!=0){
  stadium_go_alignment_fail(h,3);return;
 }
 stadium_go_alignment_stamp(h,event,3,source_branch,gate,mode,remap,1,-1,object,proc);
}
void melee_web_stage_last_stadium_go_alignment_note_hud(
        int callback_index,MeleeWebStadiumGoAlignmentHudCallback callback,
        int hud_enabled)
{
 MeleeWebStageLast* h=active;
 if(!h||!h->stadium_go_alignment.armed)return;
 MeleeWebStadiumGoAlignmentEvent* event=&h->stadium_go_alignment.hud_after;
 void* object=HSD_GObj_804D781C;void* proc=HSD_GObj_804D7838;
 int gate=-1,mode=-1,remap=-1,identity=0;
 if(event->sequence||!stadium_go_alignment_live(h)||
    !h->stadium_go_alignment.go_after.sequence||!callback||
    !stadium_go_alignment_current_display(h,&gate,&mode,&remap)){
  stadium_go_alignment_fail(h,5);return;
 }
 if(callback_index==-1 && h->stadium_go_alignment.go_after.source_branch==1){
  identity=melee_web_hud_stadium_ready_context(object,proc);
 }else if(callback_index==4 && h->stadium_go_alignment.go_after.source_branch==0){
  const Element_803F9628* row=&ifStatus_803F9628[4];
  HSD_GObj* current_object=HSD_GObj_804D781C;
  HSD_GObjProc* current_proc=HSD_GObj_804D7838;
  identity=current_object&&current_proc&&!HSD_GObj_804D7814&&
      row->x0==current_object&&row->x8==if_802F73C4&&row->x1C==callback&&
      current_object->proc==current_proc&&!current_proc->child&&
      current_proc->gobj==current_object&&current_proc->on_invoke==if_802F73C4;
 }else{
  stadium_go_alignment_fail(h,5);return;
 }
 if(!identity||!object||!proc||gate!=0||mode!=1||remap!=0||hud_enabled!=1){
  stadium_go_alignment_fail(h,6);return;
 }
 stadium_go_alignment_stamp(h,event,callback_index,
      h->stadium_go_alignment.go_after.source_branch,gate,mode,remap,
      identity,hud_enabled,object,proc);
}
static int stadium_prepare_start(MeleeWebStageLast* h,
        MeleeWebMatchContext* camera_owner,char* e,size_t n)
{
 if(!h||h!=active||h->generation!=melee_web_gameplay_generation()||
    !h->definition||!h->definition->diagnostic_only||
    h->definition->stage_kind!=St_Kind_PStadium||!h->source_ordered||
    h->stadium_started||HSD_GObj_804D781C||HSD_GObj_804D7814)
  return fail(e,n,"Stadium continuation requires its idle, unstarted diagnostic owner");
 if(!melee_web_stage_selection_preflight(h->definition->stage_kind)||
    stage_info.grkind!=h->definition->ground_kind)
  return fail(e,n,"Stadium selected source dispatch pair changed");
 if(!stadium_maps_live(h,e,n)||
    !melee_web_match_camera_available(camera_owner,e,n))return 0;
 Ground* display=h->stadium_map2_buffer_owner.display_ground->user_data;
 if(display->u.display.xF4)return fail(e,n,"Stadium display already borrows a camera subject");
 /* Exact two source enqueue sites; Ground prepends, so drain is map2,map1.
  * Map2 creates nested map5 but enqueues its original map2 GObj.
  * Header ownership is independent of its borrowed object/callback fields. */
 const struct {HSD_GObj* object;HSD_GObjEvent callback;} expected[]={
  {h->stadium_map2_buffer_owner.map2_ground,fn_801D13C8},
  {h->stadium_map2_buffer_owner.display_ground,fn_801D11E4}};
 struct Pending {void* next;HSD_GObj* object;HSD_GObjEvent callback;};
 void** headers=h->stadium_headers;
 MeleeWebSourceMemoryAllocation* leases=h->stadium_header_leases;
 MeleeWebSourceMemoryContext context;
 if(melee_web_source_memory_context_read(&context)!=MELEE_WEB_SOURCE_MEMORY_READ_OK)
  return fail(e,n,"Stadium pending queue source heap is unavailable");
 void* node=stage_info.x6A4;
 for(size_t i=0;i<ARRAY_SIZE(expected);++i){
  if(!node||melee_web_source_memory_allocation_read(node,&leases[i])!=MELEE_WEB_SOURCE_MEMORY_READ_OK||
     !leases[i].live||leases[i].requested_bytes!=sizeof(struct Pending)||
     leases[i].world_generation!=context.world_generation||
     leases[i].source_heap_handle!=context.source_heap_handle)
   return fail(e,n,"Stadium pending callback header lost its exact source lease");
  struct Pending* p=node;
  if(p->object!=expected[i].object||p->callback!=expected[i].callback)
   return fail(e,n,"Stadium pending callback source order/borrower changed");
  headers[i]=node;node=p->next;
 }
 if(node)return fail(e,n,"Stadium pending callback queue has a foreign owner");
 h->stadium_generator=melee_web_stadium_generator_prepare(e,n);
 if(!h->stadium_generator)return 0;
 h->stadium_camera_owner=camera_owner;h->stadium_started=1;
 h->stadium_start_context=context;
 if(!h->stadium_loaded){Stage_80225298();h->stadium_loaded=1;}
 return ok(e,n);
}
static int stadium_capture_start(MeleeWebStageLast* h,int ready,char* e,size_t n)
{
 Ground* display=h->stadium_map2_buffer_owner.display_ground->user_data;
 MeleeWebMatchContext* camera_owner=h->stadium_camera_owner;
 const MeleeWebSourceMemoryContext context=h->stadium_start_context;
 h->stadium_subject=display->u.display.xF4;
 if(!(ready?melee_web_stadium_generator_capture_ready(h->stadium_generator,e,n):
            melee_web_stadium_generator_capture(h->stadium_generator,e,n))||
    !(ready?melee_web_match_camera_subject_preflight_ready(camera_owner,h->stadium_subject,e,n):
            melee_web_match_camera_subject_preflight(camera_owner,h->stadium_subject,e,n)))return 0;
 if(stage_info.x6A4)return fail(e,n,"Original Stadium OnStart did not drain the pending queue");
 /* The established tracker erases freed records. A reused address instead
  * has a live allocation newer than the context captured before OnStart. */
 for(size_t i=0;i<ARRAY_SIZE(h->stadium_headers);++i){
  MeleeWebSourceMemoryAllocation after;
  if(melee_web_source_memory_allocation_read(h->stadium_headers[i],&after)!=MELEE_WEB_SOURCE_MEMORY_READ_OK||
     after.world_generation!=context.world_generation||after.source_heap_handle!=context.source_heap_handle||
     (!after.live&&(after.requested_bytes||after.allocation_generation))||
     (after.live&&after.allocation_generation<=context.allocation_generation_watermark))
   return fail(e,n,"Original Stadium OnStart did not retire its exact pending header lease");
 }
 if(!melee_web_stadium_display_owner_capture_manager(h->stadium_display_owner,&h->stadium_manager,e,n))return 0;
 if(melee_web_source_memory_allocation_read(h->stadium_manager.object,&h->stadium_manager_lease)!=MELEE_WEB_SOURCE_MEMORY_READ_OK||
    melee_web_source_memory_allocation_read(h->stadium_manager.proc,&h->stadium_manager_proc_lease)!=MELEE_WEB_SOURCE_MEMORY_READ_OK||
    !stadium_manager_storage_preflight(h,e,n))return 0;
 h->stadium_started=2;return ok(e,n);
}
int melee_web_stage_last_stadium_start(MeleeWebStageLast* h,
        MeleeWebMatchContext* camera_owner,char* e,size_t n)
{
 if(!stadium_prepare_start(h,camera_owner,e,n))return 0;
 Stage_802252E4(St_Kind_PStadium,NULL);
 return stadium_capture_start(h,0,e,n);
}
int melee_web_stage_last_stadium_on_load(MeleeWebStageLast* h,char* e,size_t n)
{
 if(!h || h!=active || h->generation!=melee_web_gameplay_generation() ||
    !h->definition || !h->definition->diagnostic_only || !h->source_ordered ||
    h->stadium_loaded || h->stadium_started || HSD_GObj_804D781C || HSD_GObj_804D7814 ||
    !melee_web_stage_selection_preflight(St_Kind_PStadium))
  return fail(e,n,"Diagnostic Stadium OnLoad requires its exact unstarted owner");
 Stage_80225298();h->stadium_loaded=1;return ok(e,n);
}
int melee_web_stage_last_stadium_prepare_ready(MeleeWebStageLast* h,
        MeleeWebMatchContext* camera_owner,char* e,size_t n)
{
 if(!stadium_prepare_start(h,camera_owner,e,n))return 0;
 h->stadium_ready_route=1;h->stadium_ready_armed=1;
 return ok(e,n);
}
/* Hooks surround only the untouched original Stage OnStart dispatch. Ordinary
 * stages and the historical immediate diagnostic continuation are unchanged. */
static int stadium_ready_phase(MeleeWebStageLast* h,int stage_kind,int armed)
{
 return h && h==active && h->generation==melee_web_gameplay_generation() &&
        h->stadium_ready_route && h->definition && h->definition->diagnostic_only &&
        h->definition->stage_kind==St_Kind_PStadium && h->source_ordered &&
        stage_kind==St_Kind_PStadium && h->stadium_started==1 && h->stadium_ready_armed==armed;
}
int melee_web_stage_last_stadium_ready_before(int stage_kind,char* e,size_t n)
{
 MeleeWebStageLast* h=active;
 if(!h || !h->stadium_ready_route)return ok(e,n);
 if(!stadium_ready_phase(h,stage_kind,1) ||
    !melee_web_stage_selection_preflight(stage_kind) ||
    stage_info.grkind!=h->definition->ground_kind ||
    !melee_web_hud_stadium_ready_snapshot(&h->stadium_ready_object,&h->stadium_ready_proc))
  return fail(e,n,"Diagnostic Stadium OnStart requires its exact original HUD Ready callback");
 if(!stadium_maps_live(h,e,n)||
    !melee_web_match_camera_available_ready(h->stadium_camera_owner,e,n))return 0;
 /* Other original setup allocations can occur between OnLoad and Ready. The
  * exact original header leases remain owned; address reuse must be newer than
  * the actual pre-OnStart construction watermark. */
 if(melee_web_source_memory_context_read(&h->stadium_start_context)!=MELEE_WEB_SOURCE_MEMORY_READ_OK)
  return fail(e,n,"Diagnostic Stadium Ready source heap is unavailable");
 struct Pending {void* next;HSD_GObj* object;HSD_GObjEvent callback;};
 const HSD_GObj* objects[]={h->stadium_map2_buffer_owner.map2_ground,
                          h->stadium_map2_buffer_owner.display_ground};
 void* node=stage_info.x6A4;
 for(size_t i=0;i<ARRAY_SIZE(stadium_pending_callbacks);++i){
  MeleeWebSourceMemoryAllocation lease;
  if(node!=h->stadium_headers[i] ||
     melee_web_source_memory_allocation_read(node,&lease)!=MELEE_WEB_SOURCE_MEMORY_READ_OK ||
     !lease.live || lease.requested_bytes!=h->stadium_header_leases[i].requested_bytes ||
     lease.world_generation!=h->stadium_header_leases[i].world_generation ||
     lease.source_heap_handle!=h->stadium_header_leases[i].source_heap_handle ||
     lease.allocation_generation!=h->stadium_header_leases[i].allocation_generation ||
     lease.world_generation!=h->stadium_start_context.world_generation ||
     lease.source_heap_handle!=h->stadium_start_context.source_heap_handle)
   return fail(e,n,"Diagnostic Stadium Ready pending header lease changed");
  const struct Pending* p=node;
  if(p->object!=objects[i] || p->callback!=stadium_pending_callbacks[i])
   return fail(e,n,"Diagnostic Stadium Ready callback borrower changed");
  node=p->next;
 }
 if(node)return fail(e,n,"Diagnostic Stadium Ready pending queue has a foreign owner");
 if(h->stadium_go_alignment.armed &&
    !stadium_go_alignment_stage_note(h,0,e,n))return 0;
 h->stadium_ready_armed=2;return ok(e,n);
}
int melee_web_stage_last_stadium_ready_after(int stage_kind,char* e,size_t n)
{
 MeleeWebStageLast* h=active;
 if(!h || !h->stadium_ready_route)return ok(e,n);
 if(!stadium_ready_phase(h,stage_kind,2) ||
    !melee_web_hud_stadium_ready_context(h->stadium_ready_object,h->stadium_ready_proc))
  return fail(e,n,"Diagnostic Stadium original OnStart changed its HUD callback identity");
 if(!stadium_capture_start(h,1,e,n))return 0;
 if(h->stadium_go_alignment.armed &&
    !stadium_go_alignment_stage_note(h,1,e,n))return 0;
 h->stadium_ready_armed=0;h->stadium_ready_object=NULL;h->stadium_ready_proc=NULL;
 return ok(e,n);
}
int melee_web_stage_last_stadium_map2_buffer_snapshot(const MeleeWebStageLast* h,MeleeWebStadiumMap2BufferOwner* out){
 if(!h||h!=active||!out||h->generation!=melee_web_gameplay_stats().generation||!h->definition||!h->definition->diagnostic_only||!h->stadium_map2_buffer_owner.captured)return 0;
 *out=h->stadium_map2_buffer_owner;return 1;
}
int melee_web_stage_last_stadium_source_journal_snapshot(const MeleeWebStageLast* h,MeleeWebStadiumSourceJournal* out){
 if(!h||h!=active||!out||h->generation!=melee_web_gameplay_stats().generation||!h->definition||!h->definition->diagnostic_only||!h->stadium_display_owner)return 0;
 return melee_web_stadium_display_owner_source_journal_snapshot(h->stadium_display_owner,out);
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
 /* Validate every new owner and map borrower before any destructive release.
  * A partial original start retains its graph for reduction, never cancellation. */
 if(h->stadium_started){
  if(h->stadium_started!=2)return fail(e,n,"Partial Stadium OnStart ownership must remain reachable");
  if(!melee_web_stage_selection_preflight(h->definition->stage_kind)||
     stage_info.grkind!=h->definition->ground_kind)
   return fail(e,n,"Stadium selected source dispatch pair changed before retirement");
  if(!stadium_maps_live(h,e,n))return 0;
  if(((Ground*)h->stadium_map2_buffer_owner.display_ground->user_data)->u.display.xF4!=h->stadium_subject)
   return fail(e,n,"Stadium display camera borrower changed before retirement");
  if(!stadium_manager_storage_preflight(h,e,n)||
     !melee_web_stadium_display_owner_preflight(h->stadium_display_owner,e,n))return 0;
  if(!melee_web_stadium_generator_preflight(h->stadium_generator,e,n)||
     !melee_web_match_camera_subject_preflight(h->stadium_camera_owner,h->stadium_subject,e,n))return 0;
  if(!melee_web_stadium_generator_end(h->stadium_generator,e,n))return 0;
  h->stadium_generator=NULL;
  if(!melee_web_match_camera_subject_return(h->stadium_camera_owner,h->stadium_subject,e,n))return 0;
  ((Ground*)h->stadium_map2_buffer_owner.display_ground->user_data)->u.display.xF4=NULL;
  h->stadium_subject=NULL;h->stadium_camera_owner=NULL;h->stadium_started=0;
 }
 if(h->stadium_display_owner){
  if(!melee_web_stadium_display_owner_end(h->stadium_display_owner,e,n))return 0;
  h->stadium_display_owner=NULL;
 }
#endif
 /* Ready completion owns creation of this source scheduler. Early unload
  * before Ready is allowed to have none; multiple schedulers are an error. */
 h->manager=NULL;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 if(h->stadium_manager.object){
  if(!stadium_manager_storage_preflight(h,e,n))return 0;
  h->manager=h->stadium_manager.object;
 }else
#endif
 for(HSD_GObj* obj=((HSD_GObj**)HSD_GObj_Entities)[5];obj;obj=obj->next){
  if(obj->classifier==HSD_GOBJ_CLASS_STAGE&&!obj->user_data){
   if(h->manager)return fail(e,n,"Original stage started more than once");
   h->manager=obj;
  }
 }
 if(h->manager){HSD_GObjPLink_80390228(h->manager);h->manager=NULL;}
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 if(h->stadium_manager.object){
  for(HSD_GObj* object=((HSD_GObj**)HSD_GObj_Entities)[5];object;object=object->next)
   if(object==h->stadium_manager.object)return fail(e,n,"Original Stadium manager remained in its object root");
  for(HSD_GObjProc* proc=HSD_GObj_804D7840[10];proc;proc=proc->next)
   if(proc==h->stadium_manager.proc)return fail(e,n,"Original Stadium manager remained in its process root");
 }
 memset(&h->stadium_manager,0,sizeof(h->stadium_manager));
#endif
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 if(h->stadium_map2_buffer_owner.captured&&
    !melee_web_stadium_map2_buffer_owner_end(&h->stadium_map2_buffer_owner,e,n))return 0;
#endif
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
