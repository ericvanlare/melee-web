#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/random.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
extern u32 seed;
static uint32_t bits(float f){uint32_t u;memcpy(&u,&f,4);return u;}
void melee_web_trajectory_sample(unsigned tick){
    Fighter* fp=Player_GetPtrForSlot(0)->player_entity[0]->user_data;
    printf("{\"tick\":%u,\"motion\":%d,\"ground_air\":%d,\"y\":%.9g,\"y_bits\":\"%08x\",\"vy\":%.9g,\"vy_bits\":\"%08x\",\"frame\":%.9g,\"held\":%u}\n",
        tick,fp->motion_id,fp->ground_or_air,fp->cur_pos.y,bits(fp->cur_pos.y),fp->self_vel.y,bits(fp->self_vel.y),fp->cur_anim_frame,fp->input.held_buttons[0]);
}
static void float_json(float value){printf("{\"bits\":\"%08x\",\"value\":%.9g}",bits(value),value);}
static void vec3_json(const Vec3* value){
    printf("[");float_json(value->x);printf(",");float_json(value->y);printf(",");float_json(value->z);printf("]");
}
static Fighter* fighter_for_slot(unsigned slot){
    StaticPlayer* player=Player_GetPtrForSlot(slot);
    return player&&player->player_entity[0]?(Fighter*)player->player_entity[0]->user_data:NULL;
}
static void fighter_json(unsigned slot,Fighter* fp){
    printf("{\"slot\":%u,\"motion\":%d,\"ground_air\":%d,\"position\":",slot,fp->motion_id,fp->ground_or_air);
    vec3_json(&fp->cur_pos);printf(",\"velocity\":");vec3_json(&fp->self_vel);
    printf(",\"frame\":");float_json(fp->cur_anim_frame);
    printf(",\"stocks\":%d,\"damage\":",Player_GetStocks(slot));float_json(fp->dmg.x1830_percent);printf("}");
}
void melee_web_trajectory_sample_stock(unsigned tick){
    Fighter* p0=fighter_for_slot(0),*p1=fighter_for_slot(1);
    if(!p0||!p1||!seed_ptr){fprintf(stderr,"Trajectory stock sample lost a live fighter or source RNG at tick %u\n",tick);abort();}
    printf("{\"tick\":%u,\"rng\":{\"pointer\":\"%p\",\"value\":%u,\"default_value\":%u},\"players\":[",tick,(void*)seed_ptr,*seed_ptr,seed);
    fighter_json(0,p0);printf(",");fighter_json(1,p1);printf("]}\n");
}
