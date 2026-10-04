// Semantic state at the same gameplay phase as reference_replay_capture.py.
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <melee/gm/forward.h>
#include <sysdolphin/baselib/random.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sysdolphin/baselib/gobj.h>
extern struct gm_80479D58_t gm_80479D58;
extern u32 gm_GetFrameCount(void);
static uint32_t bits(float f){uint32_t u;memcpy(&u,&f,4);return u;}
static void vec(const Vec3* v){printf("[\"%08x\",\"%08x\",\"%08x\"]",bits(v->x),bits(v->y),bits(v->z));}
static HSD_GObj* observed_entities[4];
static uint32_t entity_generations[4];
static uint32_t observed_match = UINT_MAX;
static HSD_GObj* observed_entities_v10[4][2];
static uint32_t entity_generations_v10[4][2];
static uint8_t entity_seen_v10[4][2];
static uint32_t observed_match_v10 = UINT_MAX;
uint32_t melee_web_retail_rng(void){
    if(!seed_ptr)abort();
    return *seed_ptr;
}
static void fighter_fields(unsigned slot,const Fighter* fp){
        if(!fp)abort();
        printf("\"slot\":%u,\"kind\":%u,\"motion\":%u,\"animation\":%u,\"ground_air\":%u,\"facing_bits\":\"%08x\",\"position_bits\":",slot,fp->kind,fp->motion_id,fp->anim_id,fp->ground_or_air,bits(fp->facing_dir));
        vec(&fp->cur_pos);printf(",\"velocity_bits\":");vec(&fp->self_vel);printf(",\"knockback_bits\":");vec(&fp->x8c_kb_vel);
        printf(",\"animation_frame_bits\":\"%08x\",\"animation_speed_bits\":\"%08x\",\"damage_bits\":\"%08x\",\"shield_bits\":\"%08x\",\"stocks\":%d,\"input_hex\":\"",bits(fp->cur_anim_frame),bits(fp->frame_speed_mul),bits(fp->dmg.x1830_percent),bits(fp->shield_health),Player_GetStocks(slot));
        // Input consists of 20 native four-byte words followed by 28 byte timers.
        // Encode each semantic word big-endian; never dump native struct bytes.
        _Static_assert(sizeof(fp->input)==0x50,"Fighter input layout changed");
        for(unsigned i=0;i<0x50;i+=4){uint32_t u;memcpy(&u,(const char*)&fp->input+i,4);printf("%08x",u);}
        for(unsigned i=0;i<0x1c;i++)printf("%02x",((const unsigned char*)&fp->x670_timer_lstick_tilt_x)[i]);
        printf("\"");
}
void melee_web_retail_state(void){
    if(!seed_ptr)abort();
    printf("\"rng\":%u,\"match_frame\":%u,\"fighters\":[",*seed_ptr,gm_GetFrameCount());
    for(unsigned slot=0;slot<4 && Player_GetPlayerSlotType(slot)!=Gm_PKind_NA;slot++){
        StaticPlayer* p=Player_GetPtrForSlot(slot);
        if(!p||!p->player_entity[0])abort();
        if(slot)printf(",");
        printf("{");fighter_fields(slot,p->player_entity[0]->user_data);printf("}");
    }
    printf("]");
}
// Standalone entity diagnostics retain follower ordinals for their dedicated
// probe.  The v8 whole-session stream intentionally emits only primary
// Fighter state; v9 uses the checked identity-only indexed path below.
void melee_web_retail_entities(void){
    printf(",\"fighter_entities\":[");
    unsigned emitted=0;
    for(unsigned slot=0;slot<4 && Player_GetPlayerSlotType(slot)!=Gm_PKind_NA;slot++){
        const StaticPlayer* p=Player_GetPtrForSlot(slot);
        if(!p)abort();
        for(unsigned entity=0;entity<sizeof(p->player_entity)/sizeof(p->player_entity[0]);entity++){
            if(!p->player_entity[entity])continue;
            if(emitted++)printf(",");
            printf("{\"entity_index\":%u,",entity);
            fighter_fields(slot,p->player_entity[entity]->user_data);printf("}");
        }
    }
    printf("]");
}
void melee_web_retail_entities_reset(void){
    memset(observed_entities,0,sizeof(observed_entities));
    memset(entity_generations,0,sizeof(entity_generations));
    observed_match=UINT_MAX;
}
void melee_web_retail_entities_index(uint32_t match_index){
    if(match_index>=3)abort();
    if(observed_match==UINT_MAX||match_index!=observed_match){
        if(observed_match!=UINT_MAX&&match_index!=observed_match+1)abort();
        memset(observed_entities,0,sizeof(observed_entities));
        memset(entity_generations,0,sizeof(entity_generations));
        observed_match=match_index;
    }
    printf(",\"fighter_entities\":[");
    for(unsigned slot=0;slot<4;slot++){
        StaticPlayer* player=Player_GetPtrForSlot(slot);
        if(!player||Player_GetPlayerSlotType(slot)!=Gm_PKind_Cpu||
           !player->player_entity[0]||player->player_entity[1])abort();
        HSD_GObj* entity=player->player_entity[0];
        Fighter* fighter=entity->user_data;
        if(!fighter||fighter->gobj!=entity||fighter->player_id!=slot)abort();
        for(unsigned earlier=0;earlier<slot;earlier++)
            if(observed_entities[earlier]==entity)abort();
        if(observed_entities[slot]&&observed_entities[slot]!=entity){
            if(entity_generations[slot]==UINT_MAX)abort();
            entity_generations[slot]++;
        }
        observed_entities[slot]=entity;
        if(slot)printf(",");
        printf("{\"match_index\":%u,\"slot\":%u,\"entity_index\":0,\"generation\":%u,\"fighter_player_id\":%u,\"fighter_gobj_linked\":true}",
               match_index,slot,entity_generations[slot],fighter->player_id);
    }
    if(Player_GetPlayerSlotType(4)!=Gm_PKind_NA||
       Player_GetPlayerSlotType(5)!=Gm_PKind_NA)abort();
    printf("]");
}

void melee_web_retail_state_v10(void){
    if(!seed_ptr)abort();
    printf("\"rng\":%u,\"match_frame\":%u",*seed_ptr,gm_GetFrameCount());
}

void melee_web_retail_entities_reset_v10(void){
    memset(observed_entities_v10,0,sizeof(observed_entities_v10));
    memset(entity_generations_v10,0,sizeof(entity_generations_v10));
    memset(entity_seen_v10,0,sizeof(entity_seen_v10));
    observed_match_v10=UINT_MAX;
}

void melee_web_retail_entities_index_v10(uint32_t match_index,int require_primaries){
    if(match_index>=3)abort();
    if(observed_match_v10==UINT_MAX||match_index!=observed_match_v10){
        if(observed_match_v10!=UINT_MAX&&match_index!=observed_match_v10+1)abort();
        memset(observed_entities_v10,0,sizeof(observed_entities_v10));
        memset(entity_generations_v10,0,sizeof(entity_generations_v10));
        memset(entity_seen_v10,0,sizeof(entity_seen_v10));
        observed_match_v10=match_index;
    }
    HSD_GObj* seen_gobjs[8]={0};
    unsigned seen_count=0;
    uint8_t has_primary[4]={0,0,0,0};
    printf(",\"fighter_entities\":[");
    unsigned emitted=0;
    for(unsigned slot=0;slot<4;slot++){
        StaticPlayer* player=Player_GetPtrForSlot(slot);
        if(!player||Player_GetPlayerSlotType(slot)!=Gm_PKind_Cpu)abort();
        for(unsigned entity_index=0;entity_index<2;entity_index++){
            HSD_GObj* entity=player->player_entity[entity_index];
            if(entity_seen_v10[slot][entity_index]&&
               observed_entities_v10[slot][entity_index]!=entity){
                if(entity_generations_v10[slot][entity_index]==UINT_MAX)abort();
                entity_generations_v10[slot][entity_index]++;
            }
            entity_seen_v10[slot][entity_index]=1;
            observed_entities_v10[slot][entity_index]=entity;
            if(!entity)continue;
            Fighter* fighter=entity->user_data;
            if(!fighter||fighter->gobj!=entity||fighter->player_id!=slot)abort();
            for(unsigned earlier=0;earlier<seen_count;earlier++)
                if(seen_gobjs[earlier]==entity)abort();
            if(seen_count>=sizeof(seen_gobjs)/sizeof(seen_gobjs[0]))abort();
            seen_gobjs[seen_count++]=entity;
            if(entity_index==0)has_primary[slot]=1;
            if(emitted++)printf(",");
            printf("{\"match_index\":%u,\"entity_index\":%u,\"generation\":%u,"
                   "\"fighter_player_id\":%u,\"fighter_gobj_linked\":true,",
                   match_index,entity_index,entity_generations_v10[slot][entity_index],
                   fighter->player_id);
            fighter_fields(slot,fighter);
            printf("}");
        }
    }
    if(Player_GetPlayerSlotType(4)!=Gm_PKind_NA||
       Player_GetPlayerSlotType(5)!=Gm_PKind_NA)abort();
    if(require_primaries){
        for(unsigned slot=0;slot<4;slot++)if(!has_primary[slot])abort();
    }
    if(!emitted)abort();
    printf("]");
}
