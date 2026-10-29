/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "lobj_presentation_cache.h"
#include <sysdolphin/baselib/class.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/object.h>
#include <stdlib.h>
#include <string.h>
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
#include <sysdolphin/baselib/list.h>
#endif
extern HSD_LObjInfo hsdLObj;
typedef struct LObjOwner {
    HSD_LObj* object;
    HSD_ClassInfo* descriptor;
    uint64_t generation, world;
    uint32_t poisoned;
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
    HSD_SList view_node;
    uint32_t source_member, view_member, anchor_member;
    uint32_t anchor_order;
#endif
    SlippiLObjCache cache;
    struct LObjOwner* next;
} LObjOwner;
typedef struct ClassProof {
    HSD_ClassInfo* identity;
    HSD_ClassInfo* parent;
    void (*info_init)(void);
    HSD_Class* (*alloc)(HSD_ClassInfo*);
    int (*init)(HSD_Class*);
    void (*release)(HSD_Class*);
    void (*destroy)(HSD_Class*);
    void (*amnesia)(HSD_ClassInfo*);
    int16_t extent, info_size;
} ClassProof;
static ClassProof object_proof, base_proof;
static ClassProof capture_class(HSD_ClassInfo* c){
    ClassProof p={c,c->head.parent,c->head.info_init,c->alloc,c->init,
        c->release,c->destroy,c->amnesia,c->head.obj_size,c->head.info_size};return p;
}
static int same_class(ClassProof p){
    HSD_ClassInfo* c=p.identity;
    return c&&(c->head.flags&1)&&c->head.parent==p.parent&&
        c->head.obj_size==p.extent&&c->head.info_size==p.info_size&&
        c->head.info_init==p.info_init&&c->alloc==p.alloc&&c->init==p.init&&
        c->release==p.release&&c->destroy==p.destroy&&c->amnesia==p.amnesia;
}
typedef struct DescriptorProof {
    HSD_ClassInfo* identity;
    HSD_ClassInfo* parent;
    void (*info_init)(void);
    HSD_Class* (*alloc)(HSD_ClassInfo*);
    int (*init)(HSD_Class*);
    void (*release)(HSD_Class*);
    void (*destroy)(HSD_Class*);
    void (*amnesia)(HSD_ClassInfo*);
    int (*load)(HSD_LObj*,HSD_LightDesc*);
    int16_t extent, info_size;
    uint32_t valid;
} DescriptorProof;
static LObjOwner* owners;
static DescriptorProof proof;
static uint64_t owner_world, owner_serial, owner_generation, interval_serial, active_token;
static uint32_t owner_count, first_failure;
static void panic_owner(uint32_t why);
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
typedef struct LObjCurrentSetView {
    HSD_SList* head;
    HSD_LObj* active[MAX_GXLIGHT];
    int32_t active_count;
    int32_t masks[4];
} LObjCurrentSetView;
static LObjCurrentSetView current_set_view, current_set_anchor;
static uint32_t current_set_ready, current_set_dirty, current_set_active;
static uint32_t current_set_anchor_members;
static uint64_t current_set_anchor_token;
extern int melee_web_lobj_current_set_anchor_capture(void);
extern int melee_web_lobj_current_set_view_import(void);
extern int melee_web_lobj_current_set_refresh_source_members(void);
#endif
enum { OWNER_MISSING=1, OWNER_DESCRIPTOR=2, OWNER_OOM=3, OWNER_WRAP=4,
       OWNER_LIFETIME=5, OWNER_REBIND=6, OWNER_AMNESIA=7, OWNER_INTERVAL=8,
       OWNER_DIRTY_SETTER=9, OWNER_WORLD=10, OWNER_SET_STATE=11,
       OWNER_SET_REF=12, OWNER_SET_OVERFLOW=13, OWNER_SET_VIEW=14,
       OWNER_SET_DUPLICATE=15 };
static void fail(uint32_t why){if(!first_failure)first_failure=why;}
static void event(void){if(owner_serial==UINT64_MAX)fail(OWNER_WRAP);else ++owner_serial;}
static int descriptor_valid(void){
    HSD_ClassInfo* c=proof.identity;
    return proof.valid&&same_class(object_proof)&&same_class(base_proof)&&c==(HSD_ClassInfo*)&hsdLObj&&(c->head.flags&1)&&
        c->head.parent==proof.parent&&proof.parent==&hsdObj&&
        c->head.obj_size==proof.extent&&proof.extent==sizeof(HSD_LObj)&&
        c->head.info_size==proof.info_size&&proof.info_size==sizeof(HSD_LObjInfo)&&
        c->head.info_init==proof.info_init&&c->alloc==proof.alloc&&c->init==proof.init&&
        c->release==proof.release&&c->destroy==proof.destroy&&c->amnesia==proof.amnesia&&
        hsdLObj.load==proof.load;
}
static LObjOwner* find(void* pointer){
    if(!owner_world)return NULL;
    for(LObjOwner* e=owners;e;e=e->next)if(e->object==pointer){
        if(e->poisoned||e->world!=owner_world)return NULL;
        if(!descriptor_valid()||e->descriptor!=(HSD_ClassInfo*)&hsdLObj||
           e->object->parent.parent.class_info!=e->descriptor){
            return NULL; /* Read-only refusal; lifecycle seams own sticky events. */
        }
        return e;
    }
    return NULL;
}
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
static void current_set_refuse(uint32_t why){fail(why);}
static void current_set_clear_members(void){
    for(LObjOwner* e=owners;e;e=e->next){
        e->view_member=0;
        e->view_node.next=NULL;
        e->view_node.data=e->object;
    }
    current_set_view.head=NULL;
}
static int current_set_independent_refs(LObjOwner* e,uint32_t* count){
    uint32_t raw;
    if(!e||!e->object||e->source_member>1||e->view_member>1){
        current_set_refuse(OWNER_SET_STATE);return 0;
    }
    raw=e->object->parent.ref_count;
    if(raw==HSD_OBJ_NOREF||(e->source_member&&raw<1)){
        current_set_refuse(OWNER_SET_REF);return 0;
    }
    *count=raw+1u-e->source_member;
    return 1;
}
static int current_set_independent_refs_for(LObjOwner* e,uint32_t source_member,
                                             uint32_t* count){
    uint32_t raw;
    if(!e||!e->object||source_member>1){
        current_set_refuse(OWNER_SET_STATE);return 0;
    }
    raw=e->object->parent.ref_count;
    if(raw==HSD_OBJ_NOREF||(source_member&&raw<1)){
        current_set_refuse(OWNER_SET_REF);return 0;
    }
    *count=raw+1u-source_member;
    return 1;
}
static int current_set_owner_valid(LObjOwner* e){
    if(!e||!e->object||find(e->object)!=e){
        current_set_refuse(OWNER_MISSING);return 0;
    }
    if(e->descriptor!=(HSD_ClassInfo*)&hsdLObj||e->world!=owner_world||e->poisoned||
       !descriptor_valid()||e->object->parent.parent.class_info!=e->descriptor){
        current_set_refuse(e->world!=owner_world?OWNER_WORLD:OWNER_DESCRIPTOR);
        return 0;
    }
    return 1;
}
static int current_set_validate_list(HSD_SList* head,uint32_t* count){
    uint32_t seen=0;
    for(HSD_SList* n=head;n;n=n->next){
        uint32_t prior=0;
        if(seen>=owner_count){current_set_refuse(OWNER_SET_STATE);return 0;}
        if(!find(n->data)){current_set_refuse(OWNER_MISSING);return 0;}
        for(HSD_SList* p=head;p!=n&&p&&prior<seen;p=p->next,++prior){
            if(p->data==n->data){current_set_refuse(OWNER_SET_DUPLICATE);return 0;}
        }
        if(prior!=seen){current_set_refuse(OWNER_SET_STATE);return 0;}
        ++seen;
    }
    *count=seen;return 1;
}
static int current_set_state_has(const SlippiLObjCurrentSetState* state,
                                 HSD_LObj* object){
    for(HSD_SList* n=state->head;n;n=n->next)if(n->data==object)return 1;
    return 0;
}
static int current_set_validate_state(const SlippiLObjCurrentSetState* state,
                                      uint32_t* members){
    if(!state||state->active_count<0||state->active_count>MAX_GXLIGHT-1||
       !current_set_validate_list(state->head,members)){
        current_set_refuse(OWNER_SET_STATE);return 0;
    }
    for(HSD_SList* n=state->head;n;n=n->next){
        LObjOwner* e=find(n->data);uint32_t independent;
        if(!e||!current_set_independent_refs_for(e,1,&independent)||independent==0){
            current_set_refuse(OWNER_SET_REF);return 0;
        }
    }
    for(int i=0;i<MAX_GXLIGHT;i++)if(state->active[i]){
        LObjOwner* e=find(state->active[i]);uint32_t independent;
        uint32_t member=(uint32_t)current_set_state_has(state,state->active[i]);
        if(!e){current_set_refuse(OWNER_MISSING);return 0;}
        if(!current_set_independent_refs_for(e,member,&independent)||independent==0){
            current_set_refuse(OWNER_SET_REF);return 0;
        }
    }
    return first_failure==0;
}
static int current_set_validate_view(uint32_t require_independent){
    uint32_t members=0;
    if(current_set_view.active_count<0||
       current_set_view.active_count>MAX_GXLIGHT-1){
        current_set_refuse(OWNER_SET_STATE);return 0;
    }
    for(HSD_SList* n=current_set_view.head;n;n=n->next){
        LObjOwner* e=NULL;uint32_t independent;
        if(members>=owner_count){current_set_refuse(OWNER_SET_STATE);return 0;}
        for(LObjOwner* candidate=owners;candidate;candidate=candidate->next)
            if(&candidate->view_node==n){e=candidate;break;}
        if(!e||!e->view_member||e->view_node.data!=e->object||
           !current_set_owner_valid(e)){
            current_set_refuse(OWNER_SET_STATE);return 0;
        }
        for(HSD_SList* prior=current_set_view.head;prior!=n;prior=prior->next)
            if(prior==n||prior->data==n->data){
                current_set_refuse(OWNER_SET_DUPLICATE);return 0;
            }
        if(!current_set_independent_refs(e,&independent))return 0;
        if(require_independent&&independent==0){
            current_set_refuse(OWNER_SET_REF);return 0;
        }
        ++members;
    }
    for(int i=0;i<MAX_GXLIGHT;i++)if(current_set_view.active[i]){
        LObjOwner* e=find(current_set_view.active[i]);uint32_t independent;
        if(!current_set_owner_valid(e))return 0;
        if(!current_set_independent_refs(e,&independent))return 0;
        if(require_independent&&independent==0){
            current_set_refuse(OWNER_SET_REF);return 0;
        }
    }
    return first_failure==0;
}
static void current_set_copy_fields(const SlippiLObjCurrentSetState* state){
    current_set_view.active_count=state->active_count;
    memcpy(current_set_view.active,state->active,sizeof(current_set_view.active));
    memcpy(current_set_view.masks,state->masks,sizeof(current_set_view.masks));
}
static int current_set_install_source_list(HSD_SList* head){
    HSD_SList** tail=&current_set_view.head;
    for(LObjOwner* e=owners;e;e=e->next){e->source_member=0;e->view_member=0;}
    current_set_view.head=NULL;
    for(HSD_SList* n=head;n;n=n->next){
        LObjOwner* e=find(n->data);
        if(!e||e->source_member){current_set_refuse(e?OWNER_SET_DUPLICATE:OWNER_MISSING);return 0;}
        e->source_member=1;e->view_member=1;
        e->view_node.data=e->object;e->view_node.next=NULL;
        *tail=&e->view_node;tail=&e->view_node.next;
    }
    return 1;
}
int slippi_lobj_current_set_import(const SlippiLObjCurrentSetState* state){
    uint32_t members;
    if(current_set_active||!owner_world||first_failure||
       !current_set_validate_state(state,&members))return 0;
    (void)members;
    /* Validate all source and host pointers before replacing the persistent
     * presentation selection. */
    if(!current_set_install_source_list(state->head))return 0;
    current_set_copy_fields(state);
    current_set_ready=1;current_set_dirty=0;
    return current_set_validate_view(1);
}
int slippi_lobj_current_set_refresh_source_members(const SlippiLObjCurrentSetState* state){
    uint32_t members;
    if(!owner_world||first_failure||!current_set_validate_state(state,&members))return 0;
    (void)members;
    for(LObjOwner* e=owners;e;e=e->next)e->source_member=0;
    for(HSD_SList* n=state->head;n;n=n->next){
        LObjOwner* e=find(n->data);
        if(!e||e->source_member){current_set_refuse(e?OWNER_SET_DUPLICATE:OWNER_MISSING);return 0;}
        e->source_member=1;
    }
    return first_failure==0&&current_set_validate_view(1);
}
int slippi_lobj_current_set_capture(const SlippiLObjCurrentSetState* state){
    uint32_t members=0,order=0;
    if(!owner_world||!active_token||current_set_active||first_failure)return 0;
    if(current_set_dirty||!current_set_ready){
        if(!slippi_lobj_current_set_import(state))return 0;
    }else if(!slippi_lobj_current_set_refresh_source_members(state))return 0;
    if(!current_set_validate_view(1))return 0;
    for(LObjOwner* e=owners;e;e=e->next)e->anchor_member=0;
    for(HSD_SList* n=current_set_view.head;n;n=n->next){
        LObjOwner* e=find(n->data);
        if(!e||order>=owner_count){current_set_refuse(OWNER_SET_STATE);return 0;}
        e->anchor_member=1;e->anchor_order=order++;
    }
    current_set_anchor=current_set_view;current_set_anchor.head=NULL;
    current_set_anchor_members=order;current_set_anchor_token=active_token;
    return 1;
}
HSD_SList **slippi_lobj_current_set_head_slot(HSD_SList **fallback){
    return current_set_active?&current_set_view.head:fallback;
}
HSD_LObj **slippi_lobj_current_set_active_slots(HSD_LObj **fallback){
    return current_set_active?current_set_view.active:fallback;
}
int32_t *slippi_lobj_current_set_count_slot(int32_t *fallback){
    return current_set_active?&current_set_view.active_count:fallback;
}
int32_t *slippi_lobj_current_set_mask_slot(unsigned index,int32_t *fallback){
    return current_set_active&&index<4?&current_set_view.masks[index]:fallback;
}
int slippi_lobj_current_set_view_active(void){return current_set_active!=0;}
void slippi_lobj_current_set_source_mutated(void){
    if(!current_set_active&&owner_world)current_set_dirty=1;
}
static int current_set_preflight_member(HSD_LObj* lobj,int require_member,
                                       int require_unlisted){
    LObjOwner* e=find(lobj);uint32_t independent;
    if(!current_set_owner_valid(e)||!current_set_independent_refs(e,&independent))return 0;
    if(require_member&&!e->view_member){
        current_set_refuse(OWNER_SET_STATE);return 0;
    }
    if(require_unlisted&&e->view_member){
        current_set_refuse(OWNER_SET_STATE);return 0;
    }
    if(independent==0){current_set_refuse(OWNER_SET_REF);return 0;}
    if(independent>=HSD_OBJ_NOREF){current_set_refuse(OWNER_SET_OVERFLOW);return 0;}
    return 1;
}
int slippi_lobj_current_set_preflight_add(HSD_LObj* lobj){
    if(!current_set_active||first_failure||!current_set_validate_view(1))return 0;
    if(!lobj)return 1;
    return current_set_preflight_member(lobj,0,0);
}
int slippi_lobj_current_set_insert_at(HSD_SList** position,HSD_LObj* lobj){
    HSD_SList** link;LObjOwner* e;
    if(!current_set_active||first_failure||!position||!lobj||
       !current_set_validate_view(1))return 0;
    for(link=&current_set_view.head;*link&&link!=position;link=&(*link)->next){}
    if(link!=position){current_set_refuse(OWNER_SET_STATE);return 0;}
    e=find(lobj);
    if(!e||!current_set_preflight_member(lobj,0,1))return 0;
    e->view_node.data=lobj;e->view_node.next=*position;
    *position=&e->view_node;e->view_member=1;
    return 1;
}
int slippi_lobj_current_set_preflight_delete(HSD_LObj* lobj){
    LObjOwner* e;
    if(!current_set_active||first_failure||!current_set_validate_view(1))return 0;
    if(!lobj)return 1;
    e=find(lobj);
    if(!e){
        for(HSD_SList* n=current_set_view.head;n;n=n->next)
            if(n->data==lobj){current_set_refuse(OWNER_MISSING);return 0;}
        return 1;
    }
    if(!e->view_member)return 1;
    return current_set_preflight_member(lobj,1,0);
}
int slippi_lobj_current_set_remove_at(HSD_SList** position,HSD_LObj* lobj){
    LObjOwner* e;
    if(!current_set_active||first_failure||!position||!lobj||
       !current_set_validate_view(1))return 0;
    e=find(lobj);
    if(!e||!e->view_member||*position!=&e->view_node||
       !current_set_preflight_member(lobj,1,0)){
        current_set_refuse(OWNER_SET_STATE);return 0;
    }
    *position=e->view_node.next;
    e->view_node.next=NULL;e->view_member=0;
    return 1;
}
static int current_set_chain_valid(HSD_LObj* head,int delete_only){
    uint32_t seen=0;
    for(HSD_LObj* l=head;l;){
        uint32_t prior=0;
        HSD_LObj* next;
        if(seen>=owner_count){current_set_refuse(OWNER_SET_STATE);return 0;}
        /* Prove this is a live canonical object before following its next
         * link.  DeleteCurrent's single-object unknown-pointer no-op is kept;
         * chain traversal has no such untyped escape hatch. */
        if(!current_set_owner_valid(find(l)))return 0;
        if(delete_only){
            if(!slippi_lobj_current_set_preflight_delete(l))return 0;
        }else if(!current_set_preflight_member(l,0,0))return 0;
        for(HSD_LObj* p=head;p!=l&&p&&prior<seen;p=p->next,++prior)
            if(p==l){current_set_refuse(OWNER_SET_DUPLICATE);return 0;}
        if(prior!=seen){current_set_refuse(OWNER_SET_STATE);return 0;}
        next=l->next;
        ++seen;
        l=next;
    }
    return 1;
}
int slippi_lobj_current_set_preflight_delete_chain(HSD_LObj* lobj){
    if(!current_set_active||first_failure||!current_set_validate_view(1))return 0;
    return current_set_chain_valid(lobj,1);
}
int slippi_lobj_current_set_preflight_remove_all(void){
    return current_set_active&&!first_failure&&current_set_validate_view(1);
}
int slippi_lobj_current_set_preflight_replace_all(HSD_LObj* lobj){
    return slippi_lobj_current_set_preflight_remove_all()&&
           current_set_chain_valid(lobj,0);
}
int slippi_lobj_current_set_remove_head(HSD_SList** head){
    HSD_LObj* object;
    if(!head||!*head){current_set_refuse(OWNER_SET_STATE);return 0;}
    object=(HSD_LObj*)(*head)->data;
    return slippi_lobj_current_set_remove_at(head,object);
}
void slippi_lobj_current_set_retire_blocked(void){
    current_set_refuse(OWNER_LIFETIME);
    HSD_Panic(__FILE__,__LINE__,"LObj retirement during presentation view");
    abort();
}
static void current_set_unlink_owner(LObjOwner* e){
    if(e->view_member){
        HSD_SList** p=&current_set_view.head;
        while(*p&&*p!=&e->view_node)p=&(*p)->next;
        if(*p)*p=e->view_node.next;
        e->view_node.next=NULL;e->view_member=0;current_set_dirty=1;
    }
    if(e->anchor_member){e->anchor_member=0;current_set_anchor_token=0;}
    e->source_member=0;
    for(int i=0;i<MAX_GXLIGHT;i++)if(current_set_view.active[i]==e->object)
        current_set_view.active[i]=NULL;
    for(int i=0;i<MAX_GXLIGHT;i++)if(current_set_anchor.active[i]==e->object)
        current_set_anchor.active[i]=NULL;
}
int slippi_lobj_current_set_view_begin(void){
    if(first_failure)return 0;
    if(!active_token)return 1;
    if(current_set_active||current_set_anchor_token!=active_token||
       !current_set_ready){current_set_refuse(OWNER_SET_VIEW);return 0;}
    if(current_set_dirty&&!melee_web_lobj_current_set_view_import())return 0;
    if(!current_set_validate_view(1))return 0;
    current_set_active=1;return 1;
}
int slippi_lobj_current_set_view_end(void){
    if(!current_set_active)return first_failure==0;
    /* Always stop routing before returning, including after sticky owner error. */
    current_set_active=0;
    if(first_failure)return 0;
    if(!current_set_validate_view(1)){current_set_active=0;return 0;}
    return 1;
}
int slippi_lobj_current_set_view_reset_after_restore(SlippiLObjAnchor a){
    uint32_t found=0;
    if(current_set_active||!slippi_lobj_anchor_admit(a)||
       current_set_anchor_token!=a.token||!melee_web_lobj_current_set_refresh_source_members()){
        current_set_refuse(OWNER_SET_VIEW);return 0;
    }
    if(current_set_anchor_members>owner_count||
       current_set_anchor.active_count<0||
       current_set_anchor.active_count>MAX_GXLIGHT-1){
        current_set_refuse(OWNER_SET_STATE);return 0;
    }
    /* Validate the complete ordered selection before changing any host node.
     * Restore refusal must not leave a partially rebuilt presentation list. */
    for(LObjOwner* e=owners;e;e=e->next){
        uint32_t independent;
        if(e->anchor_member){
            if(e->anchor_order>=current_set_anchor_members||
               !current_set_independent_refs(e,&independent)||independent==0){
                current_set_refuse(OWNER_SET_STATE);return 0;
            }
            ++found;
            for(LObjOwner* prior=owners;prior!=e;prior=prior->next)
                if(prior->anchor_member&&prior->anchor_order==e->anchor_order){
                    current_set_refuse(OWNER_SET_DUPLICATE);return 0;
                }
        }
    }
    if(found!=current_set_anchor_members){current_set_refuse(OWNER_SET_STATE);return 0;}
    for(int i=0;i<MAX_GXLIGHT;i++)if(current_set_anchor.active[i]){
        LObjOwner* e=find(current_set_anchor.active[i]);uint32_t independent;
        if(!e||!current_set_independent_refs(e,&independent)||independent==0){
            current_set_refuse(OWNER_SET_REF);return 0;
        }
    }
    current_set_clear_members();
    HSD_SList** tail=&current_set_view.head;
    found=0;
    for(uint32_t order=0;order<current_set_anchor_members;order++){
        LObjOwner* selected=NULL;
        for(LObjOwner* e=owners;e;e=e->next)if(e->anchor_member&&e->anchor_order==order){
            selected=e;
        }
        selected->view_node.data=selected->object;selected->view_node.next=NULL;
        selected->view_member=1;*tail=&selected->view_node;tail=&selected->view_node.next;
        ++found;
    }
    current_set_view.active_count=current_set_anchor.active_count;
    memcpy(current_set_view.active,current_set_anchor.active,sizeof(current_set_view.active));
    memcpy(current_set_view.masks,current_set_anchor.masks,sizeof(current_set_view.masks));
    current_set_dirty=0;current_set_ready=1;
    return current_set_validate_view(1);
}
#endif
static void panic_owner(uint32_t why){
    fail(why);HSD_Panic(__FILE__,__LINE__,"Unsupported Slippi LObj presentation owner");
    abort(); /* A provider returning from panic cannot become silent success. */
}
int slippi_lobj_world_begin(uint64_t world){
    if(!world||owner_world||owners||active_token){fail(OWNER_WORLD);return 0;}
    owner_world=world;first_failure=0;event();
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
    memset(&current_set_view,0,sizeof(current_set_view));
    memset(&current_set_anchor,0,sizeof(current_set_anchor));
    current_set_ready=current_set_dirty=current_set_active=0;
    current_set_anchor_members=0;current_set_anchor_token=0;
#endif
    return first_failure==0;
}
void slippi_lobj_world_end(uint64_t world){
    if(!world||world!=owner_world){fail(OWNER_WORLD);return;}
    if(active_token){
        fail(OWNER_INTERVAL);
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
        return;
#endif
    }
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
    if(current_set_active){fail(OWNER_INTERVAL);return;}
#endif
    active_token=0;event();
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
    current_set_active=0;current_set_ready=current_set_dirty=0;
    current_set_anchor_members=0;current_set_anchor_token=0;
    memset(&current_set_view,0,sizeof(current_set_view));
    memset(&current_set_anchor,0,sizeof(current_set_anchor));
#endif
    /* Ordinary source teardown/amnesia precedes this call. Never read an old
     * source header; nodes are host presentation storage, not source leases. */
    while(owners){LObjOwner* e=owners;owners=e->next;free(e);}
    owner_count=0;owner_world=0;proof.valid=0;
}
void slippi_lobj_descriptor_attest(void* info){
    HSD_ClassInfo* c=(HSD_ClassInfo*)info;
    if(c!=(HSD_ClassInfo*)&hsdLObj||!(c->head.flags&1)||c->head.parent!=&hsdObj||
       c->head.obj_size!=sizeof(HSD_LObj)||c->head.info_size!=sizeof(HSD_LObjInfo)||
       !(hsdObj.head.flags&1)||hsdObj.head.parent!=&hsdClass||
       hsdObj.head.obj_size!=sizeof(HSD_Obj)||hsdObj.head.info_size!=sizeof(HSD_ObjInfo)||
       !(hsdClass.head.flags&1)||hsdClass.head.parent!=NULL||
       hsdClass.head.obj_size!=sizeof(HSD_Class)||hsdClass.head.info_size!=sizeof(HSD_ClassInfo)){
        fail(OWNER_DESCRIPTOR);return;
    }
    /* This capability is published ONLY after the actual canonical source
     * initializer installs release/amnesia/load. It is not capture-time tuple
     * adoption. Custom initializers are deliberately unsupported. */
    if(owners){for(LObjOwner* e=owners;e;e=e->next)e->poisoned=OWNER_DESCRIPTOR;fail(OWNER_DESCRIPTOR);}
    object_proof=capture_class(&hsdObj);base_proof=capture_class(&hsdClass);
    proof=(DescriptorProof){c,c->head.parent,c->head.info_init,c->alloc,c->init,
        c->release,c->destroy,c->amnesia,hsdLObj.load,c->head.obj_size,c->head.info_size,1};
    event();
}
void slippi_lobj_constructed(void* object,void* info){
    if(info!=(void*)&hsdLObj)return; /* Not a claim about other source classes. */
    if(!owner_world||!object||!descriptor_valid()||first_failure){fail(OWNER_DESCRIPTOR);return;}
    for(LObjOwner* e=owners;e;e=e->next)if(e->object==object){fail(OWNER_LIFETIME);return;}
    if(active_token){event();fail(OWNER_LIFETIME);return;}
    if(owner_generation==UINT64_MAX||owner_serial==UINT64_MAX||owner_count==UINT32_MAX){fail(OWNER_WRAP);return;}
    HSD_LObj* p=(HSD_LObj*)object;
    if(p->parent.parent.class_info!=(HSD_ClassInfo*)&hsdLObj){fail(OWNER_DESCRIPTOR);return;}
    LObjOwner* e=(LObjOwner*)malloc(sizeof(*e));if(!e){fail(OWNER_OOM);return;}
    memset(e,0,sizeof(*e));e->object=p;e->descriptor=(HSD_ClassInfo*)&hsdLObj;
    e->generation=++owner_generation;e->world=owner_world;
    e->cache.hw_color=p->hw_color;e->cache.shininess=p->shininess;e->cache.lvec=p->lvec;
    e->cache.id=p->id;e->cache.spec_id=p->spec_id;e->cache.dirty=p->flags&0x180;
    memcpy(&e->cache.lightobj,&p->lightobj,64);memcpy(&e->cache.spec_lightobj,&p->spec_lightobj,64);
    e->next=owners;owners=e;++owner_count;event();
}
void slippi_lobj_retiring(void* object){
    LObjOwner** link=&owners;
    while(*link){LObjOwner* e=*link;if(e->object==object){
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
        if(active_token){panic_owner(OWNER_LIFETIME);}
        if(current_set_active)panic_owner(OWNER_LIFETIME);
        current_set_unlink_owner(e);
#else
        if(active_token)fail(OWNER_LIFETIME);
#endif
        *link=e->next;--owner_count;event();free(e);return;
    }link=&e->next;}
}
void slippi_lobj_rebound(void* object,void* previous,void* next){
    (void)previous;(void)next;
    for(LObjOwner* e=owners;e;e=e->next)if(e->object==object){
        e->poisoned=OWNER_REBIND;event();fail(OWNER_REBIND);return;
    }
}
void slippi_lobj_amnesia(void* info){
    if(info!=(void*)&hsdLObj)return;
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
    if(active_token)panic_owner(OWNER_AMNESIA);
#endif
    proof.valid=0;event();
    for(LObjOwner* e=owners;e;e=e->next)e->poisoned=OWNER_AMNESIA;
    if(owners||active_token)fail(OWNER_AMNESIA);
}
void slippi_lobj_require_descriptor(void* info){
    if(info!=(void*)&hsdLObj)panic_owner(OWNER_DESCRIPTOR);
}
SlippiLObjCache* slippi_lobj_cache_require(void* object){
    LObjOwner* e=find(object);if(!e||first_failure)panic_owner(OWNER_MISSING);return &e->cache;
}
void slippi_lobj_dirty_setter(uint32_t flags){
    if(active_token&&(flags&0x180))fail(OWNER_DIRTY_SETTER);
}
SlippiLObjAnchor slippi_lobj_anchor_capture(void){
    SlippiLObjAnchor a={0};
    if(!owner_world||!owner_count||active_token||first_failure||!descriptor_valid()||interval_serial==UINT64_MAX){fail(OWNER_INTERVAL);return a;}
    for(LObjOwner* e=owners;e;e=e->next)if(!find(e->object)){fail(OWNER_LIFETIME);return a;}
    active_token=++interval_serial;
    a.world=owner_world;a.serial=owner_serial;a.token=active_token;a.count=owner_count;
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
    if(!melee_web_lobj_current_set_anchor_capture()){
        active_token=0;current_set_anchor_token=0;return (SlippiLObjAnchor){0};
    }
#endif
    return a;
}
int slippi_lobj_anchor_admit(SlippiLObjAnchor a){
    if(first_failure||!a.token||a.token!=active_token||a.world!=owner_world||
       a.serial!=owner_serial||a.count!=owner_count||a.reserved||!descriptor_valid())return 0;
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
    if(current_set_anchor_token!=a.token||!current_set_ready)return 0;
#endif
    for(LObjOwner* e=owners;e;e=e->next)if(!find(e->object))return 0;
    return 1;
}
void slippi_lobj_anchor_disarm(SlippiLObjAnchor a){
    if(a.token&&a.token==active_token){
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
        if(current_set_active)fail(OWNER_SET_VIEW);
#endif
        active_token=0;
    }
}
uint32_t slippi_lobj_failure(void){return first_failure;}
