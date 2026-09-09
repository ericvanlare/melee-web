#ifndef MELEE_WEB_GAMEPLAY_MATCH_FLOW_H
#define MELEE_WEB_GAMEPLAY_MATCH_FLOW_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebMatchFlow MeleeWebMatchFlow;
MeleeWebMatchFlow* melee_web_match_flow_begin(char*, size_t);
int melee_web_match_flow_renew(void*, char*, size_t);
int melee_web_match_flow_pre(void*, char*, size_t);
int melee_web_match_flow_post(void*, char*, size_t);
int melee_web_match_flow_present(MeleeWebMatchFlow*, char*, size_t);
int melee_web_match_flow_ending(const MeleeWebMatchFlow*);
int melee_web_match_flow_complete(const MeleeWebMatchFlow*);
int melee_web_match_flow_end(MeleeWebMatchFlow*, char*, size_t);
#ifdef __cplusplus
}
#endif
#endif
