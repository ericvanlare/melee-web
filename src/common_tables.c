#include "common_tables.h"
#include <melee/ft/fighter.h>
#include <melee/ft/ftparts.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/sfx/crowdsfx.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(void*)==4 && sizeof(FighterPartsTable)==12,"Original common pointer ABI");
_Static_assert(FTKIND_MAX==MELEE_WEB_COMMON_FIGHTERS && MAX_FT_PARTS==MELEE_WEB_COMMON_MAX_PARTS,"Source fighter capacities");
_Static_assert(FtPart_TransN2+2==MELEE_WEB_COMMON_PART_NAMES,"Named enum plus explicit setup index0x35");
_Static_assert(ftCo_MS_HeavyThrowLw4-ftCo_MS_LightThrowF+1==26,"Original throw table range");
_Static_assert(sizeof(struct Fighter_804D6524_t)==39*4 && sizeof(struct Fighter_804D6520_t)==15*4 &&
               sizeof(struct Fighter_804D651C_t)==9*4 && sizeof(struct Fighter_804D6518_t)==2*4,"Original modifier layouts");
_Static_assert(sizeof(CrowdConfig)==sizeof(MeleeWebCommonCrowd),"Original crowd layout");
_Static_assert(sizeof(Vec2)==8 && offsetof(Vec2,y)==4,"Original shake sample layout");
#define CROWD_ASSERT(offset,kind,member) _Static_assert(offsetof(CrowdConfig,member)==offset && offsetof(MeleeWebCommonCrowd,member)==offset,"Original crowd field offset");
MELEE_WEB_CROWD_FIELDS(CROWD_ASSERT)
#undef CROWD_ASSERT

/* This type is local to original ftCo_ItemThrow.c, not its public header.
 * Keep its tag/member types compatible across C translation units. The global
 * is misleadingly declared int**, but the actual source consumes float rows. */
struct ftCo_ItemThrowAttrs { float velocity_mul,angle,x8; };
_Static_assert(sizeof(struct ftCo_ItemThrowAttrs)==12 &&
               offsetof(struct ftCo_ItemThrowAttrs,x8)==8,"Original item throw row");
struct MeleeWebCommonNative {
    MeleeWebCommonTables values;
    struct ftCo_ItemThrowAttrs throws[26];
    FighterPartsTable part_desc[MELEE_WEB_COMMON_PART_TABLES];
    FighterPartsTable* parts[MELEE_WEB_COMMON_PART_TABLES];
    struct Fighter_804D6540_t alternate_desc[MELEE_WEB_COMMON_FIGHTERS];
    struct Fighter_804D6540_t* alternates[MELEE_WEB_COMMON_FIGHTERS];
    struct Fighter_804D6540_x0_t entries[MELEE_WEB_COMMON_FIGHTERS][MELEE_WEB_COMMON_MAX_ALTERNATES];
    Vec2* damage_words[6];
    Vec2 damage_samples[3][MELEE_WEB_COMMON_MAX_SHAKE];
    Vec2 grab_samples[MELEE_WEB_COMMON_MAX_SHAKE],smash_samples[MELEE_WEB_COMMON_MAX_SHAKE];
    struct Fighter_ShakeTable_t grab,smash;
    struct Fighter_804D6524_t scale;
    struct Fighter_804D6520_t bunny;
    struct Fighter_804D651C_t metal;
    struct Fighter_804D6518_t gravity;
    CrowdConfig crowd;
    const void* roots[23];
};
static int fail(char* error,size_t size,const char* message)
{ if(error&&size)snprintf(error,size,"%s",message);return 0; }
static int success(char* error,size_t size)
{ if(error&&size)error[0]='\0';return 1; }
static int finite_values(const float* values,size_t count)
{ for(size_t i=0;i<count;++i)if(!isfinite(values[i]))return 0;return 1; }
static int shake_valid(const MeleeWebCommonShake* value)
{
    if(!value->count || value->count>MELEE_WEB_COMMON_MAX_SHAKE)return 0;
    for(uint32_t i=0;i<value->count;++i)
        if(!isfinite(value->samples[i].x)||!isfinite(value->samples[i].y))return 0;
    return 1;
}
static int parts_valid(const MeleeWebCommonParts* p)
{
    if(!p->part_count || p->part_count>MELEE_WEB_COMMON_MAX_PARTS)return 0;
    for(unsigned j=0;j<p->part_count;++j)
        if(p->joint_to_part[j]!=255&&p->joint_to_part[j]>=MELEE_WEB_COMMON_PART_NAMES)return 0;
    for(unsigned j=0;j<MELEE_WEB_COMMON_PART_NAMES;++j)
        if(p->part_to_joint[j]!=255&&p->part_to_joint[j]>=p->part_count)return 0;
    return 1;
}
static int validate(const MeleeWebCommonTables* t)
{
    if(!t || (t->ready_mask&~MELEE_WEB_COMMON_STATIC_ROOT_MASK))return 0;
#define READY(i) (t->ready_mask&(1U<<(i)))
    if(READY(1))for(unsigned i=0;i<26;++i)
        if(!isfinite(t->item_throw[i].velocity_mul)||!isfinite(t->item_throw[i].angle)||!isfinite(t->item_throw[i].heavy_mul))return 0;
    if(READY(2))for(unsigned i=0;i<6;++i)if(!finite_values(t->swing[i],5))return 0;
    if(READY(3)&&!finite_values(t->stale,9))return 0;
    if(READY(4)) {
        for(unsigned i=0;i<MELEE_WEB_COMMON_FIGHTERS;++i)
            if(!parts_valid(&t->parts[i]))return 0;
        if(!parts_valid(&t->none_parts))return 0;
    }
    if(READY(5)) {
        if(!READY(4))return 0;
        for(unsigned i=0;i<MELEE_WEB_COMMON_FIGHTERS;++i) {
            const MeleeWebCommonAlternates* a=&t->alternates[i];
            if(a->has_descriptor>1 || a->count>MELEE_WEB_COMMON_MAX_ALTERNATES || (!a->has_descriptor&&a->count))return 0;
            unsigned char seen[MELEE_WEB_COMMON_MAX_PARTS]={0};
            for(unsigned j=0;j<a->count;++j) {
                const MeleeWebCommonAlternate* e=&a->entries[j];
                const unsigned count=t->parts[i].part_count;
                if(e->slot>=count||e->parent>=count||e->insertion>3||(e->source_joint!=255&&e->source_joint>=count)||seen[e->slot])return 0;
                seen[e->slot]=1;
            }
        }
    }
    if(READY(9))for(unsigned i=0;i<3;++i)if(!shake_valid(&t->damage_shake[i]))return 0;
    if(READY(10)&&!shake_valid(&t->grab_shake))return 0;
    if(READY(11)&&!shake_valid(&t->smash_shake))return 0;
    if(READY(12)&&!finite_values(t->scale_modifiers,39))return 0;
    if(READY(13)&&!finite_values(t->bunny_modifiers,15))return 0;
    if(READY(14)&&!finite_values(t->metal_modifiers,9))return 0;
    if(READY(15)&&!finite_values(t->gravity_weight,2))return 0;
    if(READY(21)) {
#define CROWD_VALIDATE_F32(member) if(!isfinite(t->crowd.member))return 0;
#define CROWD_VALIDATE_I32(member)
#define CROWD_VALIDATE(offset,kind,member) CROWD_VALIDATE_##kind(member)
        MELEE_WEB_CROWD_FIELDS(CROWD_VALIDATE)
#undef CROWD_VALIDATE
#undef CROWD_VALIDATE_I32
#undef CROWD_VALIDATE_F32
    }
#undef READY
    return 1;
}
MeleeWebCommonNative* melee_web_common_tables_create(const MeleeWebCommonTables* tables,char* error,size_t size)
{
    if(!validate(tables)){fail(error,size,"Common native input has invalid readiness, counts or typed values");return NULL;}
    MeleeWebCommonNative* owner=calloc(1,sizeof(*owner));
    if(!owner){fail(error,size,"Unable to allocate native common table owner");return NULL;}
    owner->values=*tables;
    MeleeWebCommonTables* t=&owner->values;
    for(unsigned i=0;i<26;++i)owner->throws[i]=(struct ftCo_ItemThrowAttrs){
        t->item_throw[i].velocity_mul,t->item_throw[i].angle,t->item_throw[i].heavy_mul};
    owner->roots[1]=owner->throws;owner->roots[2]=t->swing;owner->roots[3]=t->stale;
    for(unsigned i=0;i<MELEE_WEB_COMMON_PART_TABLES;++i) {
        MeleeWebCommonParts* source=i<MELEE_WEB_COMMON_FIGHTERS?&t->parts[i]:&t->none_parts;
        owner->part_desc[i]=(FighterPartsTable){source->joint_to_part,source->part_to_joint,source->part_count};
        owner->parts[i]=&owner->part_desc[i];
        if(i==MELEE_WEB_COMMON_FIGHTERS)continue;
        if((t->ready_mask&(1U<<5))&&t->alternates[i].has_descriptor) {
            owner->alternate_desc[i].x0=owner->entries[i];owner->alternate_desc[i].x4=(int)t->alternates[i].count;
            owner->alternates[i]=&owner->alternate_desc[i];
            for(unsigned j=0;j<t->alternates[i].count;++j) {
                MeleeWebCommonAlternate* e=&t->alternates[i].entries[j];
                owner->entries[i][j]=(struct Fighter_804D6540_x0_t){e->slot,e->parent,e->insertion,e->source_joint};
            }
        }
    }
    owner->roots[4]=owner->parts;owner->roots[5]=owner->alternates;
    /* Original root9 is declared Vec2** but alternates a sample pointer and
     * integer count. Only this Wasm32 bridge materializes that source overlay;
     * serialized count words were never treated as relocation pointers. */
    for(unsigned i=0;i<3;++i) {
        if(t->ready_mask&(1U<<9))for(unsigned j=0;j<t->damage_shake[i].count;++j)
            owner->damage_samples[i][j]=(Vec2){t->damage_shake[i].samples[j].x,t->damage_shake[i].samples[j].y};
        owner->damage_words[i*2]=owner->damage_samples[i];
        owner->damage_words[i*2+1]=(Vec2*)(uintptr_t)t->damage_shake[i].count;
    }
    if(t->ready_mask&(1U<<10))for(unsigned j=0;j<t->grab_shake.count;++j)
        owner->grab_samples[j]=(Vec2){t->grab_shake.samples[j].x,t->grab_shake.samples[j].y};
    if(t->ready_mask&(1U<<11))for(unsigned j=0;j<t->smash_shake.count;++j)
        owner->smash_samples[j]=(Vec2){t->smash_shake.samples[j].x,t->smash_shake.samples[j].y};
    owner->grab=(struct Fighter_ShakeTable_t){owner->grab_samples,(int)t->grab_shake.count};
    owner->smash=(struct Fighter_ShakeTable_t){owner->smash_samples,(int)t->smash_shake.count};
    owner->roots[9]=owner->damage_words;owner->roots[10]=&owner->grab;owner->roots[11]=&owner->smash;
    memcpy(&owner->scale,t->scale_modifiers,sizeof(owner->scale));
    memcpy(&owner->bunny,t->bunny_modifiers,sizeof(owner->bunny));
    memcpy(&owner->metal,t->metal_modifiers,sizeof(owner->metal));
    memcpy(&owner->gravity,t->gravity_weight,sizeof(owner->gravity));
    owner->roots[12]=&owner->scale;owner->roots[13]=&owner->bunny;owner->roots[14]=&owner->metal;owner->roots[15]=&owner->gravity;
    owner->roots[18]=t->primary_colors;owner->roots[19]=t->secondary_colors;
#define CROWD_COPY(offset,kind,member) owner->crowd.member=t->crowd.member;
    MELEE_WEB_CROWD_FIELDS(CROWD_COPY)
#undef CROWD_COPY
    owner->roots[21]=&owner->crowd;
    success(error,size);return owner;
}
const void* melee_web_common_tables_root(const MeleeWebCommonNative* owner,uint32_t root)
{ return owner&&root<23&&(owner->values.ready_mask&(1U<<root))?owner->roots[root]:NULL; }
void melee_web_common_tables_destroy(MeleeWebCommonNative* owner){free(owner);}
int melee_web_common_parts_lookup(MeleeWebCommonNative* owner,uint32_t kind,uint32_t part,uint32_t* joint,char* error,size_t size)
{
    if(!melee_web_common_tables_root(owner,4)||!joint||kind>=MELEE_WEB_COMMON_FIGHTERS||part>=MELEE_WEB_COMMON_PART_NAMES)
        return fail(error,size,"Common part lookup requires a ready graph and bounded source indices");
    Fighter fp;fp.kind=(FighterKind)kind;
    FighterPartsTable** saved=ftPartsTable;ftPartsTable=owner->parts;
    *joint=(uint32_t)ftParts_GetBoneIndex(&fp,(Fighter_Part)part);
    ftPartsTable=saved;return success(error,size);
}
int melee_web_common_parts_remap(MeleeWebCommonNative* owner,uint32_t to,uint32_t from,uint32_t joint,uint32_t* mapped,char* error,size_t size)
{
    if(!melee_web_common_tables_root(owner,4)||!mapped||to>=MELEE_WEB_COMMON_FIGHTERS||from>=MELEE_WEB_COMMON_PART_TABLES)
        return fail(error,size,"Common part remap requires a ready graph and bounded source part tables");
    FighterPartsTable** saved=ftPartsTable;ftPartsTable=owner->parts;
    *mapped=(uint32_t)ftPartsRemap(to,from,joint);
    ftPartsTable=saved;return success(error,size);
}
