#include "gameplay_match_context.h"
#include "gameplay_bootstrap.h"
#include <melee/cm/camera.h>
#include <melee/cm/types.h>
#include <melee/ft/types.h>
#include <melee/ft/ftdata.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/aobj.h>
#include <melee/pl/player.h>
#include <melee/mp/mpcoll.h>
#include <melee/pl/plattack.h>
#include <melee/pl/plstale.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/shadow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Source-local accessor exposes the actual typed storage, never a mirror ABI. */
extern void* melee_web_camera_state(void);
extern CmSubject *cm_804D6458, *cm_804D645C, *cm_804D6460;
extern HSD_CObj* cm_804D6464;
extern u16 staleAttackInstance, unk_804D6480;
struct MeleeWebMatchContext {
    MeleeWebPlayerContext* player;
    MeleeWebCollision* collision;
    uint64_t generation, ticks;
    uint32_t slot, camera_count, seed;
    u32* saved_seed;
    u16 saved_stale, saved_attack;
    Camera saved_camera;
    CameraDebugMode saved_debug;
    HSD_PadStatus saved_pads[4];
    CmSubject* pool;
};
static MeleeWebMatchContext* owner;
static uint64_t shadow_generation;
static int fail(char* e,size_t n,const char* m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
static int live(MeleeWebMatchContext* h,char* e,size_t n)
{
    if(!h||h!=owner||h->generation!=melee_web_gameplay_stats().generation)
        return fail(e,n,"Match context requires its live owned source world");
    if(seed_ptr!=&h->seed||cm_804D645C!=h->pool)
        return fail(e,n,"Match source RNG or camera ownership changed");
    MeleeWebCollisionReadiness r;
    return melee_web_collision_readiness(h->collision,&r,e,n);
}
MeleeWebMatchContext* melee_web_match_begin(const MeleeWebMatchSettings* s,
    MeleeWebCollision* collision,char* e,size_t n)
{
    uint64_t generation=melee_web_gameplay_stats().generation;
    MeleeWebCollisionReadiness r;
    if(!s||!generation||owner||s->camera_subjects<1||s->camera_subjects>70||s->player.slot>=4){
        fail(e,n,"Match needs an unowned world, player slot 0..3 and 1..70 camera subjects");return NULL;
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
    h->player=melee_web_player_context_begin(&s->player,e,n);
    if(!h->player){free(h);return NULL;}
    h->generation=generation;h->collision=collision;h->slot=s->player.slot;
    h->camera_count=s->camera_subjects;h->seed=s->random_seed;
    h->saved_camera=*camera;h->saved_debug=cm_80453004;
    h->saved_seed=seed_ptr;h->saved_stale=staleAttackInstance;h->saved_attack=unk_804D6480;
    memcpy(h->saved_pads,HSD_PadGameStatus,sizeof(h->saved_pads));
    memset(HSD_PadGameStatus,0,sizeof(h->saved_pads));
    seed_ptr=&h->seed;
    plStale_InitAttackInstance();plAttack_80037590();
    /* Source match reset; these are per-query collision callbacks and an unused
     * event field. No prior fighters may survive into this exclusive context. */
    mpColl_80041C78();
    /* Keep the original allocator registered until bootstrap destroys its heap.
     * Reinitializing within a world would discard the retained free pool. */
    if(shadow_generation!=generation){HSD_ShadowInitAllocData();shadow_generation=generation;}
    Camera_80028B9C(s->camera_subjects);h->pool=cm_804D645C;
    owner=h;ok(e,n);return h;
}
int melee_web_match_create_fighter(MeleeWebMatchContext* h,char* e,size_t n)
{
    if(!live(h,e,n))return 0;
    StaticPlayer* p=Player_GetPtrForSlot(h->slot);
    if(p->player_entity[0]||p->player_entity[1])return fail(e,n,"Match player already has a fighter");
    Player_80031AD0(h->slot);
    if(!p->player_entity[0])return fail(e,n,"Original player constructor produced no fighter");
    return ok(e,n);
}
int melee_web_match_step(MeleeWebMatchContext* h,uint32_t ticks,char* e,size_t n)
{
    if(!live(h,e,n))return 0;
    if(ticks>36000)return fail(e,n,"Neutral scheduler request exceeds bounded 36000 ticks");
    if(!Player_GetPtrForSlot(h->slot)->player_entity[0])return fail(e,n,"Create the source fighter before stepping");
    for(uint32_t i=0;i<ticks;i++){
        memset(HSD_PadGameStatus,0,sizeof(h->saved_pads));
        if(!melee_web_gameplay_step(e,n))return 0;
        h->ticks++;
    }
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
static int eye_stats(Fighter* fp,MeleeWebMatchStats* out,char* e,size_t n)
{
    if(fp->tobj_list.n_costume_tobjs!=2)
        return fail(e,n,"Mario Wait requires exactly two original costume eye TObjs");
    HSD_MatAnimJoint* desc=CostumeListsForeachCharacter[fp->kind].costume_list[fp->x619_costume_id].x4;
    out->eye_count=2;
    for(unsigned eye=0;eye<2;eye++){
        HSD_TObj* tobj=fp->tobj_list.costume_tobjs[eye];
        if(!tobj||!tobj->aobj||!tobj->aobj->fobj||!tobj->imagetbl)
            return fail(e,n,"Original eye TObj has no attached native animation/image table");
        unsigned budget=4096;
        HSD_TexAnim* tex=find_texture(desc,tobj,&budget);
        if(!tex||!tex->n_imagetbl||!tex->aobjdesc)
            return fail(e,n,"Original eye TObj does not borrow an owned costume texture table");
        MeleeWebMatchEyeStats* result=&out->eyes[eye];
        result->image_count=tex->n_imagetbl;result->palette_count=tex->n_tluttbl;
        result->image_index=UINT32_MAX;
        for(unsigned i=0;i<tex->n_imagetbl;i++)
            if(tex->imagetbl[i]==tobj->imagedesc){result->image_index=i;break;}
        if(result->image_index==UINT32_MAX||!tobj->imagedesc||!tobj->imagedesc->image_ptr)
            return fail(e,n,"Original Wait eye selected an image outside its owned native table");
        result->palette_index=UINT32_MAX;
        if(tex->n_tluttbl){
            if(!tex->tluttbl||!tobj->tluttbl||tobj->tlut_no>=tex->n_tluttbl)
                return fail(e,n,"Original Wait eye selected a palette outside its owned table");
            /* Validate every source-created palette, including inactive ones. */
            for(unsigned i=0;i<tex->n_tluttbl;i++){
                HSD_Tlut* native=tobj->tluttbl[i];HSD_TlutDesc* owned=tex->tluttbl[i];
                if(!native||!owned||native->lut!=owned->lut||native->fmt!=owned->fmt||native->n_entries!=owned->n_entries)
                    return fail(e,n,"Original eye palette differs from retained native descriptor");
            }
            if(tobj->tluttbl[tex->n_tluttbl]!=NULL)
                return fail(e,n,"Original eye runtime palette table has no bounded terminator");
            result->palette_index=tobj->tlut_no;
        }
        result->animation_frame=tobj->aobj->curr_frame;
        result->animation_rate=tobj->aobj->framerate;
        if(result->animation_rate!=0)
            return fail(e,n,"Original costume eye animation must retain source command-controlled zero rate");
    }
    if(fp->tobj_list.costume_tobjs[0]==fp->tobj_list.costume_tobjs[1])
        return fail(e,n,"Mario eye slots unexpectedly share one runtime TObj");
    return 1;
}
int melee_web_match_stats(MeleeWebMatchContext* h,MeleeWebMatchStats* out,char* e,size_t n)
{
    if(!live(h,e,n))return 0;
    if(!out)return fail(e,n,"Match stats output is required");
    memset(out,0,sizeof(*out));out->ticks=h->ticks;out->random_seed=h->seed;
    StaticPlayer* p=Player_GetPtrForSlot(h->slot);
    out->live_fighters=(p->player_entity[0]!=NULL)+(p->player_entity[1]!=NULL);
    out->motion_id=-1;out->ground_or_air=-1;
    if(p->player_entity[0]){
        Fighter* fp=p->player_entity[0]->user_data;
        out->motion_id=fp->motion_id;out->ground_or_air=fp->ground_or_air;
        memcpy(out->position,&fp->cur_pos,sizeof(out->position));out->animation_frame=fp->cur_anim_frame;
        out->extra_model_objects=fp->x203C.count;
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
    /* Dispose the scoped source object through its registered destructor, as
     * bootstrap world disposal does. Player_80031EBC is an in-match despawn:
     * it first creates effect 0x43f and enters Sleep, leaving an effect object. */
    StaticPlayer* p=Player_GetPtrForSlot(h->slot);
    if(p->player_entity[0])HSD_GObjPLink_80390228(p->player_entity[0]);
    if(p->player_entity[1])HSD_GObjPLink_80390228(p->player_entity[1]);
    if(cm_804D6460||cm_804D6468||HSD_ShadowGetAllocData()->used)
        return fail(e,n,"Unload remaining camera subjects and shadows before match restore");
    if(!melee_web_player_context_end(h->player,e,n))return 0;
    HSD_Free(h->pool);cm_804D6458=NULL;cm_804D645C=NULL;
    *(Camera*)melee_web_camera_state()=h->saved_camera;cm_80453004=h->saved_debug;
    seed_ptr=h->saved_seed;staleAttackInstance=h->saved_stale;unk_804D6480=h->saved_attack;
    memcpy(HSD_PadGameStatus,h->saved_pads,sizeof(h->saved_pads));
    owner=NULL;free(h);return ok(e,n);
}
