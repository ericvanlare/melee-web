#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static uint32_t bits(float f){uint32_t u;memcpy(&u,&f,4);return u;}
void melee_web_trajectory_sample(unsigned tick){
    Fighter* fp=Player_GetPtrForSlot(0)->player_entity[0]->user_data;
    printf("{\"tick\":%u,\"motion\":%d,\"ground_air\":%d,\"y\":%.9g,\"y_bits\":\"%08x\",\"vy\":%.9g,\"vy_bits\":\"%08x\",\"frame\":%.9g,\"held\":%u}\n",
        tick,fp->motion_id,fp->ground_or_air,fp->cur_pos.y,bits(fp->cur_pos.y),fp->self_vel.y,bits(fp->self_vel.y),fp->cur_anim_frame,fp->input.held_buttons[0]);
}
