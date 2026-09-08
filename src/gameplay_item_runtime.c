#include "gameplay_item_runtime.h"
#include "gameplay_bootstrap.h"
#include <melee/it/item.h>
#include <melee/it/it_3F14.h>
#include <melee/it/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(ItemCommonData)==0x160,"Original item common layout");
_Static_assert(offsetof(ItemCommonData,x48_byte)==0x48&&offsetof(ItemCommonData,x14C)==0x14c,"Original item scalar offsets");
_Static_assert(sizeof(it_804D6D40_t)==0x1c,"Original bounce parameter layout");
void* melee_web_item_common_decode(const MeleeWebNativeDat* r,uint32_t root){
    r->region(r->context,root,0x160);
    ItemCommonData* p=r->allocate(r->context,1,sizeof(*p));
    /* Source scalar words retain their bits, including float fields whose
     * recovered declarations are integers. Byte and opaque padding regions
     * are explicitly preserved in archive order. No pointers occur here. */
    for(unsigned at=0;at<0x160;at+=4){
        if(at==0x48||at==0xe4||at==0xec)memcpy((char*)p+at,r->region(r->context,root+at,4),4);
        else {uint32_t value=r->word(r->context,root+at);memcpy((char*)p+at,&value,4);}
    }
    return p;
}
void* melee_web_item_bounce_decode(const MeleeWebNativeDat* r,uint32_t root){
    r->region(r->context,root,0x1c);it_804D6D40_t* p=r->allocate(r->context,1,sizeof(*p));
    for(unsigned at=0;at<0x1c;at+=4){uint32_t value=r->word(r->context,root+at);memcpy((char*)p+at,&value,4);}
    return p;
}
#define ITEM_GLOBALS(X) \
    X(Item_804A0C64) X(Item_804A0CCC) X(Item_804A0E24) \
    X(it_804D6D00) X(it_804D6D08) X(it_804D6D0C) X(it_804D6D10) X(it_804D6D14) \
    X(it_804D6D18) X(it_804D6D1C) X(it_804A0E30) X(it_804A0E50) X(it_804A0E60) X(it_804A0E70)
struct MeleeWebItemRuntime {
#define DECLARE(name) __typeof__(name) saved_##name;
    ITEM_GLOBALS(DECLARE)
#undef DECLARE
    ItemCommonData* common;it_804D6D40_t* bounce;Fighter_804D653C_t* colors;
    Fighter_804D653C_t* owned_colors;
    uint64_t generation;
};
static MeleeWebItemRuntime* active;
static int fail(char* e,size_t n,const char* m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
MeleeWebItemRuntime* melee_web_item_runtime_begin(void* common,void* bounce,const MeleeWebColorRow* colors,size_t count,char* e,size_t n){
    if(active||!common||!bounce||!colors||!count||count>256||!melee_web_gameplay_stats().generation||((HSD_GObj**)HSD_GObj_Entities)[9]){fail(e,n,"Item startup requires checked data and an empty owned item link");return NULL;}
    MeleeWebItemRuntime* h=calloc(1,sizeof(*h));if(!h){fail(e,n,"Cannot own item runtime");return NULL;}
    h->owned_colors=calloc(count,sizeof(*h->owned_colors));if(!h->owned_colors){free(h);fail(e,n,"Cannot own item color rows");return NULL;}
    for(size_t i=0;i<count;i++){h->owned_colors[i].unk=colors[i].program;h->owned_colors[i].unk4=colors[i].priority;h->owned_colors[i].unk5=colors[i].layer;}
    h->common=it_804D6D28;h->bounce=it_804D6D40;h->colors=it_804D6D04;h->generation=melee_web_gameplay_stats().generation;
    it_804D6D28=common;it_804D6D40=bounce;it_804D6D04=h->owned_colors;
#define SAVE(name) memcpy(&h->saved_##name,&name,sizeof(name));
    ITEM_GLOBALS(SAVE)
#undef SAVE
    Item_80266FCC();active=h;if(e&&n)*e=0;return h;
}
int melee_web_item_runtime_end(MeleeWebItemRuntime* h,char* e,size_t n){
    if(!h)return 1;
    if(h!=active||h->generation!=melee_web_gameplay_stats().generation||HSD_GObj_804D781C||HSD_GObj_804D7814)return fail(e,n,"Item teardown requires its idle owned source world");
    while(((HSD_GObj**)HSD_GObj_Entities)[9])Item_8026A8EC(((HSD_GObj**)HSD_GObj_Entities)[9]);
    it_804D6D28=h->common;it_804D6D40=h->bounce;it_804D6D04=h->colors;
#define RESTORE(name) memcpy(&name,&h->saved_##name,sizeof(name));
    ITEM_GLOBALS(RESTORE)
#undef RESTORE
    active=NULL;free(h->owned_colors);free(h);if(e&&n)*e=0;return 1;
}
