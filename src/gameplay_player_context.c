#include "gameplay_player_context.h"
#include <melee/pl/player.h>
#include <melee/pl/plstale.h>
#include <melee/gm/forward.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct MeleeWebPlayerContext { uint32_t slot; StaticPlayer saved; };
static MeleeWebPlayerContext* owners[Gm_Player_NumMax];
static int fail(char* e,size_t n,const char* message){if(e&&n)snprintf(e,n,"%s",message);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
MeleeWebPlayerContext* melee_web_player_context_begin(const MeleeWebPlayerSettings* s,char* e,size_t n)
{
    if(!s||s->slot>=Gm_Player_NumMax||s->controller>=4||s->stocks<1||s->stocks>99||
       (s->facing!=1&&s->facing!=-1)){
        fail(e,n,"Player settings require a valid slot/controller, 1..99 stocks and facing +/-1");return NULL;
    }
    for(int i=0;i<3;i++)if(!isfinite(s->position[i])){fail(e,n,"Player position must be finite");return NULL;}
    StaticPlayer* p=Player_GetPtrForSlot(s->slot);
    if(owners[s->slot]||p->player_entity[0]||p->player_entity[1]||p->player_state){
        fail(e,n,"Player slot already has context, state or fighter ownership");return NULL;
    }
    /* Original reset reads the previous transformation indices before writing
     * their defaults. Validate them before entering that source routine. */
    if(p->transformed[0]>1||p->transformed[1]>1){fail(e,n,"Previous source player transformation indices are invalid");return NULL;}
    MeleeWebPlayerContext* h=malloc(sizeof(*h));
    if(!h){fail(e,n,"Cannot allocate saved source player context");return NULL;}
    h->slot=s->slot;memcpy(&h->saved,p,sizeof(*p));
    Player_InitOrResetPlayer(s->slot);
    plStale_ResetStaleMoveTableForPlayer(s->slot);
    Player_SetPlayerCharacter(s->slot,CKIND_MARIO);
    Player_SetSlottype(s->slot,Gm_PKind_Human);
    Player_SetControllerIndex(s->slot,s->controller);
    Player_SetPlayerId(s->slot,s->slot);
    Player_SetCostumeId(s->slot,0);
    Player_SetTeam(s->slot,0);
    Player_SetStocks(s->slot,s->stocks);
    Player_SetFacingDirection(s->slot,s->facing);
    Vec3 pos={s->position[0],s->position[1],s->position[2]};
    Player_80032768(s->slot,&pos);
    owners[s->slot]=h;ok(e,n);return h;
}
int melee_web_player_context_stats(const MeleeWebPlayerContext* h,MeleeWebPlayerStats* out,char* e,size_t n)
{
    if(!h||!out||h->slot>=Gm_Player_NumMax||owners[h->slot]!=h)return fail(e,n,"Source player context is not owned");
    int s=h->slot;StaticPlayer* p=Player_GetPtrForSlot(s);Vec3 pos;
    if(p->transformed[0]>1||p->transformed[1]>1)return fail(e,n,"Source player transformation indices are invalid");
    memset(out,0,sizeof(*out));
    out->slot_type=Player_GetPlayerSlotType(s);out->character=Player_GetPlayerCharacter(s);
    out->controller=Player_GetControllerIndex(s);out->player_id=Player_GetPlayerId(s);
    out->costume=Player_GetCostumeId(s);out->stocks=Player_GetStocks(s);
    out->cpu_type=Player_GetCpuType(s);out->cpu_level=Player_GetCpuLevel(s);
    out->state=Player_GetPlayerState(s);out->damage=Player_GetDamage(s);
    Player_LoadPlayerCoords(s,&pos);memcpy(out->position,&pos,sizeof(pos));
    out->facing=Player_GetFacingDirection(s);out->model_scale=Player_GetModelScale(s);
    out->attack_ratio=Player_GetAttackRatio(s);out->defense_ratio=Player_GetDefenseRatio(s);
    out->stale_index=Player_GetStaleMoveTableIndexPtr(s)->current_index;
    out->live_entities=(p->player_entity[0]!=NULL)+(p->player_entity[1]!=NULL);
    return ok(e,n);
}
int melee_web_player_context_end(MeleeWebPlayerContext* h,char* e,size_t n)
{
    if(!h)return ok(e,n);
    if(h->slot>=Gm_Player_NumMax||owners[h->slot]!=h)return fail(e,n,"Source player context ownership changed");
    StaticPlayer* p=Player_GetPtrForSlot(h->slot);
    if(p->player_entity[0]||p->player_entity[1])return fail(e,n,"Unload original player fighters before restoring source context");
    memcpy(p,&h->saved,sizeof(*p));owners[h->slot]=NULL;free(h);return ok(e,n);
}
