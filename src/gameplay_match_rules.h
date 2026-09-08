#ifndef MELEE_WEB_GAMEPLAY_MATCH_RULES_H
#define MELEE_WEB_GAMEPLAY_MATCH_RULES_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebMatchRules MeleeWebMatchRules;
/* Owns original match globals; source defaults plus explicit local stock rules.
 * This does not create the match manager, timer, HUD or result processes. */
MeleeWebMatchRules* melee_web_match_rules_begin(char*,size_t);
void melee_web_match_rules_refresh(void);
/* Original FFA outcome enum; winner is source slot or -1. */
int melee_web_match_rules_outcome(int* winner);
int melee_web_match_rules_end(MeleeWebMatchRules*,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
