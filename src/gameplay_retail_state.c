// Semantic state at the same gameplay phase as reference_replay_capture.py.
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <melee/gm/forward.h>
#include <sysdolphin/baselib/random.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
extern struct gm_80479D58_t gm_80479D58;
extern u32 gm_GetFrameCount(void);
static uint32_t bits(float f){uint32_t u;memcpy(&u,&f,4);return u;}
static void vec(const Vec3* v){printf("[\"%08x\",\"%08x\",\"%08x\"]",bits(v->x),bits(v->y),bits(v->z));}
void melee_web_retail_state(void){
    if(!seed_ptr)abort();
    printf("\"rng\":%u,\"match_frame\":%u,\"fighters\":[",*seed_ptr,gm_GetFrameCount());
    for(unsigned slot=0;slot<4 && Player_GetPlayerSlotType(slot)!=Gm_PKind_NA;slot++){
        StaticPlayer* p=Player_GetPtrForSlot(slot);
        if(!p||!p->player_entity[0])abort();
        Fighter* fp=p->player_entity[0]->user_data;
        if(slot)printf(",");
        printf("{\"slot\":%u,\"kind\":%u,\"motion\":%u,\"animation\":%u,\"ground_air\":%u,\"facing_bits\":\"%08x\",\"position_bits\":",slot,fp->kind,fp->motion_id,fp->anim_id,fp->ground_or_air,bits(fp->facing_dir));
        vec(&fp->cur_pos);printf(",\"velocity_bits\":");vec(&fp->self_vel);printf(",\"knockback_bits\":");vec(&fp->x8c_kb_vel);
        printf(",\"animation_frame_bits\":\"%08x\",\"animation_speed_bits\":\"%08x\",\"damage_bits\":\"%08x\",\"shield_bits\":\"%08x\",\"stocks\":%d,\"input_hex\":\"",bits(fp->cur_anim_frame),bits(fp->frame_speed_mul),bits(fp->dmg.x1830_percent),bits(fp->shield_health),Player_GetStocks(slot));
        // Input consists of 20 native four-byte words followed by 28 byte timers.
        // Encode each semantic word big-endian; never dump native struct bytes.
        _Static_assert(sizeof(fp->input)==0x50,"Fighter input layout changed");
        for(unsigned i=0;i<0x50;i+=4){uint32_t u;memcpy(&u,(const char*)&fp->input+i,4);printf("%08x",u);}
        for(unsigned i=0;i<0x1c;i++)printf("%02x",((const unsigned char*)&fp->x670_timer_lstick_tilt_x)[i]);
        printf("\"}");
    }
    printf("]");
}
