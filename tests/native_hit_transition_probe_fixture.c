/* Synthetic game objects; the observer and generation accessor are real C. */
#include "gameplay_hit_transition_probe.h"
#include "gameplay_retail_state.h"
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
StaticPlayer test_players[4];
Fighter test_fighters[4];
HSD_GObj test_entities[4];
uint32_t test_seed=0x12345678;
uint32_t* seed_ptr=&test_seed;
int selected=1;
StaticPlayer* Player_GetPtrForSlot(unsigned slot){return slot<4?&test_players[slot]:NULL;}
int Player_GetPlayerSlotType(unsigned slot){return slot<4?Gm_PKind_Cpu:Gm_PKind_NA;}
int melee_web_hit_probe_test_selected(size_t cursor){return selected&&cursor>=5238&&cursor<=5240;}
void melee_web_hit_probe_test_emit(const char* line){puts(line);}
void test_tracker_init(void);
void test_tracker_bump(void);
int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    const char* mode=argv[1];
    for(unsigned i=0;i<4;++i){
        test_entities[i].user_data=&test_fighters[i];
        test_fighters[i].gobj=&test_entities[i];
        test_fighters[i].player_id=i;test_fighters[i].kind=i==1?FTKIND_KOOPA:0;
        test_players[i].player_entity[0]=&test_entities[i];
        test_fighters[i].dmg.x1868_source=&test_entities[0];
        test_fighters[i].dmg.x1830_percent=11.0f;
        test_fighters[i].dmg.x18a0=-0.0f;
    }
    test_tracker_init();
    Fighter saved[4];memcpy(saved,test_fighters,sizeof(saved));
    StaticPlayer saved_players[4];memcpy(saved_players,test_players,sizeof(saved_players));
    HSD_GObj saved_entities[4];memcpy(saved_entities,test_entities,sizeof(saved_entities));
    if(!strcmp(mode,"accessor")){
        uint32_t match,generation;
        if(!melee_web_retail_primary_identity(1,&test_entities[1],&match,&generation)||
           match!=0||generation!=0)return 6;
        if(melee_web_retail_primary_identity(4,&test_entities[1],&match,&generation)||
           melee_web_retail_primary_identity(1,NULL,&match,&generation)||
           melee_web_retail_primary_identity(1,&test_entities[0],&match,&generation))return 7;
        return 0;
    }
    for(size_t cursor=5238;cursor<=5240;++cursor){
        selected=strcmp(mode,"disabled")!=0;
        melee_web_hit_probe_cursor(strcmp(mode,"unselected")==0?5200:cursor);
        if(strcmp(mode,"bad_identity")==0)test_fighters[1].gobj=NULL;
        if(strcmp(mode,"generation_changed")==0)test_tracker_bump();
        if(strcmp(mode,"wrong_player")==0)test_fighters[1].player_id=0;
        if(strcmp(mode,"follower")==0)test_players[1].player_entity[1]=&test_entities[0];
        if(strcmp(mode,"zero")&&strcmp(mode,"disabled")&&strcmp(mode,"unselected")){
            size_t logs=!strcmp(mode,"log20")?20:!strcmp(mode,"count21")?21:1;
            unsigned collision=melee_web_hit_probe_begin(&test_entities[1],0,0,0,0,0,0,0,0);
            unsigned pass=melee_web_hit_probe_pass_begin(&test_entities[1]);
            melee_web_hit_probe_pass_end(pass);
            unsigned damage=melee_web_hit_probe_begin(&test_entities[1],1,0,0,0,0,0,logs,1);
            for(size_t i=0;i<logs;++i)
                melee_web_hit_probe_log(damage,!strcmp(mode,"log_gap")?i+1:i,1,0,
                    !strcmp(mode,"unattributed")?(HSD_GObj*)(uintptr_t)0x1234:&test_entities[0],
                    (void*)(uintptr_t)0x1000,(void*)(uintptr_t)0x2000,1.0f,-0.0f,3.0f,11.0f,32);
            if(!strcmp(mode,"wrong_return"))melee_web_hit_probe_end(&test_entities[1],collision);
            if(!strcmp(mode,"missing_return")){melee_web_hit_probe_scheduler_return();return 3;}
            melee_web_hit_probe_end(&test_entities[1],damage);
            if(!strcmp(mode,"duplicate_return"))melee_web_hit_probe_end(&test_entities[1],damage);
            melee_web_hit_probe_end(&test_entities[1],collision);
            unsigned motions=!strcmp(mode,"overflow")?32:2;
            for(unsigned i=0;i<motions;++i){
                unsigned motion=melee_web_hit_probe_begin(&test_entities[1],2,90,0x1234,-0.0f,1.0f,0.5f,0,0);
                melee_web_hit_probe_end(&test_entities[1],motion);
            }
        }
        /* Disabled paths must not inspect this deliberately unreadable object. */
        if(!selected||!strcmp(mode,"unselected")){
            unsigned id=melee_web_hit_probe_begin((HSD_GObj*)(uintptr_t)1,0,0,0,0,0,0,0,0);
            if(id)return 4;
        }
        if(!strcmp(mode,"missing_scheduler")){melee_web_hit_probe_cursor(cursor+1);return 3;}
        melee_web_hit_probe_scheduler_return();
    }
    if(memcmp(saved,test_fighters,sizeof(saved))||
       memcmp(saved_players,test_players,sizeof(saved_players))||
       memcmp(saved_entities,test_entities,sizeof(saved_entities))||test_seed!=0x12345678)return 5;
    return 0;
}
