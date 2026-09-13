#ifndef MELEE_WEB_GAMEPLAY_MATCH_RULES_H
#define MELEE_WEB_GAMEPLAY_MATCH_RULES_H
#include <stddef.h>
typedef struct StartMeleeData StartMeleeData;
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebMatchRules MeleeWebMatchRules;
struct StartMeleeRules;
/* Preserve disabled-timer behavior.  When enabled, admit only the ordinary
 * VS stock countdown produced by the retail Rule Plus menu: a positive whole
 * number of minutes (1..99), represented in the source payload as seconds,
 * counting down from the initial second with the normal non-hour HUD. */
int melee_web_match_timer_supported(const struct StartMeleeRules*);
/* Owns original match globals; source defaults plus explicit local stock rules.
 * This does not create the match manager, timer, HUD or result processes. */
MeleeWebMatchRules* melee_web_match_rules_begin(char*,size_t);
/* Copy and validate the complete menu payload, then invoke the original
 * fn_8016DCC0 boundary before any source fighter exists. */
int melee_web_match_rules_init_from_menu(MeleeWebMatchRules*,
                                         const StartMeleeData*,char*,size_t);
void melee_web_match_rules_refresh(void);
/* Original match outcome enum; winner is source slot for elimination, or -1. */
int melee_web_match_rules_outcome(int* winner);
int melee_web_match_rules_end(MeleeWebMatchRules*,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
