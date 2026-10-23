/* Actual flow + gm_1A45 + lb_0195 writers; explicit source service leaves.
 * The separate test bootstrap TU contains the actual production owner predicate.
 * This synthetic component control is not aggregate/browser acceptance. */
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned allocations, releases;
static void* fixture_malloc(size_t n){++allocations;return malloc(n);}
static void* fixture_calloc(size_t n,size_t s){++allocations;return calloc(n,s);}
static void fixture_free(void* p){if(p)++releases;free(p);}
#define malloc fixture_malloc
#define calloc fixture_calloc
#define free fixture_free
#include "../src/gameplay_match_flow.c"
#ifndef MELEE_WEB_TEST_CLOCK_SOURCE
#define MELEE_WEB_TEST_CLOCK_SOURCE "../build/gameplay-source/src/melee/gm/gm_1A45.c"
#endif
#ifndef MELEE_WEB_TEST_CADENCE_SOURCE
#define MELEE_WEB_TEST_CADENCE_SOURCE "../build/gameplay-source/src/melee/lb/lb_0195.c"
#endif
#include MELEE_WEB_TEST_CLOCK_SOURCE
#include MELEE_WEB_TEST_CADENCE_SOURCE
#undef malloc
#undef calloc
#undef free
extern void fixture_bootstrap_owner_begin(void);
extern void fixture_bootstrap_owner_rebind(void);
extern u64* fixture_bootstrap_mask(void);
/* Explicit SDK-heap leaf for this synthetic clock/flow control only. */
int melee_web_gameplay_heap_owns(const void* p){return p==(void*)(uintptr_t)0x1000;}
HSD_GObjLibInitDataType HSD_GObjLibInitData={0};
HSD_GObj *HSD_GObj_804D7814,*HSD_GObj_804D7818,*HSD_GObj_804D781C;
HSD_PadStatus HSD_PadMasterStatus[4];
static unsigned game_renewed,copy_renewed,frames;
void HSD_PadRenewGameStatus(void){++game_renewed;}
void HSD_PadRenewCopyStatus(void){++copy_renewed;}
int melee_web_match_end_state(void){return 0;}
int melee_web_match_source_result(void){return OUTCOME_NONE;}
int melee_web_match_source_frame(void){++frames;return 1;}
u32 melee_web_match_source_frames(void){return gm_801A4BB8();}
int melee_web_controller_map_begin(void){return 1;}
int melee_web_controller_map_end(void){return 1;}
void gm_EvaluateAllControllerInputs(void){}
void lbAudioAx_80027DF8(void){}
void gm_801677C0(struct gm_801677C0_s* p)
{p->x0=p->x1=p->x2=p->x3=0;p->unk_20=p->unk_28=0;}
static void tick(MeleeWebMatchFlow* flow)
{
 char error[160];
 assert(melee_web_match_flow_renew(flow,error,sizeof(error)));
 assert(melee_web_match_flow_pre(flow,error,sizeof(error)));
 assert(melee_web_match_flow_post(flow,error,sizeof(error)));
}
static void refused(MeleeWebMatchFlow* flow,MeleeWebMatchFlowCpuSnapshot* snapshot)
{
 char error[160];
 struct gm_80479D58_t clock=gm_80479D58;
 struct lb_804329F0_t cadence=lb_804329F0;
 MeleeWebMatchFlow saved=*flow;
 u64 clock_lifetime=melee_source_clock_lifetime;
 u64 cadence_lifetime=melee_source_cadence_lifetime;
 unsigned a=allocations,f=releases;
 assert(!melee_web_match_flow_cpu_snapshot_restore(flow,snapshot,error,sizeof(error)));
 assert(melee_source_clock_record_equal(&clock,&gm_80479D58));
 assert(melee_source_cadence_record_equal(&cadence,&lb_804329F0));
 assert(!memcmp(&saved,flow,sizeof(saved)));
 assert(clock_lifetime==melee_source_clock_lifetime);
 assert(cadence_lifetime==melee_source_cadence_lifetime);
 assert(a==allocations&&f==releases);
}
static void capture_refused(MeleeWebMatchFlow* flow,const char* expected)
{
 char error[160];struct gm_80479D58_t clock=gm_80479D58;
 struct lb_804329F0_t cadence=lb_804329F0;MeleeWebMatchFlow state=*flow;
 u64 cl=melee_source_clock_lifetime,dl=melee_source_cadence_lifetime;
 unsigned a=allocations,f=releases;
 assert(!melee_web_match_flow_cpu_snapshot_capture(flow,error,sizeof(error)));
 assert(!strcmp(error,expected)&&allocations-a==releases-f);
 assert(melee_source_clock_record_equal(&clock,&gm_80479D58)&&melee_source_cadence_record_equal(&cadence,&lb_804329F0));
 assert(!memcmp(&state,flow,sizeof(state))&&cl==melee_source_clock_lifetime&&dl==melee_source_cadence_lifetime);
}
static void verify_original_vs_mask_profile(void)
{
 char error[160];fixture_bootstrap_owner_begin();owner=NULL;
 /* Exact source constructor callback and process-mask publication topology.
  * The remaining VS manager services are outside this focused owner test. */
 gm_801A4B08(fn_801A46F4,fn_801A47E4);gm_801A4B40(0);gm_801A4B50(0);
 gm_80479D48.initdata.unk_2=&gm_80479D58.unk_10.unk_28;
 HSD_GObjLibInitData=gm_80479D48.initdata;
 MeleeWebMatchFlow* current=melee_web_match_flow_begin(0,error,sizeof(error));assert(current);
 assert(melee_source_clock_saved_mask_ptr==&gm_80479D58.unk_10.unk_28);
 MeleeWebMatchFlowCpuSnapshot* pre=melee_web_match_flow_cpu_snapshot_capture(current,error,sizeof(error));assert(pre);
 const MeleeWebSourceClockSnapshot* clock=pre->flow->source_clock;
 assert(!melee_web_source_clock_snapshot_cpu_mask_admit(NULL,current,owner_epoch,current->generation));
 assert(!melee_web_source_clock_snapshot_bootstrap_mask_admit(clock));
 assert(melee_web_source_clock_snapshot_cpu_mask_admit(clock,current,owner_epoch,current->generation));
 const MeleeWebClockCpuStorage storage=melee_web_source_clock_cpu_storage();
 const uintptr_t field=(uintptr_t)&gm_80479D58.unk_10.unk_28;
 assert(storage.record==&gm_80479D58&&storage.record_bytes==64);
 assert(field>=(uintptr_t)storage.record&&field+sizeof(gm_80479D58.unk_10.unk_28)<=(uintptr_t)storage.record+storage.record_bytes);
 /* Real guard inputs, not captured epoch/world values supplied as authority. */
 assert(!melee_web_source_clock_snapshot_cpu_mask_admit(clock,current,owner_epoch+1,current->generation));
 assert(!melee_web_source_clock_snapshot_cpu_mask_admit(clock,current,owner_epoch,current->generation+1));
 HSD_GObj marker={0};HSD_GObj_804D781C=&marker;
 capture_refused(current,"Flow snapshot requires a quiescent live owner");
 refused(current,pre);HSD_GObj_804D781C=NULL;
 melee_source_cadence_active=0;capture_refused(current,"Source cadence snapshot capture returned null");melee_source_cadence_active=1;
 u64* source_pointer=melee_source_clock_saved_mask_ptr;
 u64* snapshot_pointer=pre->flow->source_clock->saved_mask_ptr;
 u64 foreign=melee_source_clock_saved_mask;
 melee_source_clock_saved_mask_ptr=&foreign;pre->flow->source_clock->saved_mask_ptr=&foreign;
 capture_refused(current,"Clock saved mask has no supported CPU snapshot owner");
 assert(!melee_web_source_clock_snapshot_cpu_mask_admit(clock,current,owner_epoch,current->generation));refused(current,pre);
 melee_source_clock_saved_mask_ptr=NULL;pre->flow->source_clock->saved_mask_ptr=NULL;
 assert(!melee_web_source_clock_snapshot_cpu_mask_admit(clock,current,owner_epoch,current->generation));refused(current,pre);
 melee_source_clock_saved_mask_ptr=(u64*)(uintptr_t)1;pre->flow->source_clock->saved_mask_ptr=(u64*)(uintptr_t)1;
 assert(!melee_web_source_clock_snapshot_cpu_mask_admit(clock,current,owner_epoch,current->generation));refused(current,pre);
 melee_source_clock_saved_mask_ptr=source_pointer;pre->flow->source_clock->saved_mask_ptr=snapshot_pointer;
 tick(current);MeleeWebMatchFlowCpuSnapshot* post=melee_web_match_flow_cpu_snapshot_capture(current,error,sizeof(error));assert(post);
 unsigned a=allocations,f=releases;
 assert(melee_web_match_flow_cpu_snapshot_restore(current,pre,error,sizeof(error)));
 assert(allocations==a&&releases==f&&melee_web_match_flow_cpu_snapshot_matches(current,pre,error,sizeof(error)));
 tick(current);assert(melee_web_match_flow_cpu_snapshot_matches(current,post,error,sizeof(error)));
 MeleeWebMatchFlow* retired=current;
 assert(melee_web_match_flow_end(current,error,sizeof(error)));
 assert(HSD_GObjLibInitData.unk_2==&gm_80479D58.unk_10.unk_28);
 assert(!melee_web_match_flow_cpu_snapshot_admit(retired,pre,error,sizeof(error)));
 assert(!melee_web_source_clock_snapshot_cpu_mask_admit(clock,retired,pre->flow->epoch,pre->flow->value.generation));
 current=melee_web_match_flow_begin(0,error,sizeof(error));assert(current);
 assert(!melee_web_match_flow_cpu_snapshot_admit(current,pre,error,sizeof(error)));
 assert(!melee_web_source_clock_snapshot_cpu_mask_admit(clock,current,owner_epoch,current->generation));
 MeleeWebMatchFlowCpuSnapshot* fresh=melee_web_match_flow_cpu_snapshot_capture(current,error,sizeof(error));assert(fresh);
 assert(melee_web_match_flow_end(current,error,sizeof(error)));
 melee_web_match_flow_cpu_snapshot_destroy(fresh);melee_web_match_flow_cpu_snapshot_destroy(post);melee_web_match_flow_cpu_snapshot_destroy(pre);
 printf("Original VS saved-mask profile: exact typed clock field offset%zu/extent%zu within64B; noalloc restore/replay, foreign/invalid pointer, callback/epoch/world, retired and recreated refusals passed\n",(size_t)(field-(uintptr_t)storage.record),sizeof(gm_80479D58.unk_10.unk_28));
}
int main(void)
{
 char error[160];fixture_bootstrap_owner_begin();
 MeleeWebMatchFlow flow={melee_web_gameplay_generation(),1,0,0};
 owner=&flow;owner_epoch=7;
 HSD_GObjLibInitData.unk_2=fixture_bootstrap_mask();
 assert(melee_web_source_clock_begin(MELEE_WEB_SOURCE_CLOCK_MATCH));
 assert(melee_web_source_cadence_begin());
 MeleeWebMatchFlowCpuSnapshot* pre=melee_web_match_flow_cpu_snapshot_capture(&flow,error,sizeof(error));
 assert(pre&&melee_web_match_flow_cpu_snapshot_matches(&flow,pre,error,sizeof(error)));
 u64 immutable_bootstrap_mask=*fixture_bootstrap_mask();*fixture_bootstrap_mask()^=1;
 refused(&flow,pre);assert(*fixture_bootstrap_mask()==(immutable_bootstrap_mask^1));
 *fixture_bootstrap_mask()=immutable_bootstrap_mask;
 tick(&flow);assert(frames==1&&game_renewed==1&&copy_renewed==1);
 assert(!melee_web_match_flow_cpu_snapshot_matches(&flow,pre,error,sizeof(error)));
 MeleeWebMatchFlowCpuSnapshot* post=melee_web_match_flow_cpu_snapshot_capture(&flow,error,sizeof(error));assert(post);
 /* Legacy restore remains flow-only and does not rewind either source clock. */
 MeleeWebMatchFlowSnapshot* legacy=melee_web_match_flow_snapshot_capture(&flow,error,sizeof(error));assert(legacy);
 u32 frame=gm_801A4BB8();int edge=lb_804329F0.x0[0].x10;
 flow.source_valid=0;assert(melee_web_match_flow_snapshot_restore(&flow,legacy,error,sizeof(error)));
 assert(frame==gm_801A4BB8()&&edge==lb_804329F0.x0[0].x10);
 unsigned a=allocations,f=releases;u64 cl=melee_source_clock_lifetime,dl=melee_source_cadence_lifetime;
 assert(melee_web_match_flow_cpu_snapshot_admit(&flow,pre,error,sizeof(error)));
 assert(melee_web_match_flow_cpu_snapshot_restore(&flow,pre,error,sizeof(error)));
 assert(a==allocations&&f==releases&&cl==melee_source_clock_lifetime&&dl==melee_source_cadence_lifetime);
 assert(melee_web_match_flow_cpu_snapshot_matches(&flow,pre,error,sizeof(error)));
 tick(&flow);assert(melee_web_match_flow_cpu_snapshot_matches(&flow,post,error,sizeof(error)));
 /* Phase-matched drawn endpoint: presentation advances the original typed
  * counter, while a correction tick must not fake that source operation. */
 u32 present_before=gm_80479D58.unk_4;
 assert(melee_web_match_flow_present(&flow,error,sizeof(error)));
 assert(gm_80479D58.unk_4==present_before+1);
 MeleeWebMatchFlowCpuSnapshot* drawn=melee_web_match_flow_cpu_snapshot_capture(&flow,error,sizeof(error));assert(drawn);
 assert(!melee_web_match_flow_cpu_snapshot_matches(&flow,post,error,sizeof(error)));
 assert(melee_web_match_flow_cpu_snapshot_restore(&flow,pre,error,sizeof(error)));
 tick(&flow);assert(melee_web_match_flow_cpu_snapshot_matches(&flow,post,error,sizeof(error)));
 assert(!melee_web_match_flow_cpu_snapshot_matches(&flow,drawn,error,sizeof(error)));
 assert(melee_web_match_flow_present(&flow,error,sizeof(error)));
 assert(gm_80479D58.unk_4==present_before+1&&melee_web_match_flow_cpu_snapshot_matches(&flow,drawn,error,sizeof(error)));
 melee_web_match_flow_cpu_snapshot_destroy(drawn);
 /* Independent owner/lifetime/callback/alarm refusals precede every write. */
 HSD_GObj marker={0};HSD_GObj_804D781C=&marker;refused(&flow,pre);HSD_GObj_804D781C=NULL;
 lb_804329F0.x48=1;refused(&flow,pre);lb_804329F0.x48=0;
 ++owner_epoch;refused(&flow,pre);--owner_epoch;
 u64 mask=0;u64* saved_mask=melee_source_clock_saved_mask_ptr;
 melee_source_clock_saved_mask_ptr=&mask;refused(&flow,pre);melee_source_clock_saved_mask_ptr=saved_mask;
 assert(melee_web_source_cadence_end());assert(melee_web_source_cadence_begin());refused(&flow,pre);
 assert(melee_web_source_cadence_end());assert(melee_web_source_clock_end());
 /* A rebound world cannot reuse a capsule even if source storage addresses match. */
 fixture_bootstrap_owner_rebind();refused(&flow,pre);
 melee_web_match_flow_snapshot_destroy(legacy);
 melee_web_match_flow_cpu_snapshot_destroy(post);melee_web_match_flow_cpu_snapshot_destroy(pre);
 verify_original_vs_mask_profile();
 puts("Actual flow clock cadence CPU restore and lifetime/refusal controls passed");
 return 0;
}
