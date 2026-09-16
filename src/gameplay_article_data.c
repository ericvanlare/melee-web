#include "gameplay_article_data.h"
#include <melee/it/types.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WORD(o) r->word(r->context,(o))
#define PTR(o,n) r->pointer(r->context,(o),(n))
#define REGION(o,n) r->region(r->context,(o),(n))
#define NEW(t,n) ((t*)r->allocate(r->context,(n),sizeof(t)))
#define REQUIRE(c,m) do { if (!(c)) r->reject(r->context,m); } while(0)
#define ARTICLE_MAGIC UINT32_C(0x41525443)
typedef struct NativeArticle {
    Article article;
    uint32_t magic, unresolved;
    /* The source ItemStateArray is a variable-length descriptor tail in the
     * gameplay build. Keep the count beside the pointer so any source bridge
     * that indexes the tail can check its bound. */
    uint32_t state_count;
} NativeArticle;
_Static_assert(sizeof(Article)==24 && sizeof(ItemAttr)==0x84, "Article source ABI");
static float floating(const MeleeWebNativeDat* r,uint32_t at)
{ uint32_t w=WORD(at); float f; memcpy(&f,&w,4); REQUIRE(isfinite(f),"Article scalar is nonfinite"); return f; }
void* melee_web_article_decode(const MeleeWebNativeDat* r,uint32_t root,uint32_t* unresolved)
{
    if(!r || !unresolved) return NULL;
    REGION(root,24); NativeArticle* out=NEW(NativeArticle,1); out->magic=ARTICLE_MAGIC;
    for(unsigned i=0;i<6;++i) if(PTR(root+i*4,1)!=UINT32_MAX)out->unresolved|=1U<<i;
    uint32_t at=PTR(root,0x84);
    if(at==UINT32_MAX) {
        /* Some source roots (e.g. random Pokemon) fill attributes later.
         * Preserve their legal null at registration; creation stays blocked. */
        out->unresolved|=1U;
    } else {
    REGION(at,0x84); ItemAttr* attr=NEW(ItemAttr,1); out->article.x0_common_attr=attr;
    uint8_t b0=r->byte(r->context,at), b1=r->byte(r->context,at+1);
    attr->x0_is_heavy=b0>>7; attr->x0_78=(b0>>3)&15; attr->x0_hold_kind=b0&7;
    attr->x1_1=b1>>6; attr->x1_3=(b1>>5)&1; attr->x1_4=(b1>>4)&1;
    attr->x1_5=(b1>>3)&1; attr->x1_67_cam_kind=(b1>>1)&3; attr->x1_8=b1&1;
    attr->x3=r->byte(r->context,at+2);
    /* Remaining source fields are aligned 32-bit numeric scalars; no pointers
     * or bitfield overlays occur after the first word. */
    for(unsigned offset=4;offset<0x84;offset+=4) {
        uint32_t value=WORD(at+offset);
        if(offset<=0x60 && offset!=8) (void)floating(r,at+offset);
        memcpy((uint8_t*)attr+offset,&value,4);
    }
    out->unresolved&=~1U;
    }
    at=PTR(root+8,8);
    if(at!=UINT32_MAX) {
        REGION(at,8); int count=(int)WORD(at); REQUIRE(count>=0 && count<=64,"Article hurtbone count invalid");
        ItHurtBoneList* list=NEW(ItHurtBoneList,1); list->count=count;
        uint32_t p=PTR(at+4,count?count*32:1); REQUIRE(!count || p!=UINT32_MAX,"Article hurtbone rows missing");
        list->descs=NEW(ItHurtBoneDesc,count); if(count)REGION(p,count*32);
        for(int i=0;i<count;++i) {
            uint32_t q=p+i*32; ItHurtBoneDesc* h=&list->descs[i];
            h->bone_id=(int)WORD(q); REQUIRE(h->bone_id>=0 && h->bone_id<140,"Article hurtbone index invalid");
            h->a_offset=(Vec3){floating(r,q+4),floating(r,q+8),floating(r,q+12)};
            h->b_offset=(Vec3){floating(r,q+16),floating(r,q+20),floating(r,q+24)};h->scale=floating(r,q+28);
        }
        out->article.x8_hurtbones=list;
    }
    out->unresolved&=~(1U<<2);
    at=PTR(root+20,8);
    if(at!=UINT32_MAX) {
        REGION(at,8); int count=(int)WORD(at); REQUIRE(count>=0 && count<=100,"Article dynamics count invalid");
        if(!count) { out->article.x14_dynamics=NEW(ItemDynamics,1); out->unresolved&=~(1U<<5); }
    }
    /* Item creation unconditionally reaches modelDesc; a source null cannot
     * accidentally turn a registration-only root into a creation-ready one. */
    out->unresolved|=1U<<4;
    *unresolved=out->unresolved; return &out->article;
}
uint32_t melee_web_article_unresolved(const void* article)
{
    const NativeArticle* a=article;
    return a && a->magic==ARTICLE_MAGIC ? a->unresolved : UINT32_MAX;
}
void melee_web_article_require_ready(const void* article)
{
    uint32_t mask=melee_web_article_unresolved(article);
    if(mask) { fprintf(stderr,"Item creation requires hydrated Article graph (unresolved mask 0x%x)\n",mask); abort(); }
}

int melee_web_article_publish(const MeleeWebNativeDat* r,void* article,void* special,
    const MeleeWebItemStateDesc* states,uint32_t count,void* joint,uint32_t bones,int32_t attach,uint8_t flags,char* error,size_t size)
{
    NativeArticle* a=article;
    if(!r||!a||a->magic!=ARTICLE_MAGIC||(count&&!states)||(!count&&states)||count>64||!joint||
       bones>140||(a->unresolved&~((1U<<1)|(1U<<3)|(1U<<4)))){
        if(error&&size)snprintf(error,size,"Item graph publication requires checked registration root and complete fields");return 0;
    }
    /* ItemStateArray historically declared eight inline entries, but the
     * original Fox/Falco blaster roots carry a ninth, null descriptor. The
     * gameplay source patch makes this field a flexible tail; allocate the
     * exact number of source rows rather than indexing a fixed C array. */
    // An all--1 source ItemStateTable (Hookshot) has no animation table.
    struct ItemStateDesc* native_states=count?(struct ItemStateDesc*)r->allocate(
        r->context,count,sizeof(struct ItemStateDesc)):NULL;
    ItemModelDesc* model=NEW(ItemModelDesc,1);
    for(uint32_t i=0;i<count;i++){
        native_states[i]=(struct ItemStateDesc){states[i].animation,states[i].material,states[i].shape,states[i].commands};
    }
    model->x0_joint=joint;model->x4_bone_count=bones;model->x8_bone_attach_id=attach;model->xC_bit_field=flags;
    a->article.x4_specialAttributes=special;a->article.xC_itemStates=(ItemStateArray*)native_states;a->article.x10_modelDesc=model;
    a->state_count=count;
    a->unresolved=0;if(error&&size)*error=0;return 1;
}
