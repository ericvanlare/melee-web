#include "gameplay_match_context.h"
#include "gameplay_match_rules.h"
#include "gameplay_bootstrap.h"
#include "gameplay_crowd.h"
#include "fighter_binding.h"
#include <melee/cm/camera.h>
#include <melee/cm/types.h>
#include <melee/ft/types.h>
#include <melee/ft/ftdata.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/aobj.h>
#include <melee/pl/player.h>
#include <melee/it/item.h>
#include <melee/mp/mpcoll.h>
#include <melee/pl/plattack.h>
#include <melee/pl/plstale.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/rumble.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/shadow.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Source-local accessor exposes the actual typed storage, never a mirror ABI. */
extern void* melee_web_camera_state(void);
extern CmSubject *cm_804D6458, *cm_804D645C, *cm_804D6460;
extern HSD_CObj* cm_804D6464;
extern u16 staleAttackInstance, unk_804D6480;
extern PadLibData default_libinfo_data;
extern HSD_PadStatus default_status_data;
extern HSD_RumbleData HSD_Rumble_804C22E0[4];
struct MeleeWebMatchContext {
    MeleeWebPlayerContext* players[MELEE_WEB_MATCH_MAX_PLAYERS];
    uint32_t player_count, slots[MELEE_WEB_MATCH_MAX_PLAYERS], controllers[MELEE_WEB_MATCH_MAX_PLAYERS];
    MeleeWebCollision* collision;
    uint64_t generation, ticks;
    uint32_t camera_count, seed;
    u32* saved_seed;
    u16 saved_stale, saved_attack;
    Camera saved_camera;
    CameraDebugMode saved_debug;
    HSD_PadStatus saved_pads[4], saved_master[4], saved_copy[4];
    PadLibData saved_pad_library;
    HSD_PadData input_queue;
    HSD_RumbleData saved_rumble[4];
    HSD_PadRumbleListData rumble_lists[12];
    CmSubject* pool;
    int crowd_started,input_restored;
};
static MeleeWebMatchContext* owner;
static uint64_t shadow_generation;
static int fail(char* e,size_t n,const char* m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
static int live(MeleeWebMatchContext* h,char* e,size_t n)
{
    if(!h||h!=owner||h->generation!=melee_web_gameplay_generation())
        return fail(e,n,"Match context requires its live owned source world");
    if(seed_ptr!=&h->seed||cm_804D645C!=h->pool)
        return fail(e,n,"Match source RNG or camera ownership changed");
    MeleeWebCollisionReadiness r;
    return melee_web_collision_readiness(h->collision,&r,e,n);
}
MeleeWebMatchContext* melee_web_match_begin(const MeleeWebMatchSettings* s,
    MeleeWebCollision* collision,char* e,size_t n)
{
    if(!s){fail(e,n,"Match settings are required");return NULL;}
    return melee_web_match_begin_players(&s->player,1,s->camera_subjects,s->random_seed,collision,e,n);
}
MeleeWebMatchContext* melee_web_match_begin_players(const MeleeWebPlayerSettings* players,
    uint32_t count,uint32_t camera_subjects,uint32_t seed,MeleeWebCollision* collision,char* e,size_t n)
{
    uint64_t generation=melee_web_gameplay_generation();
    MeleeWebCollisionReadiness r;
    if(!players||!generation||owner||count<1||count>MELEE_WEB_MATCH_MAX_PLAYERS||camera_subjects<count||camera_subjects>70){
        fail(e,n,"Match needs an unowned world, player slot 0..3 and 1..70 camera subjects");return NULL;
    }
    for(uint32_t i=0;i<count;i++){
        if(players[i].slot>=4||players[i].controller>=4){fail(e,n,"Match player slot/controller must be 0..3");return NULL;}
        for(uint32_t j=0;j<i;j++)if(players[i].slot==players[j].slot||players[i].controller==players[j].controller){
            fail(e,n,"Match players require distinct slots and controller ports");return NULL;
        }
    }
    if(!melee_web_collision_readiness(collision,&r,e,n))return NULL;
    if(!r.storage_owned||!r.original_indices_initialized){fail(e,n,"Match requires original collision indices");return NULL;}
    for(int slot=0;slot<6;slot++) {
        StaticPlayer* p=Player_GetPtrForSlot(slot);
        if(p->player_entity[0]||p->player_entity[1]) {
            fail(e,n,"Match requires exclusive source fighter ownership");return NULL;
        }
    }
    Camera* camera=melee_web_camera_state();
    if(cm_804D6458||cm_804D645C||cm_804D6460||cm_804D6468||cm_804D6464||camera->gobj||
       HSD_ShadowGetAllocData()->used){fail(e,n,"Source camera or shadow objects are already active");return NULL;}
    MeleeWebMatchContext* h=calloc(1,sizeof(*h));
    if(!h){fail(e,n,"Cannot allocate match context");return NULL;}
    for(uint32_t i=0;i<count;i++){
        h->players[i]=melee_web_player_context_begin(&players[i],e,n);
        if(!h->players[i]){
            while(i)melee_web_player_context_end(h->players[--i],NULL,0);
            free(h);return NULL;
        }
        h->slots[i]=players[i].slot;h->controllers[i]=players[i].controller;
    }
    h->player_count=count;h->generation=generation;h->collision=collision;
    h->camera_count=camera_subjects;h->seed=seed;
    h->saved_camera=*camera;h->saved_debug=cm_80453004;
    h->saved_seed=seed_ptr;h->saved_stale=staleAttackInstance;h->saved_attack=unk_804D6480;
    memcpy(h->saved_pads,HSD_PadGameStatus,sizeof(h->saved_pads));
    memcpy(h->saved_master,HSD_PadMasterStatus,sizeof(h->saved_master));
    memcpy(h->saved_copy,HSD_PadCopyStatus,sizeof(h->saved_copy));
    h->saved_pad_library=HSD_PadLibData;
    memcpy(h->saved_rumble,HSD_Rumble_804C22E0,sizeof(h->saved_rumble));
    /* Exact processing configuration from source gmMain_8015FD24. Hardware
     * PAD sampling/output remains the provider boundary. The source rumble
     * interpreter owns the same 12-list pool as gmMain_8015FD24. */
    HSD_PadLibData=default_libinfo_data;
    HSD_PadRumbleInit(12,h->rumble_lists);
    HSD_PadLibData.qnum=1;HSD_PadLibData.queue=&h->input_queue;
    HSD_PadLibData.clamp_stickType=0;HSD_PadLibData.clamp_stickShift=1;
    HSD_PadLibData.clamp_stickMax=80;HSD_PadLibData.clamp_stickMin=0;
    HSD_PadLibData.scale_stick=80;
    HSD_PadLibData.clamp_analogLRShift=1;HSD_PadLibData.clamp_analogLRMax=140;
    HSD_PadLibData.clamp_analogLRMin=0;HSD_PadLibData.scale_analogLR=140;
    for(unsigned i=0;i<4;i++){
        HSD_PadMasterStatus[i]=default_status_data;HSD_PadCopyStatus[i]=default_status_data;
        HSD_PadGameStatus[i]=default_status_data;
    }
    seed_ptr=&h->seed;
    plStale_InitAttackInstance();plAttack_80037590();
    /* Source match reset; these are per-query collision callbacks and an unused
     * event field. No prior fighters may survive into this exclusive context. */
    mpColl_80041C78();
    /* Keep the original allocator registered until bootstrap destroys its heap.
     * Reinitializing within a world would discard the retained free pool. */
    if(shadow_generation!=generation){HSD_ShadowInitAllocData();shadow_generation=generation;}
    Camera_80028B9C(camera_subjects);h->pool=cm_804D645C;
    owner=h;ok(e,n);return h;
}
int melee_web_match_create_fighter(MeleeWebMatchContext* h,char* e,size_t n)
{
    return melee_web_match_create_fighters(h,e,n);
}
int melee_web_match_restore_input(MeleeWebMatchContext* h,const MeleeWebPadState* state,char* e,size_t n)
{
    if(!live(h,e,n))return 0;
    if(!state||h->ticks||h->input_restored||HSD_PadLibData.qcount)
        return fail(e,n,"Input history may be restored once before match initialization");
    for(uint32_t i=0;i<h->player_count;i++)
        if(Player_GetPtrForSlot(h->slots[i])->player_entity[0])
            return fail(e,n,"Input history cannot be restored after fighter creation");
    melee_web_pad_state_apply(state);h->input_restored=1;
    return ok(e,n);
}
static int create_fighters(MeleeWebMatchContext* h,int activate,char* e,size_t n)
{
    if(!live(h,e,n))return 0;
    for(uint32_t i=0;i<h->player_count;i++){
        StaticPlayer* p=Player_GetPtrForSlot(h->slots[i]);
        if(p->player_entity[0]||p->player_entity[1])return fail(e,n,"Match player already has a fighter");
    }
    for(uint32_t i=0;i<h->player_count;i++){
        Player_80031AD0(h->slots[i]);
        if(!Player_GetPtrForSlot(h->slots[i])->player_entity[0])return fail(e,n,"Original player constructor produced no fighter");
        /* Fighter_Create disables input through ftLib_800867E8. Match spawn
         * completion in gm_16AE calls this original player activation routine. */
        if(activate)Player_80031848(h->slots[i]);
    }
    melee_web_match_rules_refresh();
    if(!melee_web_crowd_begin(e,n))return 0;
    h->crowd_started=1;
    return ok(e,n);
}
int melee_web_match_create_fighters(MeleeWebMatchContext* h,char* e,size_t n)
{
    return create_fighters(h,1,e,n);
}
int melee_web_match_create_fighters_intro(MeleeWebMatchContext* h,char* e,size_t n)
{
    return create_fighters(h,0,e,n);
}
static int sample_valid(const MeleeWebControllerSample* s)
{
    return isfinite(s->stick_x)&&fabsf(s->stick_x)<=1&&isfinite(s->stick_y)&&fabsf(s->stick_y)<=1&&
        isfinite(s->cstick_x)&&fabsf(s->cstick_x)<=1&&isfinite(s->cstick_y)&&fabsf(s->cstick_y)<=1&&
        isfinite(s->trigger_l)&&s->trigger_l>=0&&s->trigger_l<=1&&
        isfinite(s->trigger_r)&&s->trigger_r>=0&&s->trigger_r<=1;
}
int melee_web_match_step_raw(MeleeWebMatchContext* h,const PADStatus raw[4],char* e,size_t n)
{
    return melee_web_match_step_raw_phased(h,raw,NULL,NULL,NULL,NULL,e,n);
}
int melee_web_match_step_raw_phased(MeleeWebMatchContext* h,const PADStatus raw[4],
    MeleeWebMatchTickPhase renew,MeleeWebMatchTickPhase before,
    MeleeWebMatchTickPhase after,void* context,char* e,size_t n)
{
    if(!live(h,e,n))return 0;
    if(!raw)return fail(e,n,"Four raw controller samples are required");
    if(HSD_PadLibData.queue!=&h->input_queue||HSD_PadLibData.qnum!=1||HSD_PadLibData.qcount)
        return fail(e,n,"Owned source PAD queue changed or has an unconsumed sample");
    for(uint32_t i=0;i<h->player_count;i++)if(!Player_GetPtrForSlot(h->slots[i])->player_entity[0])
        return fail(e,n,"Create all source fighters before raw input stepping");
    memset(&h->input_queue,0,sizeof(h->input_queue));
    for(unsigned i=0;i<4;i++)h->input_queue.stat[i].err=-1;
    for(uint32_t i=0;i<h->player_count;i++)h->input_queue.stat[h->slots[i]]=raw[h->controllers[i]];
    HSD_PadLibData.qread=HSD_PadLibData.qwrite=0;HSD_PadLibData.qcount=1;
    /* Raw replay samples replace PADRead, not the source rumble interpreter
     * that precedes it in HSD_PadRenewRawStatus. One sample is supplied per tick. */
    HSD_PadRumbleInterpret();
    HSD_PadRenewMasterStatus();
    if(renew){if(!renew(context,e,n))return 0;}
    else{HSD_PadRenewCopyStatus();HSD_PadRenewGameStatus();}
    if(HSD_PadLibData.qcount)return fail(e,n,"Original controller processing did not consume raw sample");
    if(before&&!before(context,e,n))return 0;
    if(!melee_web_gameplay_step(e,n))return 0;
    h->ticks++;
    if(after&&!after(context,e,n))return 0;
    return ok(e,n);
}
int melee_web_match_step_inputs(MeleeWebMatchContext* h,const MeleeWebControllerSample samples[4],char* e,size_t n)
{
    if(!live(h,e,n))return 0;
    if(!samples)return fail(e,n,"Four controller samples are required");
    for(unsigned i=0;i<4;i++)if(!sample_valid(&samples[i]))return fail(e,n,"Controller axes/triggers must be finite and within normalized ranges");
    for(uint32_t i=0;i<h->player_count;i++)if(!Player_GetPtrForSlot(h->slots[i])->player_entity[0])
        return fail(e,n,"Create all source fighters before stepping");
    for(uint32_t i=0;i<h->player_count;i++){
        HSD_PadStatus* pad=&HSD_PadGameStatus[h->slots[i]];
        const MeleeWebControllerSample* sample=&samples[h->controllers[i]];
        u32 previous=pad->button;
        memset(pad,0,sizeof(*pad));pad->button=sample->buttons;pad->last_button=previous;
        pad->trigger=sample->buttons&~previous;pad->release=previous&~sample->buttons;
        pad->nml_stickX=sample->stick_x;pad->nml_stickY=sample->stick_y;
        pad->nml_subStickX=sample->cstick_x;pad->nml_subStickY=sample->cstick_y;
        pad->nml_analogL=sample->trigger_l;pad->nml_analogR=sample->trigger_r;
        /* Source Fighter consumers use normalized values; raw fields remain
         * zero because this is an explicit post-normalization input boundary. */
    }
    if(!melee_web_gameplay_step(e,n))return 0;
    h->ticks++;return ok(e,n);
}
int melee_web_match_step(MeleeWebMatchContext* h,uint32_t ticks,char* e,size_t n)
{
    if(!live(h,e,n))return 0;
    if(ticks>36000)return fail(e,n,"Neutral scheduler request exceeds bounded 36000 ticks");
    const MeleeWebControllerSample neutral[4]={{0}};
    for(uint32_t i=0;i<ticks;i++)if(!melee_web_match_step_inputs(h,neutral,e,n))return 0;
    return ok(e,n);
}
/* HSD_TObjAddAnim borrows imagetbl from the retained TexAnim descriptor and
 * creates runtime palette objects from its TLUT descriptors. Match that exact
 * owner identity before reading either indexed table. */
static HSD_TexAnim* find_texture(HSD_MatAnimJoint* joint,HSD_TObj* tobj,unsigned* budget)
{
    for(;joint;joint=joint->next){
        if(!*budget)return NULL;
        --*budget;
        for(HSD_MatAnim* mat=joint->matanim;mat;mat=mat->next){
            if(!*budget)return NULL;
            --*budget;
            for(HSD_TexAnim* tex=mat->texanim;tex;tex=tex->next){
                if(!*budget)return NULL;
                --*budget;
                if(tex->imagetbl==tobj->imagetbl&&tex->id==tobj->id)return tex;
            }
        }
        HSD_TexAnim* found=find_texture(joint->child,tobj,budget);
        if(found)return found;
    }
    return NULL;
}
typedef struct BaseTextureSearch {
    HSD_TObj* runtime;
    HSD_TObjDesc* exact;
} BaseTextureSearch;
/* HSD_TObjAddAnim borrows the animation table but leaves imagedesc/tlut at
 * the descriptor loaded by HSD_TObjLoadDesc until TIMG/TCLT selects a table
 * entry. Walk the retained fighter joint graph to prove that default state is
 * owned, while retaining a bounded traversal for malformed/cyclic graphs. */
static int find_base_texture(HSD_Joint* joint,BaseTextureSearch* search,unsigned* budget)
{
    for(;joint;joint=joint->next){
        if(!*budget)return 0;
        --*budget;
        if(!(joint->flags&(JOBJ_PTCL|JOBJ_SPLINE))){
            HSD_DObjDesc* dobj=joint->u.dobjdesc;
            for(;dobj;dobj=dobj->next){
                if(!*budget)return 0;
                --*budget;
                HSD_MObjDesc* mobj=dobj->mobjdesc;
                if(mobj){
                    for(HSD_TObjDesc* tex=mobj->texdesc;tex;tex=tex->next){
                        if(!*budget)return 0;
                        --*budget;
                        if(tex->id==search->runtime->id&&
                           tex->imagedesc==search->runtime->imagedesc)
                            search->exact=tex;
                    }
                }
            }
        }
        if(!find_base_texture(joint->child,search,budget))return 0;
    }
    return 1;
}
static int base_tlut_matches(const HSD_Tlut* actual,const HSD_TlutDesc* owned)
{
    /* GX may assign tlut_name while preparing a draw; source ownership is
     * the authored LUT storage/format/entry count, which remain stable. */
    return actual&&owned&&actual->lut==owned->lut&&actual->fmt==owned->fmt&&
        actual->n_entries==owned->n_entries;
}
static HSD_TObjDesc* owned_base_texture(HSD_Joint* root,HSD_TObj* runtime)
{
    BaseTextureSearch search={runtime,NULL};
    unsigned budget=16384;
    if(!find_base_texture(root,&search,&budget))return NULL;
    return search.exact;
}
/* HSD_TObjLoadDesc copies the authored TlutDesc (same LUT storage, format and
 * entry count) into the runtime TObj, and a playing TIMG animation repoints
 * imagedesc into the borrowed animation table, so a base descriptor can no
 * longer be found by imagedesc identity. Prove palette ownership instead: the
 * runtime palette must be the authored LUT of a retained costume texture with
 * the same texture id. */
static int find_base_palette(HSD_Joint* joint,HSD_TObj* runtime,unsigned* budget)
{
    for(;joint;joint=joint->next){
        if(!*budget)return 0;
        --*budget;
        if(!(joint->flags&(JOBJ_PTCL|JOBJ_SPLINE))){
            HSD_DObjDesc* dobj=joint->u.dobjdesc;
            for(;dobj;dobj=dobj->next){
                if(!*budget)return 0;
                --*budget;
                HSD_MObjDesc* mobj=dobj->mobjdesc;
                if(mobj){
                    for(HSD_TObjDesc* tex=mobj->texdesc;tex;tex=tex->next){
                        if(!*budget)return 0;
                        --*budget;
                        if(tex->id==runtime->id&&base_tlut_matches(runtime->tlut,tex->tlutdesc))
                            return 1;
                    }
                }
            }
        }
        if(!find_base_palette(joint->child,runtime,budget))return 0;
    }
    return 1;
}
static int owned_base_palette(HSD_Joint* root,HSD_TObj* runtime)
{
    unsigned budget=16384;
    return find_base_palette(root,runtime,&budget);
}
static int eye_stats(Fighter* fp,MeleeWebMatchStats* out,char* e,size_t n)
{
    HSD_MatAnimJoint* desc=CostumeListsForeachCharacter[fp->kind].costume_list[fp->x619_costume_id].x4;
    HSD_Joint* owned_joint=CostumeListsForeachCharacter[fp->kind].costume_list[fp->x619_costume_id].joint;
    if(!owned_joint)return fail(e,n,"Original eye telemetry has no owned costume descriptor graph");
    const int material_required=melee_web_fighter_costume_material_required(fp->kind,fp->x619_costume_id);
    if(material_required<0)return fail(e,n,"Original eye telemetry has no source costume identity");
    if(!material_required){
        if(desc||fp->tobj_list.n_costume_tobjs)
            return fail(e,n,"Authored null costume material unexpectedly has animated texture owners");
        out->eye_count=0;
        return 1;
    }
    /* The costume texture map is authored per fighter (Mario/Mewtwo four-part
     * families differ). The runtime collection must match that authored count
     * exactly; the declared per-tick eye stats keep two recorded slots. The
     * original collector stores at most five costume TObjs (ftAnim_80070200
     * asserts against CostumeTObjList::costume_tobjs[5]), so a larger authored
     * map has no defined runtime state to observe. */
    const unsigned authored=fp->ft_data->x8->x8.x8;
    if(fp->tobj_list.n_costume_tobjs!=authored||!authored||authored>5)
        return fail(e,n,"Animated costume telemetry does not match its authored texture map");
    if(authored<2)
        return fail(e,n,"Animated costume telemetry requires at least two authored texture TObjs");
    out->eye_count=2;
    for(unsigned eye=0;eye<authored;eye++){
        HSD_TObj* tobj=fp->tobj_list.costume_tobjs[eye];
        if(!tobj||!tobj->aobj||!tobj->aobj->fobj||!tobj->imagetbl)
            return fail(e,n,"Original eye TObj has no attached native animation/image table");
        unsigned budget=4096;
        HSD_TexAnim* tex=find_texture(desc,tobj,&budget);
        if(!tex||!tex->n_imagetbl||!tex->imagetbl||!tex->aobjdesc)
            return fail(e,n,"Original eye TObj does not borrow an owned costume texture table");
        MeleeWebMatchEyeStats* result=eye<2?&out->eyes[eye]:NULL;
        const unsigned image_count=tex->n_imagetbl;
        unsigned image_index=UINT32_MAX;
        for(unsigned i=0;i<image_count;i++)
            if(tex->imagetbl[i]==tobj->imagedesc){image_index=i;break;}
        HSD_TObjDesc* base=NULL;
        int image_is_base;
        if(image_index==UINT32_MAX){
            base=owned_base_texture(owned_joint,tobj);
            if(!base||!base->imagedesc||!base->imagedesc->image_ptr)
                return fail(e,n,"Original eye base TObj is outside its owned costume descriptor graph");
            image_is_base=1;
        }else{
            image_is_base=0;
        }
        if(!tobj->imagedesc||!tobj->imagedesc->image_ptr)
            return fail(e,n,"Original eye selected a malformed image descriptor");
        unsigned palette_index=UINT32_MAX;
        int palette_is_base;
        if(tex->n_tluttbl){
            if(!tex->tluttbl||!tobj->tluttbl)
                return fail(e,n,"Original eye has no owned runtime palette table");
            /* Validate every source-created palette, including inactive ones. */
            for(unsigned i=0;i<tex->n_tluttbl;i++){
                HSD_Tlut* native=tobj->tluttbl[i];HSD_TlutDesc* owned=tex->tluttbl[i];
                if(!native||!owned||native->lut!=owned->lut||native->fmt!=owned->fmt||native->n_entries!=owned->n_entries)
                    return fail(e,n,"Original eye palette differs from retained native descriptor");
            }
            if(tobj->tluttbl[tex->n_tluttbl]!=NULL)
                return fail(e,n,"Original eye runtime palette table has no bounded terminator");
            if(tobj->tlut_no<tex->n_tluttbl){
                palette_index=tobj->tlut_no;
                palette_is_base=0;
            }else{
                if(!base)base=owned_base_texture(owned_joint,tobj);
                if(!base)return fail(e,n,"Original eye base palette has no owned costume descriptor");
                if((base->tlutdesc&&!base_tlut_matches(tobj->tlut,base->tlutdesc))||
                   (!base->tlutdesc&&tobj->tlut))
                    return fail(e,n,"Original eye base palette is outside its owned costume descriptor");
                palette_is_base=1;
            }
        }else{
            if(tobj->tlut){
                if(!base)base=owned_base_texture(owned_joint,tobj);
                if(!(base&&base->tlutdesc&&base_tlut_matches(tobj->tlut,base->tlutdesc))&&
                   !owned_base_palette(owned_joint,tobj))
                    return fail(e,n,"Original eye base palette is outside its owned costume descriptor");
            }
            palette_is_base=1;
        }
        const float animation_frame=tobj->aobj->curr_frame;
        const float animation_rate=tobj->aobj->framerate;
        if(animation_rate!=0)
            return fail(e,n,"Original costume eye animation must retain source command-controlled zero rate");
        if(result){
            result->image_count=image_count;result->palette_count=tex->n_tluttbl;
            result->image_index=image_index;result->image_is_base=(uint8_t)image_is_base;
            result->palette_index=palette_index;result->palette_is_base=(uint8_t)palette_is_base;
            result->animation_frame=animation_frame;result->animation_rate=animation_rate;
        }
    }
    if(fp->tobj_list.costume_tobjs[0]==fp->tobj_list.costume_tobjs[1])
        return fail(e,n,"Recorded eye slots unexpectedly share one runtime TObj");
    return 1;
}
int melee_web_match_stats(MeleeWebMatchContext* h,MeleeWebMatchStats* out,char* e,size_t n)
{
    return melee_web_match_player_stats(h,0,out,e,n);
}
int melee_web_match_player_stats(MeleeWebMatchContext* h,uint32_t index,MeleeWebMatchStats* out,char* e,size_t n)
{
    if(!live(h,e,n))return 0;
    if(!out||index>=h->player_count)return fail(e,n,"Match stats output and owned player index are required");
    memset(out,0,sizeof(*out));out->ticks=h->ticks;out->random_seed=h->seed;
    StaticPlayer* p=Player_GetPtrForSlot(h->slots[index]);
    out->live_fighters=(p->player_entity[0]!=NULL)+(p->player_entity[1]!=NULL);
    out->motion_id=-1;out->ground_or_air=-1;out->player_slot=h->slots[index];
    out->stocks=Player_GetStocks(h->slots[index]);
    if(p->player_entity[0]){
        Fighter* fp=p->player_entity[0]->user_data;
        out->motion_id=fp->motion_id;out->ground_or_air=fp->ground_or_air;
        memcpy(out->position,&fp->cur_pos,sizeof(out->position));out->facing_direction=fp->facing_dir;
        out->animation_frame=fp->cur_anim_frame;
        out->extra_model_objects=fp->x203C.count;
        out->damage_percent=fp->dmg.x1830_percent;out->shield_health=fp->shield_health;
        out->source_stick[0]=fp->input.lstick[0].x;out->source_stick[1]=fp->input.lstick[0].y;
        out->source_triggers=fp->input.triggers[0];out->held_buttons=fp->input.held_buttons[0];
        out->pressed_buttons=fp->input.pressed_buttons;out->released_buttons=fp->input.released_buttons;
        if(!eye_stats(fp,out,e,n))return 0;
    }
    for(CmSubject* subject=cm_804D6460;subject;subject=subject->next){
        if(++out->camera_subjects>h->camera_count)return fail(e,n,"Source camera subject list exceeds owned pool");
    }
    return ok(e,n);
}
int melee_web_match_end(MeleeWebMatchContext* h,char* e,size_t n)
{
    if(!h)return ok(e,n);
    if(!live(h,e,n))return 0;
    /* Items may retain their fighter owner during source destruction. */
    while(((HSD_GObj**)HSD_GObj_Entities)[9])Item_8026A8EC(((HSD_GObj**)HSD_GObj_Entities)[9]);
    if(h->crowd_started){
        if(!melee_web_crowd_end(e,n))return 0;
        h->crowd_started=0;
    }
    /* Dispose the scoped source object through its registered destructor, as
     * bootstrap world disposal does. Player_80031EBC is an in-match despawn:
     * it first creates effect 0x43f and enters Sleep, leaving an effect object. */
    for(uint32_t i=0;i<h->player_count;i++){
        StaticPlayer* p=Player_GetPtrForSlot(h->slots[i]);
        if(p->player_entity[0])HSD_GObjPLink_80390228(p->player_entity[0]);
        if(p->player_entity[1])HSD_GObjPLink_80390228(p->player_entity[1]);
    }
    if(cm_804D6460||cm_804D6468||HSD_ShadowGetAllocData()->used)
        return fail(e,n,"Unload remaining camera subjects and shadows before match restore");
    for(uint32_t i=0;i<h->player_count;i++){
        if(!melee_web_player_context_end(h->players[i],e,n))return 0;
        h->players[i]=NULL;
    }
    HSD_Free(h->pool);cm_804D6458=NULL;cm_804D645C=NULL;
    *(Camera*)melee_web_camera_state()=h->saved_camera;cm_80453004=h->saved_debug;
    seed_ptr=h->saved_seed;staleAttackInstance=h->saved_stale;unk_804D6480=h->saved_attack;
    memcpy(HSD_PadGameStatus,h->saved_pads,sizeof(h->saved_pads));
    memcpy(HSD_PadMasterStatus,h->saved_master,sizeof(h->saved_master));
    memcpy(HSD_PadCopyStatus,h->saved_copy,sizeof(h->saved_copy));
    HSD_PadLibData=h->saved_pad_library;
    for(unsigned i=0;i<4;i++)PADControlMotor(i,PAD_MOTOR_STOP_HARD);
    memcpy(HSD_Rumble_804C22E0,h->saved_rumble,sizeof(h->saved_rumble));
    owner=NULL;free(h);return ok(e,n);
}
