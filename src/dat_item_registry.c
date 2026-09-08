#include "dat_item_registry.h"
#include <melee/it/it_3F14.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
_Static_assert(It_Kind_Kuriboh==MELEE_WEB_ITEM_REGISTRY_FIRST_KIND,"Original item registry first kind");
_Static_assert(It_PKind_Start-It_Kind_Kuriboh==MELEE_WEB_ITEM_REGISTRY_COUNT,"Original item registry source extent");
struct MeleeWebItemRegistry { Article* articles[MELEE_WEB_ITEM_REGISTRY_COUNT];Article** saved; };
static MeleeWebItemRegistry* owner;
static int fail(char* e,size_t n,const char* m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
MeleeWebItemRegistry* melee_web_item_registry_begin(void* const* articles,uint32_t count,char* e,size_t n){
    if(owner||!articles||count!=MELEE_WEB_ITEM_REGISTRY_COUNT){fail(e,n,"Item registry requires exclusive ownership and exact source table extent");return NULL;}
    MeleeWebItemRegistry* h=malloc(sizeof(*h));
    if(!h){fail(e,n,"Cannot allocate source item registry table");return NULL;}
    for(uint32_t i=0;i<count;i++)h->articles[i]=articles[i];
    h->saved=it_804D6D38;it_804D6D38=h->articles;owner=h;ok(e,n);return h;
}
int melee_web_item_registry_lookup(MeleeWebItemRegistry* h,uint32_t kind,void** article,char* e,size_t n){
    if(!h||owner!=h||it_804D6D38!=h->articles)return fail(e,n,"Item registry publication was replaced");
    if(!article||kind<MELEE_WEB_ITEM_REGISTRY_FIRST_KIND||kind>=MELEE_WEB_ITEM_REGISTRY_FIRST_KIND+MELEE_WEB_ITEM_REGISTRY_COUNT)return fail(e,n,"Item kind is outside original character registry");
    *article=h->articles[kind-MELEE_WEB_ITEM_REGISTRY_FIRST_KIND];return ok(e,n);
}
int melee_web_item_registry_end(MeleeWebItemRegistry* h,char* e,size_t n){
    if(!h)return ok(e,n);
    if(owner!=h||it_804D6D38!=h->articles)return fail(e,n,"Item registry publication was replaced");
    it_804D6D38=h->saved;owner=NULL;free(h);return ok(e,n);
}
