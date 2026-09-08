#include "gameplay_fighter_assets.h"
#include "gameplay_action_store.h"
#include <melee/ft/ftdata.h>
#include <melee/ft/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct MeleeWebFighterAssetScope {
    uint32_t kind,costume,motion_count;
    ftData* data;
    ftData* previous_data;
    UnkCostumeStruct previous_costume;
    UnkCostumeStruct published_costume;
    void* context;
    MeleeWebFighterAssetBind bind;
    MeleeWebFighterAssetUnbind unbind;
    Fighter* fighters[6];
};
static MeleeWebFighterAssetScope* current;
static int fail(char* error,size_t size,const char* text)
{ if(error && size)snprintf(error,size,"%s",text);return 0; }
static void fatal(const char* text)
{ fprintf(stderr,"Owned fighter assets: %s\n",text);abort(); }
MeleeWebFighterAssetScope* melee_web_fighter_assets_begin(uint32_t kind,uint32_t costume,
    void* data,void* joint,void* mat,uint32_t count,void* context,
    MeleeWebFighterAssetBind bind,MeleeWebFighterAssetUnbind unbind,char* error,size_t size)
{
    if(current || kind!=FTKIND_MARIO || !data || !joint || !mat || !context || !bind || !unbind ||
        costume>=CostumeListsForeachCharacter[kind].numCostumes ||
        count!=(uint32_t)ftData_Table_Unk0[kind].count) {
        fail(error,size,"Fighter asset scope identity is invalid or another scope is active");return NULL;
    }
    ftData* native=data;
    if(!native->xC || !native->x10 || !native->x24 || !native->x5C) {
        fail(error,size,"Fighter asset scope requires decoded action, Wait and metal descriptors");return NULL;
    }
    MeleeWebFighterAssetScope* h=calloc(1,sizeof(*h));
    if(!h){fail(error,size,"Fighter asset scope allocation failed");return NULL;}
    h->kind=kind;h->costume=costume;h->motion_count=count;h->data=data;h->context=context;h->bind=bind;h->unbind=unbind;
    h->previous_data=gFtDataList[kind];h->previous_costume=CostumeListsForeachCharacter[kind].costume_list[costume];
    h->published_costume=(UnkCostumeStruct){joint,mat,0,0,0,NULL};
    gFtDataList[kind]=data;CostumeListsForeachCharacter[kind].costume_list[costume]=h->published_costume;
    current=h;if(error && size)error[0]=0;return h;
}
uint32_t melee_web_fighter_assets_live(const MeleeWebFighterAssetScope* h)
{ uint32_t n=0;if(h)for(unsigned i=0;i<6;++i)n+=h->fighters[i]!=NULL;return n; }
int melee_web_fighter_assets_end(MeleeWebFighterAssetScope* h,char* error,size_t size)
{
    if(!h)return 1;
    if(h!=current || melee_web_fighter_assets_live(h))return fail(error,size,"Fighter assets still have live source Fighters");
    if(gFtDataList[h->kind]!=h->data || CostumeListsForeachCharacter[h->kind].costume_list[h->costume].joint!=h->published_costume.joint)
        return fail(error,size,"Published fighter asset globals changed unexpectedly");
    gFtDataList[h->kind]=h->previous_data;CostumeListsForeachCharacter[h->kind].costume_list[h->costume]=h->previous_costume;
    current=NULL;free(h);if(error && size)error[0]=0;return 1;
}
void melee_web_fighter_assets_require_kind(uint32_t kind)
{
    if(!current || kind!=current->kind || gFtDataList[kind]!=current->data)
        fatal("No decoded assets published for requested fighter kind");
}
void melee_web_fighter_assets_require_costume(uint32_t kind,int costume)
{
    melee_web_fighter_assets_require_kind(kind);
    if(costume<0 || (uint32_t)costume!=current->costume)
        fatal("Requested costume is not hydrated in this asset scope");
}
void melee_web_fighter_assets_bind_created(Fighter* fp)
{
    char error[256];
    if(!fp)fatal("Missing source Fighter");
    melee_web_fighter_assets_require_costume(fp->kind,fp->x619_costume_id);
    if(fp->ft_data!=current->data)fatal("Constructor Fighter does not reference published ftData");
    for(unsigned i=0;i<6;++i)if(current->fighters[i]==fp)fatal("Source Fighter already has action storage");
    for(unsigned i=0;i<6;++i)if(!current->fighters[i]) {
        void* actions=NULL;void* blends=NULL;
        if(!current->bind(current->context,fp,&actions,&blends,error,sizeof(error)))fatal(error);
        if(!actions || !blends)fatal("Per-Fighter action tables were not retained");
        fp->x24=actions;fp->x28=blends;
        current->fighters[i]=fp;
        fp->x590=fp->x598=NULL;fp->x59C=fp->x5A0=NULL;fp->x5A4=fp->x5A8=NULL;fp->x58C=current->motion_count;
        return;
    }
    fatal("Source Fighter action binding capacity exceeded");
}
void melee_web_fighter_assets_unbind_destroying(Fighter* fp)
{
    if(!current)return;
    for(unsigned i=0;i<6;++i)if(current->fighters[i]==fp) {
        current->unbind(current->context,fp);fp->x24=NULL;fp->x28=NULL;current->fighters[i]=NULL;return;
    }
}
