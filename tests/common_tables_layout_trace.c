#include "common_tables.h"
#include <melee/ft/fighter.h>
#include <melee/sfx/crowdsfx.h>
#include <string.h>

/* Read through original C descriptors, independently of the bridge's owner
 * definition. A nonzero return identifies the root with an invalid graph. */
unsigned common_tables_check_native_layout(const MeleeWebCommonNative* owner,const MeleeWebCommonTables* values)
{
#define ROOT(i,T) ((const T*)melee_web_common_tables_root(owner,i))
    const unsigned char* throws=ROOT(1,unsigned char);
    float last_throw=0;
    if(!throws)return 1;
    memcpy(&last_throw,throws+25*12+8,sizeof(last_throw));
    if(last_throw!=values->item_throw[25].heavy_mul)return 1;
    const float (*swing)[5]=melee_web_common_tables_root(owner,2);
    if(!swing||swing[5][4]!=values->swing[5][4])return 2;
    const float* stale=ROOT(3,float);
    if(!stale||stale[8]!=values->stale[8])return 3;
    FighterPartsTable* const* parts=melee_web_common_tables_root(owner,4);
    if(!parts)return 4;
    struct Fighter_804D6540_t* const* alternates=melee_web_common_tables_root(owner,5);
    if(!alternates)return 5;
    for(unsigned kind=0;kind<MELEE_WEB_COMMON_FIGHTERS;++kind) {
        const MeleeWebCommonParts* p=&values->parts[kind];
        if(!parts[kind]||parts[kind]->parts_num!=p->part_count||
           memcmp(parts[kind]->part_to_joint,p->part_to_joint,MELEE_WEB_COMMON_PART_NAMES)||
           memcmp(parts[kind]->joint_to_part,p->joint_to_part,p->part_count))return 4;
        const MeleeWebCommonAlternates* a=&values->alternates[kind];
        if((alternates[kind]!=NULL)!=a->has_descriptor)return 5;
        if(!a->has_descriptor)continue;
        if(alternates[kind]->x4!=(int)a->count)return 5;
        for(unsigned i=0;i<a->count;++i) {
            const struct Fighter_804D6540_x0_t* e=&alternates[kind]->x0[i];
            const MeleeWebCommonAlternate* v=&a->entries[i];
            if(e->x0!=v->slot||e->x1!=v->parent||e->x2!=v->insertion||e->x3!=v->source_joint)return 5;
        }
    }
    Vec2* const* damage=melee_web_common_tables_root(owner,9);
    if(!damage)return 9;
    for(unsigned i=0;i<3;++i) {
        const MeleeWebCommonShake* shake=&values->damage_shake[i];
        if((uintptr_t)damage[i*2+1]!=shake->count||!damage[i*2])return 9;
        for(unsigned j=0;j<shake->count;++j)
            if(damage[i*2][j].x!=shake->samples[j].x||damage[i*2][j].y!=shake->samples[j].y)return 9;
    }
    for(unsigned root=10;root<=11;++root) {
        const struct Fighter_ShakeTable_t* s=ROOT(root,struct Fighter_ShakeTable_t);
        const MeleeWebCommonShake* v=root==10?&values->grab_shake:&values->smash_shake;
        if(!s||s->x4!=(int)v->count||!s->x0)return root;
        for(unsigned j=0;j<v->count;++j)if(s->x0[j].x!=v->samples[j].x||s->x0[j].y!=v->samples[j].y)return root;
    }
    const struct Fighter_804D6524_t* scale=ROOT(12,struct Fighter_804D6524_t);
    const struct Fighter_804D6520_t* bunny=ROOT(13,struct Fighter_804D6520_t);
    const struct Fighter_804D651C_t* metal=ROOT(14,struct Fighter_804D651C_t);
    const struct Fighter_804D6518_t* gravity=ROOT(15,struct Fighter_804D6518_t);
    if(!scale||scale->x98!=values->scale_modifiers[38])return 12;
    if(!bunny||bunny->x38!=values->bunny_modifiers[14])return 13;
    if(!metal||metal->x20!=values->metal_modifiers[8])return 14;
    if(!gravity||gravity->x4!=values->gravity_weight[1])return 15;
    const u8* first=ROOT(18,u8);const u8* second=ROOT(19,u8);
    if(!first||first[16]!=values->primary_colors[4].r||first[19]!=values->primary_colors[4].a)return 18;
    if(!second||second[16]!=values->secondary_colors[4].r||second[19]!=values->secondary_colors[4].a)return 19;
    const CrowdConfig* crowd=ROOT(21,CrowdConfig);
    if(!crowd)return 21;
#define CHECK_CROWD(offset,kind,member) if(crowd->member!=values->crowd.member)return 21;
    MELEE_WEB_CROWD_FIELDS(CHECK_CROWD)
#undef CHECK_CROWD
#undef ROOT
    return 0;
}
