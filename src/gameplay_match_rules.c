#include "gameplay_match_rules.h"
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
struct MeleeWebMatchRules {lbl_8046B6A0_t saved;StaticPlayer players[6];uint64_t generation;};
static MeleeWebMatchRules* active;
static int fail(char* e,size_t n,const char* message){if(e&&n)snprintf(e,n,"%s",message);return 0;}
MeleeWebMatchRules* melee_web_match_rules_begin(char* e,size_t n){
    if(active||!melee_web_gameplay_stats().generation){fail(e,n,"Match rules require an unowned live source world");return NULL;}
    for(int i=0;i<6;i++)if(Player_GetEntity(i)){fail(e,n,"Initialize rules before source fighters");return NULL;}
    MeleeWebMatchRules* h=malloc(sizeof(*h));if(!h){fail(e,n,"Cannot allocate original rules scope");return NULL;}
    lbl_8046B6A0_t* data=gm_16AE_GetUnkData_0();h->saved=*data;h->generation=melee_web_gameplay_stats().generation;
    for(int i=0;i<6;i++){
        h->players[i]=*Player_GetPtrForSlot(i);
        Player_InitOrResetPlayer(i);
    }
    memset(data,0,sizeof(*data));gm_SetupRulesDefaults(&data->x24C8);
    data->x24C8.match_kind=MatchKind_Stock;data->x24C8.is_stock=1;data->x24C8.is_vs=1;
    data->x24C8.is_teams=0;data->x24C8.stkind=St_Kind_Last;
    data->x24C8.xB=-1;data->x24C8.x20=0;data->x24C8.timer_enabled=0;
    data->x24C.x5=MatchKind_Stock;data->x24C.is_teams=0;
    active=h;melee_web_match_source_refresh_ratio();if(e&&n)*e=0;return h;
}
void melee_web_match_rules_refresh(void){
    if(active&&active->generation==melee_web_gameplay_stats().generation)melee_web_match_source_refresh_ratio();
}
int melee_web_match_rules_outcome(int* winner)
{
    if(winner)*winner=-1;
    if(!active)return 0;
    MatchOutcome result=gm_GetFFAOutcome();
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
