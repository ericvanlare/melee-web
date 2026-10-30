#ifndef MELEE_WEB_GAMEPLAY_MATCH_FLOW_H
#define MELEE_WEB_GAMEPLAY_MATCH_FLOW_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebMatchFlow MeleeWebMatchFlow;
MeleeWebMatchFlow* melee_web_match_flow_begin(int opening_demo, char*, size_t);
/* Legacy restore remains flow-only. Its snapshot also holds a read-only
 * clock companion for admission; CPU restore is an explicit separate API. */
typedef struct MeleeWebMatchFlowSnapshot MeleeWebMatchFlowSnapshot;
MeleeWebMatchFlowSnapshot* melee_web_match_flow_snapshot_capture(
    MeleeWebMatchFlow*,char*,size_t);
int melee_web_match_flow_snapshot_admit(
    MeleeWebMatchFlow*,const MeleeWebMatchFlowSnapshot*,char*,size_t);
int melee_web_match_flow_snapshot_restore(
    MeleeWebMatchFlow*,const MeleeWebMatchFlowSnapshot*,char*,size_t);
void melee_web_match_flow_snapshot_destroy(MeleeWebMatchFlowSnapshot*);
/* Explicit flow + source-clock + source-cadence CPU companion. Captures and
 * validates all three before writing; no allocation or callback in restore. */
typedef struct MeleeWebMatchFlowCpuSnapshot MeleeWebMatchFlowCpuSnapshot;
MeleeWebMatchFlowCpuSnapshot* melee_web_match_flow_cpu_snapshot_capture(
    MeleeWebMatchFlow*,char*,size_t);
int melee_web_match_flow_cpu_snapshot_admit(
    MeleeWebMatchFlow*,const MeleeWebMatchFlowCpuSnapshot*,char*,size_t);
int melee_web_match_flow_cpu_snapshot_restore(
    MeleeWebMatchFlow*,const MeleeWebMatchFlowCpuSnapshot*,char*,size_t);
int melee_web_match_flow_cpu_snapshot_matches(
    MeleeWebMatchFlow*,const MeleeWebMatchFlowCpuSnapshot*,char*,size_t);
/* Private read-only endpoint values. The existing owned snapshot supplies
 * lifetime admission only; values come from the current live flow. status 0
 * means admitted, 1 means refusal with every identity/value zero. No source
 * storage or caller-provided output pointer is written. Clock and cadence
 * retain their separate typed storage getters and this same CPU admission. */
typedef struct MeleeWebMatchFlowCpuEndpoint {
    uint32_t status;
    uint64_t identity, epoch, generation;
    int32_t source_valid, complete, opening_demo;
} MeleeWebMatchFlowCpuEndpoint;
MeleeWebMatchFlowCpuEndpoint melee_web_match_flow_cpu_endpoint_read(
    MeleeWebMatchFlow*,const MeleeWebMatchFlowCpuSnapshot*);
void melee_web_match_flow_cpu_snapshot_destroy(MeleeWebMatchFlowCpuSnapshot*);

int melee_web_match_flow_renew(void*, char*, size_t);
int melee_web_match_flow_pre(void*, char*, size_t);
int melee_web_match_flow_post(void*, char*, size_t);
int melee_web_match_flow_present(MeleeWebMatchFlow*, char*, size_t);
int melee_web_match_flow_ending(const MeleeWebMatchFlow*);
int melee_web_match_flow_complete(const MeleeWebMatchFlow*);
int melee_web_match_flow_paused(const MeleeWebMatchFlow*);
uint32_t melee_web_match_flow_frames(const MeleeWebMatchFlow*);
int melee_web_match_flow_result(const MeleeWebMatchFlow*);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
/* Read-only original fields, copied only under the live flow owner. */
typedef struct MeleeWebMatchPauseObservation {
    uint64_t generation;
    uint32_t frame;
    unsigned pause_timer;
    int paused, result, complete;
} MeleeWebMatchPauseObservation;
int melee_web_match_flow_pause_observation(const MeleeWebMatchFlow*,
                                         MeleeWebMatchPauseObservation*);
#endif
int melee_web_match_flow_end(MeleeWebMatchFlow*, char*, size_t);
#ifdef __cplusplus
}
#endif
#endif
