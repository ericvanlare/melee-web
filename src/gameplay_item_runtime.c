#include "gameplay_item_runtime.h"
#include "gameplay_bootstrap.h"
#include "gameplay_article_data.h"
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
enum {
    MELEE_WEB_ITEM_COMMON_COUNT = It_Kind_Kuriboh,
    MELEE_WEB_ITEM_CHARACTER_COUNT = It_PKind_Start - It_Kind_Kuriboh,
    MELEE_WEB_ITEM_POKEMON_COUNT = It_Kind_Old_Kuri - It_PKind_Start,
};
_Static_assert(MELEE_WEB_ITEM_COMMON_COUNT == 43 &&
               MELEE_WEB_ITEM_CHARACTER_COUNT == 118 &&
               MELEE_WEB_ITEM_POKEMON_COUNT == 47,
               "Original itPublicData Article table extents");
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
static void item_require(const MeleeWebNativeDat* r,int condition,const char* message){
    if(!condition)r->reject(r->context,message);
}
static Article* item_decode_article(const MeleeWebNativeDat* r,uint32_t root,
                                    uint32_t* roots,Article** articles,
                                    uint32_t* count){
    for(uint32_t i=0;i<*count;i++)if(roots[i]==root)return articles[i];
    uint32_t unresolved=0;
    Article* article=melee_web_article_decode(r,root,&unresolved);
    item_require(r,article!=NULL,"ItCo Article root did not decode");
    item_require(r,*count<MELEE_WEB_ITEM_COMMON_COUNT+MELEE_WEB_ITEM_POKEMON_COUNT,
                 "ItCo Article decode table exceeds source extent");
    roots[*count]=root;articles[*count]=article;(*count)++;
    return article;
}
static Article** item_decode_article_table(const MeleeWebNativeDat* r,
                                           uint32_t table,uint32_t count,
                                           uint32_t* roots,Article** articles,
                                           uint32_t* decoded){
    /* The pointer reader checks that the target is inside the archive, but a
     * source table also has an authored extent.  Validate the whole table
     * before walking its entries so a relocation target cannot make a short
     * table appear valid merely because each individual word is readable. */
    r->region(r->context,table,(size_t)count*4);
    Article** output=r->allocate(r->context,count,sizeof(*output));
    for(uint32_t i=0;i<count;i++){
        uint32_t root=r->pointer(r->context,table+4*i,sizeof(Article));
        output[i]=root==UINT32_MAX?NULL:item_decode_article(r,root,roots,articles,decoded);
    }
    return output;
}
void* melee_web_item_public_data_decode(const MeleeWebNativeDat* r,uint32_t root,
                                        void* const* character_articles,
                                        uint32_t character_count){
    if(!r)return NULL;
    item_require(r,character_articles!=NULL &&
                    character_count==MELEE_WEB_ITEM_CHARACTER_COUNT,
                 "ItCo character Article owner has the wrong source extent");
    r->region(r->context,root,sizeof(it_804D6D20_t));
    uint32_t common_root=r->pointer(r->context,root,0x160);
    uint32_t common_table=r->pointer(r->context,root+4,MELEE_WEB_ITEM_COMMON_COUNT*4);
    uint32_t character_table=r->pointer(r->context,root+8,MELEE_WEB_ITEM_CHARACTER_COUNT*4);
    uint32_t pokemon_table=r->pointer(r->context,root+12,MELEE_WEB_ITEM_POKEMON_COUNT*4);
    uint32_t bounce_root=r->pointer(r->context,root+16,sizeof(it_804D6D40_t));
    uint32_t colors_root=r->pointer(r->context,root+20,8);
    item_require(r,common_root!=UINT32_MAX&&common_table!=UINT32_MAX&&
                    character_table!=UINT32_MAX&&pokemon_table!=UINT32_MAX&&
                    bounce_root!=UINT32_MAX&&colors_root!=UINT32_MAX,
                 "Incomplete original ItCo itPublicData root");
    item_require(r,common_table%4==0&&character_table%4==0&&pokemon_table%4==0,
                 "ItCo Article table is unaligned");
    Article* decoded_articles[MELEE_WEB_ITEM_COMMON_COUNT+MELEE_WEB_ITEM_POKEMON_COUNT];
    uint32_t decoded_roots[MELEE_WEB_ITEM_COMMON_COUNT+MELEE_WEB_ITEM_POKEMON_COUNT];
    uint32_t decoded_count=0;
    Article** common_articles=item_decode_article_table(r,common_table,
        MELEE_WEB_ITEM_COMMON_COUNT,decoded_roots,decoded_articles,&decoded_count);
    Article** pokemon_articles=item_decode_article_table(r,pokemon_table,
        MELEE_WEB_ITEM_POKEMON_COUNT,decoded_roots,decoded_articles,&decoded_count);
    for(uint32_t i=0;i<character_count;i++){
        uint32_t source=r->pointer(r->context,character_table+4*i,sizeof(Article));
        item_require(r,(source==UINT32_MAX)==(character_articles[i]==NULL),
                     "ItCo character Article owner differs from source table");
    }
    it_804D6D20_t* output=r->allocate(r->context,1,sizeof(*output));
    output->x0=melee_web_item_common_decode(r,common_root);
    output->x4=common_articles;
    output->x8=(Article**)character_articles;
    output->xC=pokemon_articles;
    output->x10=melee_web_item_bounce_decode(r,bounce_root);
    /* Color rows are decoded by DatColorAnimation. The source owner attaches
     * those checked native rows immediately before Item_80266FA8. */
    output->x14=NULL;
    return output;
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
    it_804D6D20_t* source_data;void* source_colors;
    it_804D6D20_t* saved_public;Article** saved_common_articles;
    Article** saved_character_articles;Article** saved_pokemon_articles;
    Fighter_804D653C_t* owned_colors;
    uint64_t generation;
};
static MeleeWebItemRuntime* active;
static int fail(char* e,size_t n,const char* m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
static MeleeWebItemRuntime* item_prepare(void* common,void* bounce,
                                          it_804D6D20_t* source,
                                          const MeleeWebColorRow* colors,
                                          size_t count,char* e,size_t n){
    if(active||!common||!bounce||!colors||!count||count>256||
       (source&&(!source->x4||!source->x8||!source->xC))||
       !melee_web_gameplay_stats().generation||((HSD_GObj**)HSD_GObj_Entities)[9]){
        fail(e,n,"Item startup requires checked data and an empty owned item link");return NULL;}
    MeleeWebItemRuntime* h=calloc(1,sizeof(*h));if(!h){fail(e,n,"Cannot own item runtime");return NULL;}
    h->owned_colors=calloc(count,sizeof(*h->owned_colors));if(!h->owned_colors){free(h);fail(e,n,"Cannot own item color rows");return NULL;}
    for(size_t i=0;i<count;i++){h->owned_colors[i].unk=colors[i].program;h->owned_colors[i].unk4=colors[i].priority;h->owned_colors[i].unk5=colors[i].layer;}
    h->common=it_804D6D28;h->bounce=it_804D6D40;h->colors=it_804D6D04;
    h->saved_public=it_804D6D20;h->saved_common_articles=it_804D6D24;
    h->saved_character_articles=it_804D6D38;h->saved_pokemon_articles=it_804D6D30;
    h->source_data=source;h->source_colors=source?source->x14:NULL;
    h->generation=melee_web_gameplay_stats().generation;
    #define SAVE(name) memcpy(&h->saved_##name,&name,sizeof(name));
    ITEM_GLOBALS(SAVE)
    #undef SAVE
    it_804D6D28=common;it_804D6D40=bounce;it_804D6D04=h->owned_colors;
    if(source){
        source->x0=common;source->x10=bounce;source->x14=h->owned_colors;
        it_804D6D20=source;it_804D6D24=source->x4;
        it_804D6D38=source->x8;it_804D6D30=source->xC;
    }
    active=h;if(e&&n)*e=0;return h;
}
MeleeWebItemRuntime* melee_web_item_runtime_prepare(void* common,void* bounce,const MeleeWebColorRow* colors,size_t count,char* e,size_t n){
    return item_prepare(common,bounce,NULL,colors,count,e,n);
}
MeleeWebItemRuntime* melee_web_item_runtime_prepare_source(void* source,const MeleeWebColorRow* colors,size_t count,char* e,size_t n){
    it_804D6D20_t* data=source;
    if(!data)return fail(e,n,"Item source root is missing"),NULL;
    return item_prepare(data->x0,data->x10,data,colors,count,e,n);
}
MeleeWebItemRuntime* melee_web_item_runtime_begin_source(void* source,const MeleeWebColorRow* colors,size_t count,char* e,size_t n){
    MeleeWebItemRuntime* h=melee_web_item_runtime_prepare_source(source,colors,count,e,n);
    if(h)Item_80266FCC();
    return h;
}
MeleeWebItemRuntime* melee_web_item_runtime_begin(void* common,void* bounce,const MeleeWebColorRow* colors,size_t count,char* e,size_t n){
    MeleeWebItemRuntime* h=melee_web_item_runtime_prepare(common,bounce,colors,count,e,n);
    if(h)Item_80266FCC();
    return h;
}
int melee_web_item_runtime_end(MeleeWebItemRuntime* h,char* e,size_t n){
    if(!h)return 1;
    if(h!=active||h->generation!=melee_web_gameplay_stats().generation||HSD_GObj_804D781C||HSD_GObj_804D7814)return fail(e,n,"Item teardown requires its idle owned source world");
    while(((HSD_GObj**)HSD_GObj_Entities)[9])Item_8026A8EC(((HSD_GObj**)HSD_GObj_Entities)[9]);
    if(h->source_data)h->source_data->x14=h->source_colors;
    it_804D6D20=h->saved_public;it_804D6D24=h->saved_common_articles;
    it_804D6D38=h->saved_character_articles;it_804D6D30=h->saved_pokemon_articles;
    it_804D6D28=h->common;it_804D6D40=h->bounce;it_804D6D04=h->colors;
#define RESTORE(name) memcpy(&name,&h->saved_##name,sizeof(name));
    ITEM_GLOBALS(RESTORE)
#undef RESTORE
    active=NULL;free(h->owned_colors);free(h);if(e&&n)*e=0;return 1;
}
