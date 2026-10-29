/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "tobj_presentation_cache.h"
#include <sysdolphin/baselib/class.h>
#include <sysdolphin/baselib/debug.h>
#include <stdlib.h>
#include <string.h>
extern HSD_TObjInfo hsdTObj;
typedef struct TObjCache { Mtx matrix; uint32_t dirty; } TObjCache;
enum { ROLE_PENDING=0, ROLE_MANAGED=1, ROLE_SHADOW=2 };
typedef struct TObjOwner {
    HSD_TObj* object;
    HSD_ClassInfo* descriptor;
    uint64_t generation, world;
    uint32_t poisoned, role;
    TObjCache cache;
    struct TObjOwner* next;
} TObjOwner;
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
    int (*load)(HSD_TObj*,HSD_TObjDesc*);
    void (*make_mtx)(HSD_TObj*);
    __typeof__(((HSD_TObjInfo*)0)->make_texp) make_texp;
    int16_t extent, info_size;
    uint32_t valid;
} DescriptorProof;
static TObjOwner* owners;
static DescriptorProof proof;
static uint64_t owner_world, owner_serial, owner_generation, interval_serial, active_token;
static uint32_t owner_count, first_failure;
/* Read only from a source/map-pinned post-abort diagnostic. Volatile keeps
 * these failure-path breadcrumbs in the frozen native image without exports. */
static volatile uint32_t first_failure_line, first_failure_detail;
enum { OWNER_MISSING=1, OWNER_DESCRIPTOR=2, OWNER_OOM=3, OWNER_WRAP=4,
       OWNER_LIFETIME=5, OWNER_REBIND=6, OWNER_AMNESIA=7, OWNER_INTERVAL=8,
       OWNER_ROLE=9, OWNER_WORLD=10, OWNER_LOAD=11, OWNER_CUSTOM=12 };
static void fail_at(uint32_t why,uint32_t line,uint32_t detail){
    if(!first_failure){first_failure=why;first_failure_line=line;first_failure_detail=detail;}
}
#define fail(why) fail_at((why),(uint32_t)__LINE__,0)
static void event(void){if(owner_serial==UINT64_MAX)fail(OWNER_WRAP);else ++owner_serial;}
static int descriptor_valid(void){
    HSD_ClassInfo* c=proof.identity;
    return proof.valid&&same_class(object_proof)&&same_class(base_proof)&&c==(HSD_ClassInfo*)&hsdTObj&&(c->head.flags&1)&&
        c->head.parent==proof.parent&&proof.parent==&hsdObj&&
        c->head.obj_size==proof.extent&&proof.extent==sizeof(HSD_TObj)&&
        c->head.info_size==proof.info_size&&proof.info_size==sizeof(HSD_TObjInfo)&&
        c->head.info_init==proof.info_init&&c->alloc==proof.alloc&&c->init==proof.init&&
        c->release==proof.release&&c->destroy==proof.destroy&&c->amnesia==proof.amnesia&&
        hsdTObj.load==proof.load&&hsdTObj.make_mtx==proof.make_mtx&&hsdTObj.make_texp==proof.make_texp;
}
static TObjOwner* find(void* pointer){
    if(!owner_world)return NULL;
    for(TObjOwner* e=owners;e;e=e->next)if(e->object==pointer){
        if(e->poisoned||e->world!=owner_world)return NULL;
        if(!descriptor_valid()||e->descriptor!=(HSD_ClassInfo*)&hsdTObj||
           e->object->parent.parent.class_info!=e->descriptor){
            return NULL; /* Read-only refusal; lifecycle seams own sticky events. */
        }
        return e;
    }
    return NULL;
}
static void panic_owner(uint32_t why){
    fail(why);
    OSReport("FIRST_FAILURE_CONTEXT={\"code\":%u,\"line\":%u,\"detail\":%u}\n",
        (unsigned)first_failure,(unsigned)first_failure_line,(unsigned)first_failure_detail);
    HSD_Panic(__FILE__,__LINE__,"Unsupported Slippi TObj presentation owner");
    abort(); /* A provider returning from panic cannot become silent success. */
}
int slippi_tobj_world_begin(uint64_t world){
    if(!world||owner_world||owners||active_token){fail(OWNER_WORLD);return 0;}
    owner_world=world;first_failure=first_failure_line=first_failure_detail=0;event();return first_failure==0;
}
void slippi_tobj_world_end(uint64_t world){
    if(!world||world!=owner_world){fail(OWNER_WORLD);return;}
    if(active_token)fail(OWNER_INTERVAL);
    active_token=0;event();
    /* Ordinary source teardown/amnesia precedes this call. Never read an old
     * source header; nodes are host presentation storage, not source leases. */
    while(owners){TObjOwner* e=owners;owners=e->next;free(e);}
    owner_count=0;owner_world=0;proof.valid=0;
}
void slippi_tobj_descriptor_attest(void* info){
    HSD_ClassInfo* c=(HSD_ClassInfo*)info;
    if(c!=(HSD_ClassInfo*)&hsdTObj||!(c->head.flags&1)||c->head.parent!=&hsdObj||
       c->head.obj_size!=sizeof(HSD_TObj)||c->head.info_size!=sizeof(HSD_TObjInfo)||
       !(hsdObj.head.flags&1)||hsdObj.head.parent!=&hsdClass||
       hsdObj.head.obj_size!=sizeof(HSD_Obj)||hsdObj.head.info_size!=sizeof(HSD_ObjInfo)||
       !(hsdClass.head.flags&1)||hsdClass.head.parent!=NULL||
       hsdClass.head.obj_size!=sizeof(HSD_Class)||hsdClass.head.info_size!=sizeof(HSD_ClassInfo)){
        fail(OWNER_DESCRIPTOR);return;
    }
    /* This capability is published ONLY after the actual canonical source
     * initializer installs release/amnesia/load. It is not capture-time tuple
     * adoption. Custom initializers are deliberately unsupported. */
    if(owners){for(TObjOwner* e=owners;e;e=e->next)e->poisoned=OWNER_DESCRIPTOR;fail(OWNER_DESCRIPTOR);}
    object_proof=capture_class(&hsdObj);base_proof=capture_class(&hsdClass);
    proof=(DescriptorProof){c,c->head.parent,c->head.info_init,c->alloc,c->init,
        c->release,c->destroy,c->amnesia,hsdTObj.load,hsdTObj.make_mtx,hsdTObj.make_texp,c->head.obj_size,c->head.info_size,1};
    event();
}
void slippi_tobj_constructed(void* object,void* info){
    if(info!=(void*)&hsdTObj)return; /* Not a claim about other source classes. */
    if(!owner_world||!object||!descriptor_valid()||first_failure){fail(OWNER_DESCRIPTOR);return;}
    for(TObjOwner* e=owners;e;e=e->next)if(e->object==object){fail(OWNER_LIFETIME);return;}
    if(active_token){event();fail(OWNER_LIFETIME);return;}
    if(owner_generation==UINT64_MAX||owner_serial==UINT64_MAX||owner_count==UINT32_MAX){fail(OWNER_WRAP);return;}
    HSD_TObj* p=(HSD_TObj*)object;
    if(p->parent.parent.class_info!=(HSD_ClassInfo*)&hsdTObj){fail(OWNER_DESCRIPTOR);return;}
    TObjOwner* e=(TObjOwner*)malloc(sizeof(*e));if(!e){fail(OWNER_OOM);return;}
    memset(e,0,sizeof(*e));e->object=p;e->descriptor=(HSD_ClassInfo*)&hsdTObj;
    e->generation=++owner_generation;e->world=owner_world;
    e->role=ROLE_PENDING; /* Constructor identity, never pre-Load cache adoption. */
    e->next=owners;owners=e;++owner_count;event();
}
void slippi_tobj_retiring(void* object){
    TObjOwner** link=&owners;
    while(*link){TObjOwner* e=*link;if(e->object==object){
        if(active_token)fail(OWNER_LIFETIME);
        *link=e->next;--owner_count;event();free(e);return;
    }link=&e->next;}
}
void slippi_tobj_rebound(void* object,void* previous,void* next){
    (void)previous;(void)next;
    for(TObjOwner* e=owners;e;e=e->next)if(e->object==object){
        e->poisoned=OWNER_REBIND;event();fail(OWNER_REBIND);return;
    }
}
void slippi_tobj_amnesia(void* info){
    if(info!=(void*)&hsdTObj)return;
    proof.valid=0;event();
    for(TObjOwner* e=owners;e;e=e->next)e->poisoned=OWNER_AMNESIA;
    if(owners||active_token)fail(OWNER_AMNESIA);
}
static int admissible(TObjOwner* e){
    if(!find(e->object))return 0;
    if(e->role==ROLE_SHADOW)return 1; /* No managed matrix/flags access. */
    if(e->role!=ROLE_MANAGED)return 0;
    const uint32_t c=e->object->flags&TEX_COORD_MASK;
    return c==TEX_COORD_UV||c==TEX_COORD_REFLECTION||c==TEX_COORD_TOON;
}
static TObjOwner* require_owner(void* object){
    TObjOwner* e=find(object);if(!e||first_failure)panic_owner(OWNER_MISSING);return e;
}
void slippi_tobj_selected(void* object,void* info){
    /* Actual Alloc/LoadDesc selections enumerate custom paths even if their
     * virtual Load never calls the canonical loaded hook. No mint here. */
    if(info!=(void*)&hsdTObj||!find(object)){fail(OWNER_CUSTOM);return;}
}
void slippi_tobj_loaded(void* object){
    TObjOwner* e=find(object);
    if(!e||first_failure||active_token){fail(OWNER_LOAD);return;}
    const uint32_t coord=e->object->flags&TEX_COORD_MASK;
    if(coord==TEX_COORD_SHADOW){
        /* Descriptor loads copy authored flags before this callback. Only a
         * newly constructed owner may acquire the named Shadow role here;
         * an already named Shadow may retain it across a canonical reload.
         * Neither case enters the private matrix/dirty cache. */
        if(e->role==ROLE_PENDING){e->role=ROLE_SHADOW;event();return;}
        if(e->role==ROLE_SHADOW){event();return;}
        e->poisoned=OWNER_ROLE;fail_at(OWNER_ROLE,(uint32_t)__LINE__,coord);event();return;
    }
    if(coord==TEX_COORD_UV||coord==TEX_COORD_REFLECTION||coord==TEX_COORD_TOON){
        if(e->role==ROLE_PENDING)memcpy(e->cache.matrix,e->object->mtx,sizeof(Mtx));
        else if(e->role!=ROLE_MANAGED){
            e->poisoned=OWNER_ROLE;fail_at(OWNER_ROLE,(uint32_t)__LINE__,coord);event();return;
        }
        e->role=ROLE_MANAGED;e->cache.dirty=TEX_MTX_DIRTY;event();return;
    }
    e->poisoned=OWNER_ROLE;fail_at(OWNER_ROLE,(uint32_t)__LINE__,coord);event();
}
void slippi_tobj_shadow(void* object){
    TObjOwner* e=find(object);
    if(!e||first_failure||active_token||e->role!=ROLE_PENDING){fail(OWNER_ROLE);return;}
    /* This hook is only at actual makeShadowTObj after its original complete
     * source flag assignment. Role does not authorize any cache/restore. */
    if((e->object->flags&TEX_COORD_MASK)!=TEX_COORD_SHADOW){fail(OWNER_ROLE);return;}
    e->role=ROLE_SHADOW;event();
}
void slippi_tobj_unsupported_write(void* object){(void)object;fail(OWNER_ROLE);event();}
int slippi_tobj_managed_require(void* object){
    TObjOwner* e=require_owner(object);
    if(e->role==ROLE_SHADOW)return 0;
    if(!admissible(e))panic_owner(OWNER_ROLE);
    return 1;
}
MtxPtr slippi_tobj_matrix_require(void* object){
    TObjOwner* e=require_owner(object);
    if(e->role!=ROLE_MANAGED||!admissible(e))panic_owner(OWNER_ROLE);
    return e->cache.matrix;
}
uint32_t slippi_tobj_effective_flags(void* object){
    TObjOwner* e=require_owner(object);
    if(e->role!=ROLE_MANAGED||!admissible(e))panic_owner(OWNER_ROLE);
    return (e->object->flags&~TEX_MTX_DIRTY)|e->cache.dirty;
}
void slippi_tobj_authored_dirty(void* object){
    TObjOwner* e=require_owner(object);
    if(e->role==ROLE_SHADOW)return; /* Entire original dirty update retained. */
    if(!admissible(e))panic_owner(OWNER_ROLE);
    e->cache.dirty=TEX_MTX_DIRTY;
}
void slippi_tobj_consumed(void* object){
    TObjOwner* e=require_owner(object);
    if(e->role!=ROLE_MANAGED||!admissible(e))panic_owner(OWNER_ROLE);
    e->cache.dirty=0;
}
SlippiTObjAnchor slippi_tobj_anchor_capture(void){
    SlippiTObjAnchor a={0};
    if(!owner_world||!owner_count||active_token||first_failure||!descriptor_valid()||interval_serial==UINT64_MAX){fail(OWNER_INTERVAL);return a;}
    for(TObjOwner* e=owners;e;e=e->next)if(!admissible(e)){fail(OWNER_LIFETIME);return a;}
    active_token=++interval_serial;
    a.world=owner_world;a.serial=owner_serial;a.token=active_token;a.count=owner_count;return a;
}
int slippi_tobj_anchor_admit(SlippiTObjAnchor a){
    if(first_failure||!a.token||a.token!=active_token||a.world!=owner_world||
       a.serial!=owner_serial||a.count!=owner_count||a.reserved||!descriptor_valid())return 0;
    for(TObjOwner* e=owners;e;e=e->next)if(!admissible(e))return 0;
    return 1;
}
void slippi_tobj_anchor_disarm(SlippiTObjAnchor a){if(a.token&&a.token==active_token)active_token=0;}
uint32_t slippi_tobj_failure(void){return first_failure;}
