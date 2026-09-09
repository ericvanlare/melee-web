#include "gameplay_player_context.h"
#include <melee/pl/player.h>
#include <melee/gm/gm_1601.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"CHECK failed at %d: %s\n",__LINE__,#c);abort();}}while(0)
int main(void){
    /* Original executable retains ckind as the default base and returns the
     * computed float through the wrapper. Cover the roster, costume stride,
     * transform aliases and special icons; zero is only Captain Falcon. */
    const int icon_bases[26]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,18,19,20,21,22,23,24};
    for(int kind=0;kind<26;kind++)for(int costume=0;costume<5;costume++)
        CHECK(gm_80168B34((CharacterKind)kind,0,costume)==icon_bases[kind]+30.0f*costume);
    CHECK(gm_80168B34(CKIND_ZELDA,7,2)==85.0f);
    CHECK(gm_80168B34(CKIND_SEAK,7,2)==85.0f);
    CHECK(gm_80168B34(CKIND_GKOOPS,0,0)==58.0f);
    CHECK(gm_80168B34(CKIND_BOY,0,0)==26.0f);
    CHECK(gm_80168B34(CKIND_GIRL,0,0)==26.0f);
    CHECK(gm_80168B34(CKIND_MASTERH,0,0)==28.0f);
    CHECK(gm_80168B34(CKIND_CREZYH,0,0)==27.0f);
    CHECK(gm_80168B34(CHKIND_SANDBAG,0,0)==59.0f);
    CHECK(gm_80168B34(CHKIND_POPO,0,1)==44.0f);
    char error[256];MeleeWebPlayerSettings settings={0,0,4,{12,3,0},1,2,3};MeleeWebPlayerStats stats;
    StaticPlayer* p=Player_GetPtrForSlot(0);StaticPlayer original;memcpy(&original,p,sizeof(original));
    Player_SetModelScale(0,1.75f);p->stale_moves.current_index=7;
    StaticPlayer saved;memcpy(&saved,p,sizeof(saved));
    for(int pass=0;pass<2;pass++){
        settings.controller=pass;
        MeleeWebPlayerContext* h=melee_web_player_context_begin(&settings,error,sizeof(error));CHECK(h);
        CHECK(!melee_web_player_context_begin(&settings,error,sizeof(error)));
        CHECK(melee_web_player_context_stats(h,&stats,error,sizeof(error)));
        CHECK(stats.character==CKIND_MARIO&&stats.slot_type==Gm_PKind_Human);
        CHECK(stats.controller==(unsigned)pass&&stats.player_id==0&&stats.costume==2&&stats.stocks==4);
        CHECK(stats.state==0&&stats.damage==0&&stats.live_entities==0&&stats.stale_index==0);
        CHECK(stats.cpu_type==4&&stats.cpu_level==0&&stats.model_scale==1&&stats.attack_ratio==1&&stats.defense_ratio==1);
        CHECK(stats.position[0]==12&&stats.position[1]==3&&stats.position[2]==0&&stats.facing==1);
        CHECK(gm_80168BF8(0)==68.0f); /* Mario costume 2, not Falcon. */
        CHECK(Player_GetControllerIndex(0)==3); /* tint is independent of the input port */
        CHECK(Player_GetNametagSlotID(0)==0x78);
        CHECK(p->transformed[0]==0&&p->transformed[1]==1);
        for(int i=0;i<10;i++)CHECK(!p->stale_moves.StaleMoves[i].move_id&&!p->stale_moves.StaleMoves[i].attack_instance);
        CHECK(melee_web_player_context_end(h,error,sizeof(error)));
        CHECK(!memcmp(p,&saved,sizeof(saved)));
    }
    settings.controller=4;CHECK(!melee_web_player_context_begin(&settings,error,sizeof(error)));
    settings.controller=0;settings.sub_color=5;CHECK(!melee_web_player_context_begin(&settings,error,sizeof(error)));
    settings.sub_color=3;settings.facing=0;CHECK(!melee_web_player_context_begin(&settings,error,sizeof(error)));
    settings.facing=1;settings.fighter_kind=FTKIND_FALCO;settings.costume=3;
    MeleeWebPlayerContext* falco=melee_web_player_context_begin(&settings,error,sizeof(error));CHECK(falco);
    CHECK(melee_web_player_context_stats(falco,&stats,error,sizeof(error)));
    CHECK(stats.character==CKIND_FALCO&&stats.costume==3);
    CHECK(melee_web_player_context_end(falco,error,sizeof(error)));
    settings.fighter_kind=FTKIND_FOX;settings.costume=0;
    MeleeWebPlayerContext* fox=melee_web_player_context_begin(&settings,error,sizeof(error));CHECK(fox);
    CHECK(melee_web_player_context_stats(fox,&stats,error,sizeof(error)));
    CHECK(stats.character==CKIND_FOX&&stats.costume==0);
    CHECK(melee_web_player_context_end(fox,error,sizeof(error)));
    memcpy(p,&original,sizeof(original));
    puts("Original Player fighter identity mapping, settings, getters, stale reset, scoped restore and restart passed");return 0;
}
