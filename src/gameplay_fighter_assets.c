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
    UnkCostumeStruct previous_costume[16],published_costume[16];
    unsigned char costume_owned[16];
    void* context;
    MeleeWebFighterAssetBind bind;
    MeleeWebFighterAssetUnbind unbind;
    Fighter* fighters[6];
};
/* Source keeps one ftData/costume namespace per FighterKind.  Keep the
 * publication owner keyed the same way so Mario and Falco can be resident in
 * one match while duplicate publication of one kind remains impossible. */
static MeleeWebFighterAssetScope* owners[FTKIND_NONE];
static int fail(char* error,size_t size,const char* text)
{ if(error && size)snprintf(error,size,"%s",text);return 0; }
static void fatal(const char* text)
{ fprintf(stderr,"Owned fighter assets: %s\n",text);abort(); }
MeleeWebFighterAssetScope* melee_web_fighter_assets_begin(uint32_t kind,uint32_t costume,
    void* data,void* joint,void* mat,uint32_t count,void* context,
    MeleeWebFighterAssetBind bind,MeleeWebFighterAssetUnbind unbind,char* error,size_t size)
{
    if(kind>=FTKIND_NONE || owners[kind] || !data || !joint || !mat || !context || !bind || !unbind ||
        costume>=16 || costume>=CostumeListsForeachCharacter[kind].numCostumes ||
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
    h->previous_data=gFtDataList[kind];h->previous_costume[costume]=CostumeListsForeachCharacter[kind].costume_list[costume];
    h->published_costume[costume]=(UnkCostumeStruct){joint,mat,0,0,0,NULL};h->costume_owned[costume]=1;
    gFtDataList[kind]=data;CostumeListsForeachCharacter[kind].costume_list[costume]=h->published_costume[costume];
    owners[kind]=h;if(error && size)error[0]=0;return h;
}
uint32_t melee_web_fighter_assets_live(const MeleeWebFighterAssetScope* h)
{ uint32_t n=0;if(h)for(unsigned i=0;i<6;++i)n+=h->fighters[i]!=NULL;return n; }
int melee_web_fighter_assets_add_costume(MeleeWebFighterAssetScope* h,uint32_t costume,
    void* joint,void* mat,char* error,size_t size)
{
    if(!h || h->kind>=FTKIND_NONE || owners[h->kind]!=h || melee_web_fighter_assets_live(h) || !joint || !mat ||
       costume>=16 || costume>=CostumeListsForeachCharacter[h->kind].numCostumes ||
       h->costume_owned[costume] || gFtDataList[h->kind]!=h->data)
        return fail(error,size,"Additional costume requires an idle owned fighter scope and distinct valid identity");
    h->previous_costume[costume]=CostumeListsForeachCharacter[h->kind].costume_list[costume];
    h->published_costume[costume]=(UnkCostumeStruct){joint,mat,0,0,0,NULL};
    CostumeListsForeachCharacter[h->kind].costume_list[costume]=h->published_costume[costume];
    h->costume_owned[costume]=1;if(error&&size)*error=0;return 1;
}
int melee_web_fighter_assets_end(MeleeWebFighterAssetScope* h,char* error,size_t size)
{
    if(!h)return 1;
    if(h->kind>=FTKIND_NONE || owners[h->kind]!=h || melee_web_fighter_assets_live(h))return fail(error,size,"Fighter assets still have live source Fighters");
    if(gFtDataList[h->kind]!=h->data)
        return fail(error,size,"Published fighter data changed unexpectedly");
    for(unsigned c=0;c<16;c++)if(h->costume_owned[c] &&
        (CostumeListsForeachCharacter[h->kind].costume_list[c].joint!=h->published_costume[c].joint ||
         CostumeListsForeachCharacter[h->kind].costume_list[c].x4!=h->published_costume[c].x4))
        return fail(error,size,"Published costume changed unexpectedly");
    gFtDataList[h->kind]=h->previous_data;
    for(unsigned c=0;c<16;c++)if(h->costume_owned[c])
        CostumeListsForeachCharacter[h->kind].costume_list[c]=h->previous_costume[c];
    owners[h->kind]=NULL;free(h);if(error && size)error[0]=0;return 1;
}
void melee_web_fighter_assets_require_kind(uint32_t kind)
{
    if(kind>=FTKIND_NONE || !owners[kind] || gFtDataList[kind]!=owners[kind]->data)
        fatal("No decoded assets published for requested fighter kind");
}
void melee_web_fighter_assets_require_costume(uint32_t kind,int costume)
{
    melee_web_fighter_assets_require_kind(kind);
    if(costume<0 || costume>=16 || !owners[kind]->costume_owned[costume])
        fatal("Requested costume is not hydrated in this asset scope");
}
void melee_web_fighter_assets_bind_created(Fighter* fp)
{
    char error[256];
    if(!fp)fatal("Missing source Fighter");
    melee_web_fighter_assets_require_costume(fp->kind,fp->x619_costume_id);
    MeleeWebFighterAssetScope* owner=owners[fp->kind];
    if(fp->ft_data!=owner->data)fatal("Constructor Fighter does not reference published ftData");
    for(unsigned i=0;i<6;++i)if(owner->fighters[i]==fp)fatal("Source Fighter already has action storage");
    for(unsigned i=0;i<6;++i)if(!owner->fighters[i]) {
        void* actions=NULL;void* blends=NULL;
        if(!owner->bind(owner->context,fp,&actions,&blends,error,sizeof(error)))fatal(error);
        if(!actions || !blends)fatal("Per-Fighter action tables were not retained");
        fp->x24=actions;fp->x28=blends;
        owner->fighters[i]=fp;
        fp->x590=fp->x598=NULL;fp->x59C=fp->x5A0=NULL;fp->x5A4=fp->x5A8=NULL;fp->x58C=owner->motion_count;
        /* The original character OnLoad callback publishes its Article
         * identities after it initializes dat_attrs.  The owned registry is
         * already live by then, so this bind boundary only installs the
         * per-Fighter action storage that ftData_80085B10 replaces. */
        return;
    }
    fatal("Source Fighter action binding capacity exceeded");
}
void melee_web_fighter_assets_unbind_destroying(Fighter* fp)
{
    if(!fp || fp->kind>=FTKIND_NONE || !owners[fp->kind])return;
    MeleeWebFighterAssetScope* owner=owners[fp->kind];
    for(unsigned i=0;i<6;++i)if(owner->fighters[i]==fp) {
        owner->unbind(owner->context,fp);fp->x24=NULL;fp->x28=NULL;owner->fighters[i]=NULL;return;
    }
}
