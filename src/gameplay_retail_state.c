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
#include "gameplay_retail_state.h"
extern struct gm_80479D58_t gm_80479D58;
extern u32 gm_GetFrameCount(void);
static uint32_t bits(float f){uint32_t u;memcpy(&u,&f,4);return u;}
static HSD_GObj* observed_entities[4];
static uint32_t entity_generations[4];
static uint32_t observed_match = UINT_MAX;
uint32_t melee_web_retail_rng(void){
    if(!seed_ptr)abort();
    return *seed_ptr;
}
static void vector_bits(const Vec3* v,uint32_t out[3]){out[0]=bits(v->x);out[1]=bits(v->y);out[2]=bits(v->z);}
/* The declared Fighter field list. The JSON printer below and the networked
 * checksum both visit exactly these fields in this order. */
void melee_web_fighter_fields_visit(unsigned slot,const Fighter* fp,
                                    const MeleeWebFighterFieldVisitor* visitor,void* context){
        if(!fp||!visitor)abort();
        uint32_t vector[3];
        visitor->unsigned_field(context,"slot",slot);
        visitor->unsigned_field(context,"kind",(uint32_t)fp->kind);
        visitor->unsigned_field(context,"motion",(uint32_t)fp->motion_id);
        visitor->unsigned_field(context,"animation",(uint32_t)fp->anim_id);
        visitor->unsigned_field(context,"ground_air",(uint32_t)fp->ground_or_air);
        visitor->bits_field(context,"facing_bits",bits(fp->facing_dir));
        vector_bits(&fp->cur_pos,vector);visitor->vector_field(context,"position_bits",vector);
        vector_bits(&fp->self_vel,vector);visitor->vector_field(context,"velocity_bits",vector);
        vector_bits(&fp->x8c_kb_vel,vector);visitor->vector_field(context,"knockback_bits",vector);
        visitor->bits_field(context,"animation_frame_bits",bits(fp->cur_anim_frame));
        visitor->bits_field(context,"animation_speed_bits",bits(fp->frame_speed_mul));
        visitor->bits_field(context,"damage_bits",bits(fp->dmg.x1830_percent));
        visitor->bits_field(context,"shield_bits",bits(fp->shield_health));
        visitor->signed_field(context,"stocks",Player_GetStocks(slot));
        // Input consists of 20 native four-byte words followed by 28 byte timers.
        // Pass each semantic word by value; never expose native struct bytes.
        _Static_assert(sizeof(fp->input)==0x50,"Fighter input layout changed");
        uint32_t words[20];uint8_t timers[28];
        for(unsigned i=0;i<20;i++)memcpy(&words[i],(const char*)&fp->input+i*4,4);
        memcpy(timers,&fp->x670_timer_lstick_tilt_x,sizeof(timers));
        visitor->input_field(context,"input_hex",words,timers);
}
// JSON printer: byte-identical to the historical v8/v9 retail stream.
typedef struct JsonFieldContext { int first_field; } JsonFieldContext;
static void json_name(void* context,const char* name){
        JsonFieldContext* writer=(JsonFieldContext*)context;
        printf(writer->first_field?"\"%s\":":",\"%s\":",name);
        writer->first_field=0;
}
static void json_unsigned(void* context,const char* name,uint32_t value){json_name(context,name);printf("%u",value);}
static void json_signed(void* context,const char* name,int32_t value){json_name(context,name);printf("%d",(int)value);}
static void json_bits(void* context,const char* name,uint32_t value){json_name(context,name);printf("\"%08x\"",value);}
static void json_vector(void* context,const char* name,const uint32_t value[3]){
        json_name(context,name);printf("[\"%08x\",\"%08x\",\"%08x\"]",value[0],value[1],value[2]);
}
static void json_input(void* context,const char* name,const uint32_t words[20],const uint8_t timers[28]){
        json_name(context,name);printf("\"");
        for(unsigned i=0;i<20;i++)printf("%08x",words[i]);
        for(unsigned i=0;i<28;i++)printf("%02x",timers[i]);
        printf("\"");
}
static const MeleeWebFighterFieldVisitor json_fields={
        json_unsigned,json_signed,json_bits,json_vector,json_input};
static void fighter_fields(unsigned slot,const Fighter* fp){
        JsonFieldContext context={1};
        melee_web_fighter_fields_visit(slot,fp,&json_fields,&context);
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

int melee_web_retail_primary_identity(unsigned slot, const HSD_GObj* entity,
    uint32_t* match_index, uint32_t* generation)
{
    if(slot>=4||!entity||!match_index||!generation||
       observed_match==UINT_MAX||observed_entities[slot]!=entity)return 0;
    const StaticPlayer* player=Player_GetPtrForSlot(slot);
    const Fighter* fighter=entity->user_data;
    if(!player||Player_GetPlayerSlotType(slot)!=Gm_PKind_Cpu||
       player->player_entity[0]!=entity||player->player_entity[1]||
       !fighter||fighter->gobj!=entity||fighter->player_id!=slot)return 0;
    *match_index=observed_match;
    *generation=entity_generations[slot];
    return 1;
}
