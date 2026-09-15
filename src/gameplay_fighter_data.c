#include "gameplay_fighter_data.h"
#include "fighter_attributes.h"
#include "gameplay_article_data.h"
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftMario/types.h>
#include <melee/ft/kinds/ftFox/types.h>
#include <melee/ft/kinds/ftMars/types.h>
#include <sysdolphin/baselib/jobj.h>
#include <stddef.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(ftData) == 0x60 && sizeof(void*) == 4, "Native fighter ABI");
typedef struct Counted { uint32_t count; void* data; } Counted;
_Static_assert(sizeof(Counted) == 8, "Visibility descriptor ABI");
#define WORD(o) r->word(r->context, (o))
#define BYTE(o) r->byte(r->context, (o))
#define PTR(o,n) r->pointer(r->context, (o),(n))
#define REGION(o,n) r->region(r->context,(o),(n))
#define NEW(t,n) ((t*) r->allocate(r->context,(n),sizeof(t)))
#define REQUIRE(c,m) do { if (!(c)) r->reject(r->context,(m)); } while (0)
static uint32_t required(const MeleeWebNativeDat* r,uint32_t slot,size_t size)
{
    uint32_t at=PTR(slot,size); REQUIRE(at != UINT32_MAX,"Required fighter data pointer is null");
    REGION(at,size); return at;
}
static float floating(const MeleeWebNativeDat* r,uint32_t at)
{
    uint32_t bits=WORD(at); float value; memcpy(&value,&bits,4);
    REQUIRE(isfinite(value),"Native fighter scalar is nonfinite"); return value;
}
static Counted* visibility(const MeleeWebNativeDat* r,uint32_t at,uint32_t count,unsigned category)
{
    REGION(at,count*8); Counted* groups=NEW(Counted,count);
    for (uint32_t i=0;i<count;++i) {
        uint32_t n=WORD(at+i*8); REQUIRE(n<=128,"Visibility variants exceed selector capacity");
        groups[i].count=n; groups[i].data=NEW(Counted,n);
        uint32_t variants=PTR(at+i*8+4,n?n*8:1);
        REQUIRE(!n || variants!=UINT32_MAX,"Visibility variants missing");
        if(n) REGION(variants,n*8);
        for(uint32_t j=0;j<n;++j) {
            Counted* v=&((Counted*)groups[i].data)[j];
            v->count=WORD(variants+j*8);
            uint32_t limit=category==2?32:124;
            REQUIRE(v->count<=limit,"Visibility list exceeds source capacity");
            uint32_t indices=PTR(variants+j*8+4,v->count?v->count:1);
            REQUIRE(!v->count || indices!=UINT32_MAX,"Visibility indices missing");
            v->data=NEW(uint8_t,v->count);
            if(v->count) REGION(indices,v->count);
            for(uint32_t k=0;k<v->count;++k) {
                uint8_t index=BYTE(indices+k); REQUIRE(index<limit,"Visibility index exceeds source capacity");
                ((uint8_t*)v->data)[k]=index;
            }
        }
    }
    return groups;
}
static FtSFXArr* sound_array(const MeleeWebNativeDat* r,uint32_t slot)
{
    uint32_t at=PTR(slot,8); if(at==UINT32_MAX) return NULL;
    REGION(at,8); FtSFXArr* out=NEW(FtSFXArr,1); out->num=(int)WORD(at);
    REQUIRE(out->num>=0 && out->num<=1024,"Fighter sound list count invalid");
    uint32_t data=PTR(at+4,out->num?out->num*4:1);
    REQUIRE(!out->num || data!=UINT32_MAX,"Fighter sound IDs missing");
    out->sfx_ids=NEW(s32,out->num); if(out->num) REGION(data,out->num*4);
    for(int i=0;i<out->num;++i) out->sfx_ids[i]=(s32)WORD(data+i*4);
    return out;
}
void* melee_web_fighter_data_decode(const MeleeWebNativeDat* r,uint32_t root,
    uint32_t kind,uint32_t costumes,uint32_t motion_count,void* actions,void* blends,
    void* choices,uint32_t* unresolved)
{
    if (!r || !unresolved) return NULL;
    REQUIRE(kind==FTKIND_MARIO || kind==FTKIND_DRMARIO || kind==FTKIND_FOX ||
        kind==FTKIND_FALCO || kind==FTKIND_MARS || kind==FTKIND_EMBLEM,
        "Native fighter extension schema unavailable");
    REQUIRE(costumes>0 && costumes<=16,"Native costume count exceeds checked bound");
    REQUIRE(motion_count>0 && motion_count<=1024,"Native motion count exceeds checked bound");
    REGION(root,0x60); ftData* d=NEW(ftData,1); *unresolved=0;
    /* Every source pointer starts explicitly unresolved until decoded below. */
    for(unsigned field=0;field<24;++field)
        if(PTR(root+field*4,1)!=UINT32_MAX) *unresolved |= 1U<<field;
    uint32_t at=required(r,root,0x184); d->x0=NEW(ftCo_DatAttrs,1);
#define READ_F32(o) floating(r,o)
#define READ_I32(o) ((int32_t)WORD(o))
#define READ_U32(o) WORD(o)
#define READ_U8(o) BYTE(o)
#define CO(o,t,n,orig) d->x0->orig=READ_##t(at+o);
    MELEE_WEB_CO_ATTRIBUTE_FIELDS(CO)
#undef CO
    if(kind==FTKIND_MARIO || kind==FTKIND_DRMARIO) {
        /* ftMr_Init_OnLoadForDrMario copies the full Mario ABI from Dr.
         * Mario's own DAT, including his distinct cape Article kind. */
        at=required(r,root+4,0x84); ftMario_DatAttrs* mario=NEW(ftMario_DatAttrs,1); d->ext_attr=mario;
#define MARIO(o,t,n,orig) mario->orig=READ_##t(at+o);
        MELEE_WEB_MARIO_ATTRIBUTE_FIELDS(MARIO)
#undef MARIO
    } else if(kind==FTKIND_FOX || kind==FTKIND_FALCO) {
        /* Fox and Falco use one original extension ABI.  The source keeps
         * separate DAT values and Article identities, so decode the complete
         * ftFox_DatAttrs record for either kind rather than borrowing Fox's
         * values or reducing the extension to a common subset. */
        at=required(r,root+4,0xD4); ftFox_DatAttrs* fox=NEW(ftFox_DatAttrs,1); d->ext_attr=fox;
#define FOX(o,t,n,orig) fox->orig=READ_##t(at+o);
        MELEE_WEB_FOX_ATTRIBUTE_FIELDS(FOX)
#undef FOX
        REQUIRE(fox->xB0_FOX_REFLECTOR_REFLECTION.x0_bone_id<140,"Native reflector bone index invalid");
    } else if(kind==FTKIND_MARS || kind==FTKIND_EMBLEM) {
        at=required(r,root+4,0x98); MarsAttributes* mars=NEW(MarsAttributes,1); d->ext_attr=mars;
#define MARS(o,t,n,orig) mars->orig=READ_##t(at+o);
        MELEE_WEB_MARS_ATTRIBUTE_FIELDS(MARS)
#undef MARS
        REQUIRE(mars->x64.x0_bone_id>=0 && mars->x64.x0_bone_id<140 && mars->x64.x10_size>0,
                "Native Marth/Roy counter descriptor invalid");
    } else {
        REQUIRE(0,"Native fighter extension schema unavailable");
    }
    at=required(r,root+8,0x18); d->x8=NEW(struct ftData_x8,1);
    d->x8->x0.model_num=WORD(at); REQUIRE(d->x8->x0.model_num<=11,"Fighter model count exceeds source capacity");
    uint32_t table=required(r,at+4,costumes*16);
    d->x8->x0.vis_table=r->allocate(r->context,costumes,16);
    for(uint32_t c=0;c<costumes;++c) for(unsigned category=0;category<4;++category) {
        uint32_t p=PTR(table+c*16+category*4,d->x8->x0.model_num*8);
        if(p!=UINT32_MAX) d->x8->x0.vis_table[c][category]=visibility(r,p,d->x8->x0.model_num,category);
    }
    d->x8->x8.x8=WORD(at+8); REQUIRE(d->x8->x8.x8<=8,"Costume texture map exceeds source capacity");
    table=required(r,at+12,costumes*4); d->x8->x8.xC=NEW(u16*,costumes);
    for(uint32_t c=0;c<costumes;++c) {
        uint32_t p=PTR(table+c*4,d->x8->x8.x8*2);
        if(p==UINT32_MAX) continue;
        REGION(p,d->x8->x8.x8*2); d->x8->x8.xC[c]=NEW(u16,d->x8->x8.x8);
        for(uint32_t j=0;j<d->x8->x8.x8;++j) d->x8->x8.xC[c][j]=r->half(r->context,p+j*2);
    }
    d->x8->x10=BYTE(at+16); d->x8->x11=BYTE(at+17); d->x8->x12=BYTE(at+18);
    d->x8->x13=BYTE(at+19); d->x8->x14=BYTE(at+20);
    d->xC=actions; d->x10=blends; d->x24=choices;
    at=required(r,root+0x2c,20); d->x2C=NEW(ftDynamics,1);
    d->x2C->dynamicsNum=READ_I32(at);
    const unsigned dynamics_capacity=sizeof(((struct ArticleDynamicBones*)0)->array)/
                                     sizeof(((struct ArticleDynamicBones*)0)->array[0]);
    REQUIRE(d->x2C->dynamicsNum>=0 && (unsigned)d->x2C->dynamicsNum<dynamics_capacity,
            "Native fighter dynamics count exceeds source Fighter storage");
    if(d->x2C->dynamicsNum) {
        const uint32_t bones=required(r,at+4,(size_t)d->x2C->dynamicsNum*sizeof(BoneDynamicsDesc));
        d->x2C->ftDynamicBones=NEW(struct ArticleDynamicBones,1);
        for(int i=0;i<d->x2C->dynamicsNum;++i) {
            const uint32_t row=bones+(uint32_t)i*sizeof(BoneDynamicsDesc);
            BoneDynamicsDesc* out=&d->x2C->ftDynamicBones->array[i];
            out->bone_id=(enum_t)READ_I32(row);
            REQUIRE(out->bone_id>=0 && out->bone_id<140,
                    "Native fighter dynamics bone exceeds source part storage");
            out->dyn_desc.count=READ_U32(row+8);
            REQUIRE(out->dyn_desc.count>0 && out->dyn_desc.count<=140,
                    "Native fighter dynamics chain exceeds source part storage");
            const uint32_t values=required(r,row+4,
                (size_t)out->dyn_desc.count*sizeof(struct lb_00F9_UnkDesc1Inner));
            struct lb_00F9_UnkDesc1Inner* parameters=
                NEW(struct lb_00F9_UnkDesc1Inner,out->dyn_desc.count);
            for(unsigned j=0;j<out->dyn_desc.count;++j)for(unsigned k=0;k<15;++k) {
                const uint32_t field=values+j*sizeof(*parameters)+k*4;
                const float checked=floating(r,field);
                memcpy((uint8_t*)&parameters[j]+k*4,&checked,4);
            }
            /* lb_80011710 treats the serialized parameter array as the
             * lb_unk1 view of DynamicsData. Runtime linked DynamicsData is
             * allocated separately by lb_8000FD48 for each Fighter. */
            out->dyn_desc.data=(struct DynamicsData*)parameters;
            out->dyn_desc.pos=(Vec3){floating(r,row+12),floating(r,row+16),floating(r,row+20)};
        }
    } else REQUIRE(PTR(at+4,1)==UINT32_MAX,
                   "Empty native fighter dynamics has a nonnull bone table");
    d->x2C->x4=READ_I32(at+8);
    REQUIRE(d->x2C->x4>=0 && d->x2C->x4<=11,
            "Native fighter dynamics auxiliary count exceeds source capacity");
    if(d->x2C->x4) {
        const uint32_t rows=required(r,at+12,(size_t)d->x2C->x4*sizeof(struct ftData_x38));
        d->x2C->x8=NEW(struct ftData_x38,d->x2C->x4);
        for(int i=0;i<d->x2C->x4;++i) {
            const uint32_t row=rows+(uint32_t)i*sizeof(struct ftData_x38);
            d->x2C->x8[i].x0=READ_I32(row);
            REQUIRE(d->x2C->x8[i].x0>=0 && d->x2C->x8[i].x0<140,
                    "Native fighter dynamics auxiliary bone exceeds source part storage");
            d->x2C->x8[i].x4=(Vec3){floating(r,row+4),floating(r,row+8),floating(r,row+12)};
            d->x2C->x8[i].x10=floating(r,row+16);
        }
    } else REQUIRE(PTR(at+12,1)==UINT32_MAX,
                   "Empty native fighter dynamics has a nonnull auxiliary table");
    uint32_t dynamics_table=PTR(at+16,1);
    if(dynamics_table!=UINT32_MAX) {
        /* Marth/Roy's authored dynamics modes each carry one integer chain
         * cutoff per sword/cape bone. The mode selector is the second byte of
         * every source blend row, and the original field is typed FigaTree***
         * even though ftdynamics.c compares these pointer-width values as
         * small integers. Derive the table extent from all authored selectors:
         * Roy has a sixth row for selector 5, while Marth's table ends at 4.
         * The referenced-region check below keeps an adjacent descriptor from
         * being consumed as a fabricated mode row.
         */
        REQUIRE((kind==FTKIND_MARS||kind==FTKIND_EMBLEM)&&d->x2C->dynamicsNum==3,
                "Native fighter dynamics mode schema unavailable");
        REQUIRE(blends,"Native fighter dynamics selectors are missing");
        unsigned mode_count=0;
        const uint8_t* blend_bytes=(const uint8_t*)blends;
        for(uint32_t motion=0;motion<motion_count;++motion) {
            const unsigned mode=blend_bytes[motion*2+1];
            if(mode+1>mode_count)mode_count=mode+1;
        }
        REQUIRE(mode_count>0,"Native fighter dynamics mode table is empty");
        REGION(dynamics_table,(size_t)mode_count*4);
        d->x2C->x10=NEW(FigaTree**,mode_count);
        for(unsigned mode=0;mode<mode_count;++mode) {
            uint32_t row=required(r,dynamics_table+mode*4,
                                  d->x2C->dynamicsNum*4);
            d->x2C->x10[mode]=NEW(FigaTree*,d->x2C->dynamicsNum);
            for(int bone=0;bone<d->x2C->dynamicsNum;++bone) {
                uint32_t cutoff=WORD(row+bone*4);
                REQUIRE(cutoff<=d->x2C->ftDynamicBones->array[bone].dyn_desc.count,
                        "Marth/Roy dynamics cutoff exceeds its source chain");
                d->x2C->x10[mode][bone]=(FigaTree*)(uintptr_t)cutoff;
            }
        }
    } else d->x2C->x10=NULL;
    at=required(r,root+0x30,8); d->x30=NEW(struct ftData_x30,1); d->x30->count=(int)WORD(at);
    REQUIRE(d->x30->count>=0 && d->x30->count<=15,"Native hurtbox count exceeds source capacity");
    uint32_t rows=PTR(at+4,d->x30->count?d->x30->count*40:1);
    REQUIRE(!d->x30->count || rows!=UINT32_MAX,"Native hurtbox rows missing");
    if(d->x30->count) REGION(rows,d->x30->count*40);
    d->x30->inits=NEW(ftHurtboxInit,d->x30->count);
    for(int i=0;i<d->x30->count;++i) {
        uint32_t p=rows+i*40; ftHurtboxInit* h=&d->x30->inits[i];
        /* Named layout is checked by the existing source attribute bridge. */
        uint32_t words[10]; for(unsigned j=0;j<10;++j) words[j]=WORD(p+j*4);
        memcpy(h,words,40);
        REQUIRE(words[0]<140 && words[1]<=2 && words[2]<=1,"Native hurtbox bone/height/grabbable invalid");
        for(unsigned j=3;j<10;++j) (void)floating(r,p+j*4);
        REQUIRE(isfinite(h->scale),"Native hurtbox scale nonfinite");
    }
    at=required(r,root+0x34,8); d->x34=NEW(struct ftData_x34,1);
    d->x34->x0=WORD(at); d->x34->scale=floating(r,at+4);
    at=required(r,root+0x38,sizeof(((Fighter*)0)->x1614)/sizeof(((Fighter*)0)->x1614[0])*20);
    const unsigned reflected=sizeof(((Fighter*)0)->x1614)/sizeof(((Fighter*)0)->x1614[0]);
    d->x38=NEW(struct ftData_x38,reflected);
    for(unsigned i=0;i<reflected;++i) {
        d->x38[i].x0=WORD(at+i*20); d->x38[i].x4=(Vec3){floating(r,at+i*20+4),floating(r,at+i*20+8),floating(r,at+i*20+12)};
        d->x38[i].x10=floating(r,at+i*20+16);
    }
    at=required(r,root+0x3c,24); d->x3C=NEW(UnkFloat6_Camera,1);
    d->x3C->x0=(Vec3){floating(r,at),floating(r,at+4),floating(r,at+8)};
    d->x3C->xC=(Vec3){floating(r,at+12),floating(r,at+16),floating(r,at+20)};
    at=required(r,root+0x40,0x30); d->x40=NEW(itPickup,1);
#define PICKUP(o,t,n,orig) d->x40->orig=READ_##t(at+o);
    MELEE_WEB_PICKUP_ATTRIBUTE_FIELDS(PICKUP)
#undef PICKUP
    at=required(r,root+0x44,28); d->x44=NEW(ftData_x44_t,1);
    d->x44->unk0=r->half(r->context,at); d->x44->unk2=r->half(r->context,at+2);
    d->x44->unk4=r->half(r->context,at+4); d->x44->unk6=r->half(r->context,at+6);
    d->x44->unk8=r->half(r->context,at+8); d->x44->unkA=r->half(r->context,at+10);
    d->x44->unkC=floating(r,at+12); d->x44->ledge_snap_x=floating(r,at+16);
    d->x44->ledge_snap_y=floating(r,at+20); d->x44->ledge_snap_height=floating(r,at+24);
    at=required(r,root+0x4c,56); d->x4C_sfx=NEW(FtSFX,1);
    d->x4C_sfx->smash=sound_array(r,at); d->x4C_sfx->x20=sound_array(r,at+32);
    d->x4C_sfx->x1C=(int)(uintptr_t)sound_array(r,at+28);
    for(unsigned i=1;i<14;++i) if(i!=7 && i!=8) ((int*)d->x4C_sfx)[i]=(int)WORD(at+i*4);
    at=required(r,root+0x50,8); d->x50=NEW(Vec2,1); d->x50->x=floating(r,at); d->x50->y=floating(r,at+4);
    /* ftCo_8009F834 rotates through five Fighter_Part entries for effect 0x8D. */
    at=required(r,root+0x54,5*sizeof(int)); int* effect_parts=NEW(int,5);
    for(unsigned i=0;i<5;++i) effect_parts[i]=(int)WORD(at+i*4);
    d->x54=(int)(uintptr_t)effect_parts;
    at=required(r,root+0x58,28); d->x58=NEW(struct ftData_x58_t,1);
    d->x58->x0=BYTE(at); d->x58->x1=BYTE(at+1); d->x58->x4=floating(r,at+4);
    d->x58->x8=BYTE(at+8); d->x58->x9=BYTE(at+9); d->x58->xC=floating(r,at+12);
    d->x58->x10=BYTE(at+16); d->x58->x11=BYTE(at+17); d->x58->x18=floating(r,at+24);
    REQUIRE(d->x58->x0<140 && d->x58->x1<140 && d->x58->x8<140 && d->x58->x9<140 &&
        d->x58->x10<140 && d->x58->x11<140,"Native IK bone index invalid");
    d->x48_items=NEW(void*,4);
    at=PTR(root+0x48,16);
    if(at!=UINT32_MAX) {
        REGION(at,16);
        for(unsigned i=0;i<4;++i) {
            uint32_t p=PTR(at+i*4,24), article_unresolved;
            if(p!=UINT32_MAX) d->x48_items[i]=melee_web_article_decode(r,p,&article_unresolved);
        }
    }
    if(kind==FTKIND_MARIO)
        REQUIRE(d->x48_items[0] && d->x48_items[2],"Mario OnLoad requires fireball and cape Articles");
    else if(kind==FTKIND_DRMARIO)
        REQUIRE(d->x48_items[1] && d->x48_items[3],
            "Dr. Mario OnLoad requires vitamin and sheet Articles");
    else if(kind==FTKIND_FOX)
        REQUIRE(d->x48_items[0] && d->x48_items[1] && d->x48_items[2],
            "Fox OnLoad requires laser, blaster and illusion Articles");
    else if(kind==FTKIND_FALCO)
        REQUIRE(kind==FTKIND_FALCO && d->x48_items[0] && d->x48_items[1] && d->x48_items[3],
            "Falco OnLoad requires laser, blaster and Phantasm Articles");
    else
        REQUIRE((kind==FTKIND_MARS||kind==FTKIND_EMBLEM) && at==UINT32_MAX,
                "Marth/Roy source ftData must not invent an Article table");
    const unsigned ready[]={0,1,2,11,12,13,14,15,16,17,18,19,20,21,22};
    for(unsigned i=0;i<sizeof(ready)/sizeof(ready[0]);++i) *unresolved &= ~(1U<<ready[i]);
    if(actions) *unresolved &= ~(1U<<3);
    if(blends) *unresolved &= ~(1U<<4);
    if(choices) *unresolved &= ~(1U<<9);
    return d;
}

void* melee_web_fighter_data_article(void* data, uint32_t index)
{
    if (!data || index >= 4) return NULL;
    return ((ftData*)data)->x48_items[index];
}

int melee_web_fighter_data_set_metal(void* data,void* joint,uint32_t costumes,
    uint32_t dobj_count,uint32_t* unresolved,char* error,size_t size)
{
    ftData* d=data;
#define METAL_REQUIRE(c,m) do { if(!(c)){if(error&&size)snprintf(error,size,"%s",m);return 0;} } while(0)
    METAL_REQUIRE(d&&joint&&unresolved&&(*unresolved&(1U<<23))&&!d->x5C,
                  "Metal descriptor requires an unresolved nonnull source root");
    METAL_REQUIRE(costumes&&costumes<=16&&dobj_count&&dobj_count<=32&&d->x8&&d->x8->x0.vis_table&&d->x8->x0.model_num<=11,
                  "Metal descriptor or visibility count exceeds source capacity");
    for(uint32_t c=0;c<costumes;c++) {
        Counted* groups=d->x8->x0.vis_table[c][2];
        if(!groups)continue;
        for(uint32_t i=0;i<d->x8->x0.model_num;i++) {
            Counted* variants=groups[i].data;
            METAL_REQUIRE(groups[i].count<=128&&(!groups[i].count||variants),"Metal visibility variants are invalid");
            for(uint32_t j=0;j<groups[i].count;j++) {
                uint8_t* indices=variants[j].data;
                METAL_REQUIRE(variants[j].count<=32&&(!variants[j].count||indices),"Metal visibility indices are invalid");
                for(uint32_t k=0;k<variants[j].count;k++)
                    METAL_REQUIRE(indices[k]<dobj_count,"Metal visibility index exceeds hydrated DObj occurrences");
            }
        }
    }
    d->x5C=joint;*unresolved&=~(1U<<23);if(error&&size)*error=0;return 1;
#undef METAL_REQUIRE
}

void melee_web_fighter_data_set_guard(const MeleeWebNativeDat* r,uint32_t root,
    void* data,void* joint,uint32_t* unresolved)
{
    _Static_assert(offsetof(HSD_Joint,child)==2*sizeof(HSD_Joint*),"Source guard child alias");
    ftData* d=data; HSD_Joint* pose=joint;
    REQUIRE(d&&pose&&pose->child&&unresolved&&(*unresolved&(1U<<8))&&!d->x20,
            "Guard pose requires an unresolved source descriptor with a child");
    uint32_t at=required(r,root+0x20,4);
    required(r,at,64);
    /* Source guard consumers read only x0. The next relocated object starts
     * at +4; the decompiler's unused x8 member is not serialized here. */
    struct ftData_x20* guard=NEW(struct ftData_x20,1);
    guard->x0=(HSD_Joint**)pose;
    d->x20=guard; *unresolved&=~(1U<<8);
}

int melee_web_fighter_data_set_part_animations(void* data,void* groups,
    uint32_t group_count,uint32_t* unresolved,char* error,size_t size)
{
    ftData* d=data;
#define PART_REQUIRE(c,m) do { if(!(c)){if(error&&size)snprintf(error,size,"%s",m);return 0;} } while(0)
    PART_REQUIRE(d&&groups&&unresolved&&(*unresolved&(1U<<7))&&!d->x1C,
                 "Part animations require an unresolved nonnull source table");
    PART_REQUIRE(group_count>0&&group_count<=5,
                 "Part animation group count exceeds source Fighter storage");
    struct ftData_x1C** table=groups;
    for(uint32_t i=0;i<group_count;++i)
        PART_REQUIRE(table[i]&&table[i]->x2&&table[i]->x4&&table[i]->x8,
                     "Part animation group is incomplete");
    d->x1C=table;*unresolved&=~(1U<<7);if(error&&size)*error=0;return 1;
#undef PART_REQUIRE
}
