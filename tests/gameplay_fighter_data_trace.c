#include "gameplay_fighter_data.h"
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftPurin/types.h>
#include <melee/ft/kinds/ftDonkey/types.h>
#include <melee/ft/ftwaitanim.h>
#include "gameplay_article_data.h"
#include <melee/it/types.h>
#include <sysdolphin/baselib/jobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"fighter data check failed: %s\n",#c); abort(); } } while(0)
void melee_web_test_fighter_data(void* data,int actual) {
    ftData* d=data;
    CHECK(d && d->x0 && d->ext_attr && d->x8 && d->x30 && d->x3C);
    CHECK(d->x30->count==(actual?10:1) && d->x2C->dynamicsNum==0 && d->x2C->x4==0);
    CHECK(d->x8->x0.model_num<=11 && d->x8->x8.x8==(actual?2:0));
    CHECK((!actual || d->x8->x8.xC[0]) && d->x4C_sfx && d->x50);
    CHECK(d->xC && d->x10 && d->x24 && d->x54 && d->x58 && d->x48_items);
    {
        const int* effect_parts=(const int*)(uintptr_t)d->x54;
        if(!actual) CHECK(effect_parts[0]==1 && effect_parts[1]==2 && effect_parts[2]==3 &&
                          effect_parts[3]==4 && effect_parts[4]==5);
        if(actual) CHECK(effect_parts[0]==23 && effect_parts[1]==32 && effect_parts[2]==49 &&
                          effect_parts[3]==55 && effect_parts[4]==9);
    }
    CHECK(d->x48_items[0] && d->x48_items[2] && !d->x48_items[1] && !d->x48_items[3]);
    for(unsigned i=4;i<8;++i)CHECK(!melee_web_fighter_data_article(d,0,i));
    CHECK(!melee_web_fighter_data_article(NULL,0,0));
    { ftData empty={0}; CHECK(!melee_web_fighter_data_article(&empty,0,0)); }
    for(unsigned i=0;i<4;i+=2) {
        Article* article=d->x48_items[i];
        CHECK(article->x0_common_attr && melee_web_article_unresolved(article));
    }
    if(actual) CHECK(d->x58->x0==54 && d->x58->x1==48 && d->x58->x18==0.25f);

    if(actual){
        HSD_Joint joint={0};uint32_t unresolved=1U<<23;char error[128];
        CHECK(melee_web_fighter_data_set_metal(d,&joint,5,8,&unresolved,error,sizeof(error)));
        CHECK(d->x5C==&joint&&!unresolved);d->x5C=NULL;
    }
    {
        /* Publication checks category2 against the hydrated graph count, not
         * merely the source array capacity32. Rejection must be atomic. */
        struct TestCounted { unsigned count; void* data; };
        unsigned char index=8;struct TestCounted variant={1,&index},group={1,&variant};
        void* old_group=d->x8->x0.vis_table[0][2];unsigned old_models=d->x8->x0.model_num;
        HSD_Joint joint={0};uint32_t unresolved=1U<<23;char error[128];
        d->x8->x0.vis_table[0][2]=&group;d->x8->x0.model_num=1;
        CHECK(!melee_web_fighter_data_set_metal(d,&joint,1,8,&unresolved,error,sizeof(error)));
        CHECK(!d->x5C&&unresolved==(1U<<23));
        index=7;CHECK(melee_web_fighter_data_set_metal(d,&joint,1,8,&unresolved,error,sizeof(error)));
        CHECK(d->x5C==&joint&&!unresolved);
        CHECK(!melee_web_fighter_data_set_metal(d,&joint,1,8,&unresolved,error,sizeof(error)));
        d->x5C=NULL;d->x8->x0.vis_table[0][2]=old_group;d->x8->x0.model_num=old_models;
    }
    printf("Native Mario ftData: hurtboxes=%d, texture_map=%u, models=%u\n",d->x30->count,d->x8->x8.x8,d->x8->x0.model_num);
}

void melee_web_test_guard_data(const MeleeWebNativeDat* r,uint32_t root,void* data,uint32_t* mask) {
    HSD_Joint child={0},joint={0}; joint.child=&child;
    ftData* d=data;
    melee_web_fighter_data_set_guard(r,root,d,&joint,mask);
    CHECK(d->x20&&d->x20->x0[2]==&child&&d->x20->x8==0&& !(*mask&(1U<<8)));
    d->x20=NULL; /* The test's borrowed descriptor expires here. */
}

void melee_web_test_donkey_data(void* data) {
    ftData* d=data;
    CHECK(d && d->ext_attr && d->x2C && d->x2C->dynamicsNum==1 &&
          d->x2C->ftDynamicBones && d->x2C->x4==1 && d->x2C->x8 && d->x48_items);
    ftDonkeyAttributes* attrs=(ftDonkeyAttributes*)d->ext_attr;
    CHECK(attrs->motion_state==341 && attrs->x4_motion_state==351 &&
          attrs->SpecialN.x2C_MAX_ARM_SWINGS==10 &&
          attrs->SpecialN.x30_DAMAGE_PER_SWING==2 &&
          attrs->cargo_hold.x20_TURN_SPEED==6.0f &&
          attrs->cargo_hold.x24_JUMP_STARTUP_LAG==3.0f &&
          attrs->cargo_hold.x28_LANDING_LAG==15.0f);
    for(unsigned i=0;i<7;++i) CHECK(!d->x48_items[i]);
    for(unsigned i=0;i<6;++i) CHECK(!melee_web_fighter_data_article(data,FTKIND_DONKEY,i));
}

void melee_web_test_purin_data(void* data) {
    ftData* d=data;
    CHECK(d && d->ext_attr && d->x28 && d->x28[0].u.i.x==31 && d->x28[0].u.i.y==80 &&
          d->x28[1].u.i.x==32 && d->x28[1].u.i.y==20 &&
          d->x28[2].u.i.x==-1 && d->x28[2].u.i.y==-1 && d->x48_items &&
          !d->x48_items[0] && d->x48_items[1]);
    ftPurinAttributes* attrs=(ftPurinAttributes*)d->ext_attr;
    CHECK(attrs->x2C==341 && attrs->x30==-1 && attrs->x34==90 && attrs->x38==20 &&
          attrs->x70==8 && attrs->x9C==32 && attrs->specialn_vel.x==-0.13f &&
          attrs->specialn_vel.y==1.6f && (uintptr_t)attrs->xE8==0x3e800000U &&
          (uintptr_t)attrs->xEC==0x3e4ccccdU);
    CHECK(attrs->_48[0]==0x3d && attrs->_48[1]==0x4c &&
          attrs->_48[2]==0xcc && attrs->_48[3]==0xcd &&
          attrs->_60[0]==0x3f && attrs->_60[1]==0x80 && attrs->_60[4]==0x40 &&
          attrs->_B0[0]==0x41 && attrs->_B0[1]==0xa0 &&
          attrs->_F8[0]==0 && attrs->_F8[7]==0);
    /* dynamicsNum is the active body count consumed by ftCo_8009CF84.
     * Purin's authored ArticleDynamicBones table is longer: costume 2/3
     * source code indexes rows 1..4 while attaching the hat. */
    CHECK(d->x2C && d->x2C->dynamicsNum==1 && d->x2C->ftDynamicBones);
    {
        static const unsigned ids[5]={7,3,7,3,9};
        static const unsigned counts[5]={3,3,3,5,5};
        static const float z[5]={0.00001f,0.145f,0.145f,0.145f,0.145f};
        for(unsigned i=0;i<5;++i) {
            const BoneDynamicsDesc* row=&d->x2C->ftDynamicBones->array[i];
            CHECK(row->bone_id==ids[i] && row->dyn_desc.count==counts[i] &&
                  row->dyn_desc.pos.x==1.0f && row->dyn_desc.pos.y==1.0f &&
                  row->dyn_desc.pos.z==z[i] && row->dyn_desc.data);
        }
    }
    for(unsigned i=0;i<6;++i)CHECK(!melee_web_fighter_data_article(data,FTKIND_PURIN,i));
}
