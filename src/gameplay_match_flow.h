#ifndef MELEE_WEB_GAMEPLAY_MATCH_FLOW_H
#define MELEE_WEB_GAMEPLAY_MATCH_FLOW_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebMatchFlow MeleeWebMatchFlow;
MeleeWebMatchFlow* melee_web_match_flow_begin(int opening_demo, char*, size_t);
/* Captures only this C flow owner. Clock/cadence private state resides in
 * generated source gm_1A45.c/lb_0192.c and requires the source-static snapshot. */
typedef struct MeleeWebMatchFlowSnapshot MeleeWebMatchFlowSnapshot;
MeleeWebMatchFlowSnapshot* melee_web_match_flow_snapshot_capture(
    MeleeWebMatchFlow*,char*,size_t);
int melee_web_match_flow_snapshot_admit(
    MeleeWebMatchFlow*,const MeleeWebMatchFlowSnapshot*,char*,size_t);
int melee_web_match_flow_snapshot_restore(
    MeleeWebMatchFlow*,const MeleeWebMatchFlowSnapshot*,char*,size_t);
void melee_web_match_flow_snapshot_destroy(MeleeWebMatchFlowSnapshot*);
int melee_web_match_flow_renew(void*, char*, size_t);
int melee_web_match_flow_pre(void*, char*, size_t);
int melee_web_match_flow_post(void*, char*, size_t);
int melee_web_match_flow_present(MeleeWebMatchFlow*, char*, size_t);
int melee_web_match_flow_ending(const MeleeWebMatchFlow*);
int melee_web_match_flow_complete(const MeleeWebMatchFlow*);
int melee_web_match_flow_paused(const MeleeWebMatchFlow*);
uint32_t melee_web_match_flow_frames(const MeleeWebMatchFlow*);
int melee_web_match_flow_result(const MeleeWebMatchFlow*);
int melee_web_match_flow_end(MeleeWebMatchFlow*, char*, size_t);
#ifdef __cplusplus
}
#endif
#endif
