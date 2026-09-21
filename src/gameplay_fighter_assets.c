#include "gameplay_fighter_assets.h"
#include "gameplay_action_store.h"
#include "fighter_binding.h"
#include <melee/ft/ftdata.h>
#include <melee/ft/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct MeleeWebFighterAssetScope {
    uint32_t kind,costume,motion_count,demo_motion_count;
    unsigned demo_initialized;
    ftData* data;
    ftData* previous_data;
    void* previous_demo_data;
    UnkCostumeStruct previous_costume[16],published_costume[16];
    unsigned char costume_owned[16];
    void* context;
    MeleeWebFighterAssetBind bind;
    MeleeWebFighterAssetDemoBind demo_bind;
    MeleeWebFighterAssetUnbind unbind;
    Fighter* fighters[6];
    void* previous_purin_hat_cache[6];
};
/* Source keeps one ftData/costume namespace per FighterKind.  Keep the
 * publication owner keyed the same way so Mario and Falco can be resident in
 * one match while duplicate publication of one kind remains impossible. */
static MeleeWebFighterAssetScope* owners[FTKIND_NONE];
extern char* ftPr_Init_803D05B4[5];
extern void melee_web_purin_exchange_hat_cache(void**);
const char* melee_web_fighter_costume_part_symbol(uint32_t kind,uint32_t costume)
{
    return kind==FTKIND_PURIN && costume<5 ? ftPr_Init_803D05B4[costume] : NULL;
}
static int fail(char* error,size_t size,const char* text)
{ if(error && size)snprintf(error,size,"%s",text);return 0; }
static int fail_data_change(char* error,size_t size,uint32_t kind,const void* expected,const void* actual)
{
    if(error&&size)snprintf(error,size,"Published fighter data changed unexpectedly for kind %u (expected %p, actual %p)",
        kind,expected,actual);
    return 0;
}
static void fatal(const char* text)
{ fprintf(stderr,"Owned fighter assets: %s\n",text);abort(); }
MeleeWebFighterAssetScope* melee_web_fighter_assets_begin(uint32_t kind,uint32_t costume,
    void* data,void* joint,void* mat,void* archive,uint32_t count,void* context,
    MeleeWebFighterAssetBind bind,MeleeWebFighterAssetUnbind unbind,char* error,size_t size)
{
    if(kind>=FTKIND_NONE || owners[kind] || !data || !joint || !context || !bind || !unbind ||
        costume>=16 || costume>=CostumeListsForeachCharacter[kind].numCostumes ||
        melee_web_fighter_costume_material_required(kind,costume)!=(mat!=NULL) ||
        (melee_web_fighter_costume_part_symbol(kind,costume)!=NULL)!=(archive!=NULL) ||
        count!=(uint32_t)ftData_Table_Unk0[kind].count) {
        fail(error,size,"Fighter asset scope identity is invalid or another scope is active");return NULL;
    }
    ftData* native=data;
    if(!native->xC || !native->x10 || !native->x5C) {
        fail(error,size,"Fighter asset scope requires decoded action and metal descriptors");return NULL;
    }
    MeleeWebFighterAssetScope* h=calloc(1,sizeof(*h));
    if(!h){fail(error,size,"Fighter asset scope allocation failed");return NULL;}
    h->kind=kind;h->costume=costume;h->motion_count=count;h->demo_motion_count=0;
    h->data=data;h->context=context;h->bind=bind;h->unbind=unbind;
    h->previous_data=gFtDataList[kind];h->previous_costume[costume]=CostumeListsForeachCharacter[kind].costume_list[costume];
    h->published_costume[costume]=(UnkCostumeStruct){joint,mat,0,0,0,archive};h->costume_owned[costume]=1;
    gFtDataList[kind]=data;CostumeListsForeachCharacter[kind].costume_list[costume]=h->published_costume[costume];
    if(kind==FTKIND_PURIN)melee_web_purin_exchange_hat_cache(h->previous_purin_hat_cache);
    owners[kind]=h;if(error && size)error[0]=0;return h;
}
int melee_web_fighter_assets_set_demo(MeleeWebFighterAssetScope* h,uint32_t count,
    MeleeWebFighterAssetDemoBind bind,char* error,size_t size)
{
    if(!h || h->kind>=FTKIND_NONE || owners[h->kind]!=h || h->demo_bind ||
       melee_web_fighter_assets_live(h) || !count || count>32 ||
       count!=(uint32_t)ftData_UnkIntPairs[h->kind].count || !bind)
        return fail(error,size,"Demo fighter asset table identity is invalid or already installed");
    /* ftDemo_SetArchiveData still uses this source cache to publish the
     * GmRstM* table. The web demo binder owns the decoded rows itself and
     * bypasses ftData_80085B98, whose original side effect clears this cache;
     * clear it here so a later Results scene loads its own archive. */
    h->previous_demo_data=ftData_UnkIntPairs[h->kind].data;
    ftData_UnkIntPairs[h->kind].data=NULL;
    h->demo_motion_count=count;h->demo_bind=bind;if(error&&size)*error=0;return 1;
}
uint32_t melee_web_fighter_assets_live(const MeleeWebFighterAssetScope* h)
{ uint32_t n=0;if(h)for(unsigned i=0;i<6;++i)n+=h->fighters[i]!=NULL;return n; }
int melee_web_fighter_assets_add_costume(MeleeWebFighterAssetScope* h,uint32_t costume,
    void* joint,void* mat,void* archive,char* error,size_t size)
{
    if(!h || h->kind>=FTKIND_NONE || owners[h->kind]!=h || melee_web_fighter_assets_live(h) || !joint ||
       costume>=16 || costume>=CostumeListsForeachCharacter[h->kind].numCostumes ||
       melee_web_fighter_costume_material_required(h->kind,costume)!=(mat!=NULL) ||
       (melee_web_fighter_costume_part_symbol(h->kind,costume)!=NULL)!=(archive!=NULL) ||
       h->costume_owned[costume] || gFtDataList[h->kind]!=h->data)
        return fail(error,size,"Additional costume requires an idle owned fighter scope and distinct valid identity");
    h->previous_costume[costume]=CostumeListsForeachCharacter[h->kind].costume_list[costume];
    h->published_costume[costume]=(UnkCostumeStruct){joint,mat,0,0,0,archive};
    CostumeListsForeachCharacter[h->kind].costume_list[costume]=h->published_costume[costume];
    h->costume_owned[costume]=1;if(error&&size)*error=0;return 1;
}
int melee_web_fighter_assets_end(MeleeWebFighterAssetScope* h,char* error,size_t size)
{
    if(!h)return 1;
    if(h->kind>=FTKIND_NONE || owners[h->kind]!=h || melee_web_fighter_assets_live(h))return fail(error,size,"Fighter assets still have live source Fighters");
    if(gFtDataList[h->kind]!=h->data)
        return fail_data_change(error,size,h->kind,h->data,gFtDataList[h->kind]);
    for(unsigned c=0;c<16;c++)if(h->costume_owned[c] &&
        (CostumeListsForeachCharacter[h->kind].costume_list[c].joint!=h->published_costume[c].joint ||
         CostumeListsForeachCharacter[h->kind].costume_list[c].x4!=h->published_costume[c].x4 ||
         CostumeListsForeachCharacter[h->kind].costume_list[c].x14_archive!=h->published_costume[c].x14_archive))
        return fail(error,size,"Published costume changed unexpectedly");
    if(h->kind==FTKIND_PURIN)melee_web_purin_exchange_hat_cache(h->previous_purin_hat_cache);
    gFtDataList[h->kind]=h->previous_data;
    for(unsigned c=0;c<16;c++)if(h->costume_owned[c])
        CostumeListsForeachCharacter[h->kind].costume_list[c]=h->previous_costume[c];
    if(h->demo_bind)ftData_UnkIntPairs[h->kind].data=h->previous_demo_data;
    owners[h->kind]=NULL;free(h);if(error && size)error[0]=0;return 1;
}
void melee_web_fighter_assets_require_kind(uint32_t kind)
{
    if(kind>=FTKIND_NONE || !owners[kind] || gFtDataList[kind]!=owners[kind]->data)
        fatal("No decoded assets published for requested fighter kind");
}
void melee_web_fighter_assets_demo_init_begin(void)
{
    char error[256];
    if(!melee_web_fighter_assets_check_owned("Before source demo initialization",error,sizeof(error)))fatal(error);
    for(unsigned kind=0;kind<FTKIND_NONE;++kind){
        MeleeWebFighterAssetScope* h=owners[kind];
        if(h&&(!h->demo_bind||h->demo_initialized||melee_web_fighter_assets_live(h)))
            fatal("Demo initialization requires fresh prepared demo asset scopes");
    }
}
void melee_web_fighter_assets_demo_init_end(void)
{
    /* Fighter_800679B0 performs the original allocator/material setup and
     * clears its file caches. The already decoded immutable graphs become
     * available only after that reset, before the first demo constructor. */
    for(unsigned kind=0;kind<FTKIND_NONE;++kind){
        MeleeWebFighterAssetScope* h=owners[kind];
        if(!h)continue;
        if(!h->demo_bind||h->demo_initialized||melee_web_fighter_assets_live(h)||
           gFtDataList[kind]||ftData_UnkIntPairs[kind].data)
            fatal("Original demo initialization did not clear its fighter caches");
        for(unsigned c=0;c<16;++c)if(h->costume_owned[c]){
            UnkCostumeStruct* costume=&CostumeListsForeachCharacter[kind].costume_list[c];
            if(costume->joint||costume->pad_x8||costume->x4!=h->published_costume[c].x4)
                fatal("Original demo initialization did not clear its costume cache");
        }
    }
    for(unsigned kind=0;kind<FTKIND_NONE;++kind){
        MeleeWebFighterAssetScope* h=owners[kind];
        if(!h)continue;
        gFtDataList[kind]=h->data;
        for(unsigned c=0;c<16;++c)if(h->costume_owned[c])
            CostumeListsForeachCharacter[kind].costume_list[c]=h->published_costume[c];
        h->demo_initialized=1;
    }
}
void melee_web_fighter_assets_require_costume(uint32_t kind,int costume)
{
    melee_web_fighter_assets_require_kind(kind);
    if(costume<0 || costume>=16 || !owners[kind]->costume_owned[costume])
        fatal("Requested costume is not hydrated in this asset scope");
}
int melee_web_fighter_assets_check_owned(const char* phase,char* error,size_t size)
{
    for(unsigned kind=0;kind<FTKIND_NONE;++kind) {
        MeleeWebFighterAssetScope* h=owners[kind];
        if(!h)continue;
        if(gFtDataList[kind]!=h->data) {
            if(error&&size)snprintf(error,size,"%s: kind %u ftData owner changed (expected %p, actual %p)",
                phase?phase:"fighter-assets",kind,(void*)h->data,(void*)gFtDataList[kind]);
            return 0;
        }
        for(unsigned c=0;c<16;++c)if(h->costume_owned[c] &&
            (CostumeListsForeachCharacter[kind].costume_list[c].joint!=h->published_costume[c].joint ||
             CostumeListsForeachCharacter[kind].costume_list[c].x4!=h->published_costume[c].x4 ||
             CostumeListsForeachCharacter[kind].costume_list[c].x14_archive!=h->published_costume[c].x14_archive)) {
            if(error&&size)snprintf(error,size,"%s: kind %u costume %u owner changed",
                phase?phase:"fighter-assets",kind,c);
            return 0;
        }
    }
    if(error&&size)error[0]=0;
    return 1;
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
void melee_web_fighter_assets_bind_demo_created(Fighter* fp,int first_motion,int last_motion)
{
    char error[256];
    if(!fp)fatal("Missing source demo Fighter");
    melee_web_fighter_assets_require_costume(fp->kind,fp->x619_costume_id);
    MeleeWebFighterAssetScope* owner=owners[fp->kind];
    if(!owner->demo_motion_count || !owner->demo_bind)fatal("No authored demo action table is installed for this fighter");
    if(first_motion<0 || last_motion<first_motion || (uint32_t)last_motion>=owner->demo_motion_count)
        fatal("Source demo action range is outside its authored table");
    if(fp->ft_data!=owner->data)fatal("Demo Fighter does not reference published ftData");
    for(unsigned i=0;i<6;++i)if(owner->fighters[i]==fp)fatal("Source demo Fighter already has action storage");
    for(unsigned i=0;i<6;++i)if(!owner->fighters[i]) {
        void* actions=NULL;void* blends=NULL;
        if(!owner->demo_bind(owner->context,fp,&actions,&blends,error,sizeof(error)))fatal(error);
        if(!actions || !blends)fatal("Per-Fighter demo action tables were not retained");
        owner->fighters[i]=fp;
        fp->x24=actions;fp->x28=blends;fp->x58C=owner->demo_motion_count;
        fp->x590=fp->x598=NULL;fp->x59C=fp->x5A0=NULL;fp->x5A4=fp->x5A8=NULL;
        /* Match ftData_80085B98's post-relocation transition. The next
         * Player_80036E20 call must publish its own GmRstM* table, and no
         * source object may retain the archive pointer after this bind. */
        ftData_UnkIntPairs[fp->kind].data=NULL;
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
