#include "gameplay_match_rules.h"
#include "gameplay_content.h"
#include "gameplay_bootstrap.h"
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_16AE.h>
#include <melee/gm/types.h>
#include <melee/gr/forward.h>
#include <melee/pl/player.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern void melee_web_match_source_refresh_ratio(void);
extern int melee_web_match_init_source(StartMeleeData*);
struct MeleeWebMatchRules {lbl_8046B6A0_t saved;StaticPlayer players[6];StartMeleeData start;uint64_t generation;int initialized;};
static MeleeWebMatchRules* active;
static int fail(char* e,size_t n,const char* message){if(e&&n)snprintf(e,n,"%s",message);return 0;}
int melee_web_match_timer_supported(const struct StartMeleeRules* rules)
{
    if (!rules) return 0;
    if (!rules->timer_enabled) return 1;
    /* Retail Rule Plus stores minutes in an u8; the VS handoff converts each
     * selected minute to 60 seconds before it reaches time_limit. */
    return !rules->timer_counts_up && !rules->timer_shows_hours &&
           rules->time_limit >= 60 && rules->time_limit <= 99 * 60 &&
           rules->time_limit % 60 == 0 && rules->x14 == 0;
}
MeleeWebMatchRules* melee_web_match_rules_begin(char* e,size_t n){
    if(active||!melee_web_gameplay_stats().generation){fail(e,n,"Match rules require an unowned live source world");return NULL;}
    for(int i=0;i<6;i++)if(Player_GetEntity(i)){fail(e,n,"Initialize rules before source fighters");return NULL;}
    MeleeWebMatchRules* h=malloc(sizeof(*h));if(!h){fail(e,n,"Cannot allocate original rules scope");return NULL;}
    lbl_8046B6A0_t* data=gm_16AE_GetUnkData_0();h->saved=*data;h->generation=melee_web_gameplay_stats().generation;h->initialized=0;
    for(int i=0;i<6;i++){
        h->players[i]=*Player_GetPtrForSlot(i);
        Player_InitOrResetPlayer(i);
    }
    memset(data,0,sizeof(*data));gm_SetupRulesDefaults(&data->x24C8);
    data->x24C8.match_kind=MatchKind_Stock;data->x24C8.is_stock=1;data->x24C8.is_vs=1;
    data->x24C8.is_teams=0;data->x24C8.stkind=St_Kind_Last;
    data->x24C8.xB=-1;data->x24C8.x20=UINT64_MAX;data->x24C8.timer_enabled=0;
    data->x24C.x5=MatchKind_Stock;data->x24C.is_teams=0;
    active=h;melee_web_match_source_refresh_ratio();if(e&&n)*e=0;return h;
}
int melee_web_match_rules_init_from_menu(MeleeWebMatchRules* h,
                                         const StartMeleeData* menu,
                                         char* e,size_t n)
{
    if(!h||h!=active||h->generation!=melee_web_gameplay_stats().generation)
        return fail(e,n,"Original match rules scope is not active");
    if(h->initialized)return fail(e,n,"Original match data is already initialized");
    if(!menu)return fail(e,n,"A complete original menu payload is required");
    for(int i=0;i<6;i++)if(Player_GetEntity(i))
        return fail(e,n,"Initialize original match data before source fighters");
    StartMeleeData candidate=*menu;
    if(candidate.rules.match_kind!=MatchKind_Stock||!candidate.rules.is_stock||
       !candidate.rules.is_vs||candidate.rules.is_teams||
       !melee_web_match_timer_supported(&candidate.rules)||candidate.rules.xB!=-1||
       candidate.rules.x20!=UINT64_MAX||!melee_web_stage_content(candidate.rules.stkind))
        return fail(e,n,"Menu payload does not match the supported stock/stage rules");
    h->start=candidate;
    if(!melee_web_match_init_source(&h->start)){
        memset(&h->start,0,sizeof(h->start));
        return fail(e,n,"Original per-match initialization rejected menu payload");
    }
    h->initialized=1;
    melee_web_match_source_refresh_ratio();
    if(e&&n)*e=0;return 1;
}
void melee_web_match_rules_refresh(void){
    if(active&&active->generation==melee_web_gameplay_stats().generation)melee_web_match_source_refresh_ratio();
}
int melee_web_match_rules_outcome(int* winner)
{
    if(winner)*winner=-1;
    if(!active)return 0;
    /* gm_GetFFAOutcome only covers stock elimination.  The original match
     * manager owns the timer branch and returns OUTCOME_TIMEOUT there. */
    MatchOutcome result=gm_GetMatchOutcome();
    if(result==OUTCOME_ELIMINATION&&winner){
        for(int i=0;i<6;i++)if(Player_GetPlayerSlotType(i)!=Gm_PKind_NA&&Player_GetStocks(i)>0)*winner=i;
    }
    return result;
}
int melee_web_match_rules_end(MeleeWebMatchRules* h,char* e,size_t n){
    if(!h)return 1;
    if(active!=h||h->generation!=melee_web_gameplay_stats().generation)return fail(e,n,"Original rules ownership changed");
    for(int i=0;i<6;i++)if(Player_GetEntity(i))return fail(e,n,"Unload source fighters before restoring rules");
    for(int i=0;i<6;i++)*Player_GetPtrForSlot(i)=h->players[i];
    *gm_16AE_GetUnkData_0()=h->saved;active=NULL;free(h);if(e&&n)*e=0;return 1;
}
