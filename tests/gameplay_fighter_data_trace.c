#include "gameplay_fighter_data.h"
#include <melee/ft/types.h>
#include "gameplay_article_data.h"
#include <melee/it/types.h>
#include <sysdolphin/baselib/jobj.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"fighter data check failed: %s\n",#c); abort(); } } while(0)
void melee_web_test_fighter_data(void* data,int actual) {
    ftData* d=data;
    CHECK(d && d->x0 && d->ext_attr && d->x8 && d->x30 && d->x3C);
    CHECK(d->x30->count==(actual?10:1) && d->x2C->dynamicsNum==0 && d->x2C->x4==0);
    CHECK(d->x8->x0.model_num<=11 && d->x8->x8.x8==(actual?2:0));
    CHECK((!actual || d->x8->x8.xC[0]) && d->x4C_sfx && d->x50);
    CHECK(d->xC && d->x10 && d->x24 && d->x58 && d->x48_items);
    CHECK(d->x48_items[0] && d->x48_items[2] && !d->x48_items[1] && !d->x48_items[3]);
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
