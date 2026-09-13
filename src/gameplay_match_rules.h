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
/* Publish the source MatchEnd ranking at close/exit while source fighters are
 * resident. Must run before melee_web_match_end destroys them. */
int melee_web_match_rules_publish_result(void);
/* Read the close-boundary source snapshot after the source player/rules
 * owner has been torn down. The cache is cleared at the next begin(). */
int melee_web_match_rules_terminal_result(int* outcome,int* count,int winners[6]);
/* Original match outcome enum; a unique source winner is returned for stock
 * elimination or timeout, while source ties and unavailable rankings return
 * winner=-1. */
int melee_web_match_rules_outcome(int* winner);
int melee_web_match_rules_end(MeleeWebMatchRules*,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
