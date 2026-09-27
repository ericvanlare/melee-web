#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftCommon/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static struct ftCommonData common;
struct ftCommonData* p_ftCommonData = &common;
static float word(uint32_t u) { float f;memcpy(&f,&u,4);return f; }
static uint32_t bits(float f) { uint32_t u;memcpy(&u,&f,4);return u; }
static void check_gates(void) {
    for (unsigned mode=0;mode<3;++mode) {
        Fighter fighter={0};HSD_GObj object={0};object.user_data=&fighter;
        fighter.cur_pos=(Vec3){1.25f,-2.5f,3.75f};
        fighter.x2092=mode==0?0:9;
        fighter.ground_or_air=mode==1?GA_Air:GA_Ground;
        fighter.victim_gobj=mode==2?&object:NULL;
        Vec3 before=fighter.cur_pos;
        ftColl_80076528(&object);
        assert(fighter.x2092==(mode==0?0:8));
        assert(memcmp(&before,&fighter.cur_pos,sizeof(before))==0);
    }
}
int main(void) {
    unsigned split_differences=0,production_differences=0;
    common.x4C8=5;common.x4D0=0.025f;common.x4D4=0.075f;
    for(unsigned n=0;n<256;++n) {
        Fighter fighter={0};HSD_GObj object={0};object.user_data=&fighter;
        fighter.x2092=9;fighter.x2090=n%10;fighter.ground_or_air=GA_Ground;
        fighter.facing_dir=(n&1)?1.0f:-1.0f;
        fighter.coll_data.floor.normal.x=word(0x3eaaaaa0+(n%16));
        fighter.coll_data.floor.normal.y=word(0x3f7ffff8+(n%8));
        fighter.cur_pos=(Vec3){word(0x3e0853e0+(n%32)),word(0xbe044c00+n),19.0f};
        Vec3 initial=fighter.cur_pos;
        float f2=fighter.facing_dir*(fighter.x2090<common.x4C8?common.x4D0:common.x4D4);
        /* DOL 8007658C and 800765A0: fnmsubs rounds then negates. */
        float x=-fmaf(fighter.coll_data.floor.normal.y,f2,-initial.x);
        float y=-fmaf(-fighter.coll_data.floor.normal.x,f2,-initial.y);
        volatile float split_x=fighter.coll_data.floor.normal.y*f2;
        volatile float split_y=-fighter.coll_data.floor.normal.x*f2;
        split_differences+=bits(-(split_x-initial.x))!=bits(x)||bits(-(split_y-initial.y))!=bits(y);
        ftColl_80076528(&object);
        production_differences+=bits(fighter.cur_pos.x)!=bits(x)||bits(fighter.cur_pos.y)!=bits(y);
        assert(fighter.x2092==8&&fighter.cur_pos.z==initial.z);
    }
    check_gates();
    assert(split_differences>0);
    printf("combo push cases=256 source_differences=%u split_differences=%u\n",production_differences,split_differences);
    return production_differences!=0;
}
