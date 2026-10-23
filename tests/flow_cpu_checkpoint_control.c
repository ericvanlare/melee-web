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
 puts("Actual flow clock cadence CPU restore and lifetime/refusal controls passed");
 return 0;
}
