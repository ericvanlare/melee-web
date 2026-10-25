#ifndef MELEE_WEB_GAMEPLAY_MATCH_FLOW_H
#define MELEE_WEB_GAMEPLAY_MATCH_FLOW_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebMatchFlow MeleeWebMatchFlow;
MeleeWebMatchFlow* melee_web_match_flow_begin(int opening_demo, char*, size_t);
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
