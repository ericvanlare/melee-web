/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "lobj_presentation_cache.h"
#include <sysdolphin/baselib/class.h>
#include <sysdolphin/baselib/debug.h>
#include <stdlib.h>
#include <string.h>
extern HSD_LObjInfo hsdLObj;
typedef struct LObjOwner {
    HSD_LObj* object;
    HSD_ClassInfo* descriptor;
    uint64_t generation, world;
    uint32_t poisoned;
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
enum { OWNER_MISSING=1, OWNER_DESCRIPTOR=2, OWNER_OOM=3, OWNER_WRAP=4,
       OWNER_LIFETIME=5, OWNER_REBIND=6, OWNER_AMNESIA=7, OWNER_INTERVAL=8,
       OWNER_DIRTY_SETTER=9, OWNER_WORLD=10 };
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
static void panic_owner(uint32_t why){
    fail(why);HSD_Panic(__FILE__,__LINE__,"Unsupported Slippi LObj presentation owner");
    abort(); /* A provider returning from panic cannot become silent success. */
}
int slippi_lobj_world_begin(uint64_t world){
    if(!world||owner_world||owners||active_token){fail(OWNER_WORLD);return 0;}
    owner_world=world;first_failure=0;event();return first_failure==0;
}
void slippi_lobj_world_end(uint64_t world){
    if(!world||world!=owner_world){fail(OWNER_WORLD);return;}
    if(active_token)fail(OWNER_INTERVAL);
    active_token=0;event();
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
        if(active_token)fail(OWNER_LIFETIME);
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
    a.world=owner_world;a.serial=owner_serial;a.token=active_token;a.count=owner_count;return a;
}
int slippi_lobj_anchor_admit(SlippiLObjAnchor a){
    if(first_failure||!a.token||a.token!=active_token||a.world!=owner_world||
       a.serial!=owner_serial||a.count!=owner_count||a.reserved||!descriptor_valid())return 0;
    for(LObjOwner* e=owners;e;e=e->next)if(!find(e->object))return 0;
    return 1;
}
void slippi_lobj_anchor_disarm(SlippiLObjAnchor a){if(a.token&&a.token==active_token)active_token=0;}
uint32_t slippi_lobj_failure(void){return first_failure;}
