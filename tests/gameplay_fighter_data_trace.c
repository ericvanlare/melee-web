#include "gameplay_fighter_data.h"
#include <melee/ft/types.h>
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
    for(unsigned i=4;i<8;++i)CHECK(!melee_web_fighter_data_article(d,i));
    CHECK(!melee_web_fighter_data_article(NULL,0));
    { ftData empty={0}; CHECK(!melee_web_fighter_data_article(&empty,0)); }
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
