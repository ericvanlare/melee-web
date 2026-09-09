#ifndef MELEE_WEB_GAMEPLAY_MATCH_RULES_H
#define MELEE_WEB_GAMEPLAY_MATCH_RULES_H
#include <stddef.h>
typedef struct StartMeleeData StartMeleeData;
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebMatchRules MeleeWebMatchRules;
/* Owns original match globals; source defaults plus explicit local stock rules.
 * This does not create the match manager, timer, HUD or result processes. */
MeleeWebMatchRules* melee_web_match_rules_begin(char*,size_t);
/* Copy and validate the complete menu payload, then invoke the original
 * fn_8016DCC0 boundary before any source fighter exists. */
int melee_web_match_rules_init_from_menu(MeleeWebMatchRules*,
                                         const StartMeleeData*,char*,size_t);
void melee_web_match_rules_refresh(void);
/* Original FFA outcome enum; winner is source slot or -1. */
int melee_web_match_rules_outcome(int* winner);
int melee_web_match_rules_end(MeleeWebMatchRules*,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
