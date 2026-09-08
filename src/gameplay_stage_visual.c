#include "gameplay_stage_visual.h"
#include "gameplay_bootstrap.h"
#include <melee/gr/ground.h>
#include <melee/gr/grdisplay.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/jobj.h>
#include <stdio.h>
#include <stdlib.h>
struct MeleeWebStageVisual { HSD_GObj* gobj; uint64_t generation; };
static int fail(char* e,size_t n,const char* m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
static int live(MeleeWebStageVisual* h,char* e,size_t n){
    if(!h||!h->gobj||h->generation!=melee_web_gameplay_stats().generation)
        return fail(e,n,"Stage visual requires its live source world");
    return 1;
}
MeleeWebStageVisual* melee_web_stage_visual_begin(MeleeWebNativeJoint* model,
    void* joint_animation,void* material_animation,int map_id,unsigned camera_pass,char* e,size_t n)
{
    if(map_id<0||map_id>=256||camera_pass>2||!stage_info.param){
        fail(e,n,"Stage visual requires valid map/pass and published Ground parameters");return NULL;
    }
    HSD_Joint* descriptor=melee_web_native_joint_descriptor(model,e,n);
    if(!descriptor||!melee_web_native_world_enable(e,n))return NULL;
    MeleeWebStageVisual* h=calloc(1,sizeof(*h));
    if(!h){fail(e,n,"Cannot allocate stage visual owner");return NULL;}
    h->gobj=Ground_801C1A20(descriptor,map_id);
    if(!h->gobj){free(h);fail(e,n,"Original Ground visual construction failed");return NULL;}
    h->generation=melee_web_gameplay_stats().generation;
    Ground* gp=h->gobj->user_data;gp->x11_flags.b012=camera_pass;
    HSD_JObj* wrapper=h->gobj->hsd_obj;
    HSD_JObj* root=HSD_JObjGetChild(wrapper);
    if(!root){melee_web_stage_visual_end(h,NULL,0);fail(e,n,"Original Ground wrapper has no model child");return NULL;}
    HSD_JObjAddAnimAll(root,joint_animation,material_animation,NULL);
    HSD_JObjReqAnimAll(root,0);HSD_JObjAnimAll(root);
    GObj_SetupGXLink(h->gobj,grDisplay_801C5DB0,3,0);
    ok(e,n);return h;
}
void* melee_web_stage_visual_object(MeleeWebStageVisual* h,char* e,size_t n){
    if(!live(h,e,n))return NULL;ok(e,n);return h->gobj;
}
int melee_web_stage_visual_end(MeleeWebStageVisual* h,char* e,size_t n){
    if(!h)return ok(e,n);
    if(!live(h,e,n))return 0;
    if(h->gobj==HSD_GObj_804D781C||h->gobj==HSD_GObj_804D7814)
        return fail(e,n,"Stage visual destruction is deferred inside its callback");
    HSD_GObjPLink_80390228(h->gobj);h->gobj=NULL;free(h);return ok(e,n);
}
