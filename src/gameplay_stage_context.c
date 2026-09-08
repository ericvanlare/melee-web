#include "gameplay_stage_context.h"
#include "gameplay_bootstrap.h"
#include "hsd_native_joint.h"
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/sc/types.h>
#include <melee/lb/lbspdisplay.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/wobj.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct NativeLight { HSD_LightDesc desc; HSD_WObjDesc position, interest; float shininess; LightList list; uint8_t override_ready, override_found, override_flags; } NativeLight;
struct MeleeWebStageLights {
    NativeLight* lights; LightList** list; LightList** saved;
    HSD_GObj* owner; uint32_t count; uint64_t generation; int attached;
};
static MeleeWebStageLights* published;
static int fail(char* e,size_t n,const char* s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
static void removed(void* p){((MeleeWebStageLights*)p)->owner=NULL;}
MeleeWebStageLights* melee_web_stage_lights_create(const MeleeWebStageLightDesc* in,uint32_t count,char* e,size_t n){
    if(!in||!count||count>64){fail(e,n,"Stage lights require 1..64 descriptors");return NULL;}
    for(uint32_t i=0;i<count;i++){
        if((in[i].flags&3)>1||((in[i].flags&3)==1&&!in[i].has_position)){
            fail(e,n,"Unsupported native stage light type or missing position");return NULL;
        }
        for(int j=0;j<3;j++)if(!isfinite(in[i].position[j])||!isfinite(in[i].interest[j])){
            fail(e,n,"Nonfinite native stage light position");return NULL;
        }
        if(!isfinite(in[i].shininess)){fail(e,n,"Nonfinite native stage light shininess");return NULL;}
    }
    MeleeWebStageLights* h=calloc(1,sizeof(*h));
    if(!h){fail(e,n,"Cannot allocate stage light context");return NULL;}
    h->lights=calloc(count,sizeof(*h->lights));h->list=calloc(count+1,sizeof(*h->list));h->count=count;
    if(!h->lights||!h->list){free(h->lights);free(h->list);free(h);fail(e,n,"Cannot allocate stage light descriptors");return NULL;}
    for(uint32_t i=0;i<count;i++){
        NativeLight* l=&h->lights[i];const MeleeWebStageLightDesc* d=&in[i];
        l->desc.flags=d->flags;l->desc.attnflags=d->attenuation_flags;
        memcpy(&l->desc.color,d->color,4);
        memcpy(&l->position.pos,d->position,12);memcpy(&l->interest.pos,d->interest,12);
        if(d->has_position)l->desc.position=&l->position;
        if(d->has_interest)l->desc.interest=&l->interest;
        l->shininess=d->shininess;if(d->has_shininess)l->desc.u.shininess=&l->shininess;
        l->list.desc=&l->desc;h->list[i]=&l->list;
    }
    ok(e,n);return h;
}
int melee_web_stage_lights_set_override(MeleeWebStageLights* h,uint32_t index,int found,uint8_t flags,char* e,size_t n) {
    if(!h||h->attached||index>=h->count||(found!=0&&found!=1)||(flags&~0xe0)||(!found&&flags))
        return fail(e,n,"Invalid or already published stage light override");
    NativeLight* l=&h->lights[index];l->override_ready=1;l->override_found=found;l->override_flags=flags;
    return ok(e,n);
}
int melee_web_stage_lights_lookup_override(void* descriptor,int* found,uint8_t* flags) {
    if(!published)return 0;
    for(uint32_t i=0;i<published->count;i++) {
        NativeLight* l=&published->lights[i];
        if(descriptor==&l->desc) {
            if(!l->override_ready||!found||!flags) {
                fputs("Owned stage light override was not decoded before original lookup\n",stderr);abort();
            }
            *found=l->override_found;*flags=l->override_flags;return 1;
        }
    }
    return 0;
}
void* melee_web_stage_lights_descriptors(MeleeWebStageLights* h){return h?h->list:NULL;}
int melee_web_stage_lights_load(MeleeWebStageLights* h,char* e,size_t n){
    if(!h||h->owner)return fail(e,n,"Stage lights missing or already loaded");
    if(!melee_web_native_world_enable(e,n))return 0;
    HSD_GObj* owner=GObj_Create(12,3,0);
    if(!owner)return fail(e,n,"Cannot allocate stage light GObj");
    HSD_LObj* l=lb_80011AC4(h->list);
    if(!l){HSD_GObjPLink_80390228(owner);return fail(e,n,"Original light loader returned no lights");}
    h->owner=owner;h->generation=melee_web_gameplay_stats().generation;
    HSD_GObjObject_80390A70(owner,HSD_GObj_LightKind,l);GObj_InitUserData(owner,0,removed,h);
    return ok(e,n);
}
int melee_web_stage_lights_stats(MeleeWebStageLights* h,uint32_t* count,uint16_t* flags,uint8_t* rgba,uint32_t capacity,char* e,size_t n){
    if(!h||!h->owner||h->generation!=melee_web_gameplay_stats().generation)return fail(e,n,"Stage lights are not in their live owned world");
    if(!count||!flags||!rgba||capacity<h->count)return fail(e,n,"Stage light stats capacity is insufficient");
    *count=0;
    for(HSD_LObj* l=h->owner->hsd_obj;l;l=l->next){
        if(*count>=capacity)return fail(e,n,"Original light chain exceeds capacity");
        flags[*count]=l->flags;memcpy(rgba+4*(*count),&l->color,4);++*count;
    }
    return ok(e,n);
}
int melee_web_stage_lights_attach(MeleeWebStageLights* h,char* e,size_t n){
    if(!h||h->attached||published)return fail(e,n,"Stage light publication already owned or context missing");
    h->saved=stage_info.map_plit;stage_info.map_plit=h->list;h->attached=1;published=h;return ok(e,n);
}
int melee_web_stage_lights_detach(MeleeWebStageLights* h,char* e,size_t n){
    if(!h)return fail(e,n,"Stage light context missing");
    if(h->attached){
        if(published!=h||stage_info.map_plit!=h->list)return fail(e,n,"Stage light publication was replaced by another owner");
        stage_info.map_plit=h->saved;h->saved=NULL;h->attached=0;published=NULL;
    }
    return ok(e,n);
}
int melee_web_stage_lights_destroy(MeleeWebStageLights* h,char* e,size_t n){
    if(!h)return ok(e,n);
    if(h->owner&&(h->generation!=melee_web_gameplay_stats().generation||h->owner==HSD_GObj_804D781C))return fail(e,n,"Stage light teardown requires its live world outside its callback");
    if(!melee_web_stage_lights_detach(h,e,n))return 0;
    if(h->owner)HSD_GObjPLink_80390228(h->owner);
    free(h->lights);free(h->list);free(h);return ok(e,n);
}
