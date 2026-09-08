#include "gameplay_player_context.h"
#include <melee/pl/player.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"CHECK failed at %d: %s\n",__LINE__,#c);abort();}}while(0)
int main(void){
    char error[256];MeleeWebPlayerSettings settings={0,0,4,{12,3,0},1};MeleeWebPlayerStats stats;
    StaticPlayer* p=Player_GetPtrForSlot(0);StaticPlayer original;memcpy(&original,p,sizeof(original));
    Player_SetModelScale(0,1.75f);p->stale_moves.current_index=7;
    StaticPlayer saved;memcpy(&saved,p,sizeof(saved));
    for(int pass=0;pass<2;pass++){
        MeleeWebPlayerContext* h=melee_web_player_context_begin(&settings,error,sizeof(error));CHECK(h);
        CHECK(!melee_web_player_context_begin(&settings,error,sizeof(error)));
        CHECK(melee_web_player_context_stats(h,&stats,error,sizeof(error)));
        CHECK(stats.character==CKIND_MARIO&&stats.slot_type==Gm_PKind_Human);
        CHECK(stats.controller==0&&stats.player_id==0&&stats.costume==0&&stats.stocks==4);
        CHECK(stats.state==0&&stats.damage==0&&stats.live_entities==0&&stats.stale_index==0);
        CHECK(stats.cpu_type==4&&stats.cpu_level==0&&stats.model_scale==1&&stats.attack_ratio==1&&stats.defense_ratio==1);
        CHECK(stats.position[0]==12&&stats.position[1]==3&&stats.position[2]==0&&stats.facing==1);
        CHECK(p->transformed[0]==0&&p->transformed[1]==1);
        for(int i=0;i<10;i++)CHECK(!p->stale_moves.StaleMoves[i].move_id&&!p->stale_moves.StaleMoves[i].attack_instance);
        CHECK(melee_web_player_context_end(h,error,sizeof(error)));
        CHECK(!memcmp(p,&saved,sizeof(saved)));
    }
    settings.controller=4;CHECK(!melee_web_player_context_begin(&settings,error,sizeof(error)));
    settings.controller=0;settings.facing=0;CHECK(!melee_web_player_context_begin(&settings,error,sizeof(error)));
    memcpy(p,&original,sizeof(original));
    puts("Original Player reset, settings, getters, stale reset, scoped restore and restart passed");return 0;
}
