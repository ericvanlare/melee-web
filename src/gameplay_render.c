#include "gameplay_render.h"
#include "gameplay_bootstrap.h"
#include "gameplay_stage_context.h"
#include <melee/cm/camera.h>
#include "hsd_native_joint.h"
#include <melee/cm/types.h>
#include <melee/pl/player.h>
#include <melee/gr/ground.h>
#include <melee/ft/types.h>
#include <melee/ft/ftlib.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/displayfunc.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/state.h>
#include <math.h>
#include <emscripten.h>
#define BOUNDARY(s) EM_ASM({window.runtimeBoundary=UTF8ToString($0);},s)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void* melee_web_camera_state(void);
extern HSD_CObj* cm_804D6464;
extern HSD_ObjAllocData zlist_alloc_data;
extern int melee_web_native_camera_restore_current(void*,void*);
struct MeleeWebRender {
    uint64_t generation;
    HSD_GObj* gobj;
    HSD_CObj* camera;
    HSD_GObj* view_owner;
    HSD_CObj* view_camera;
    int original_controller;
    int match_passes;
    int scene_cameras;
    MeleeWebRenderSettings settings;
    int drawing;
};
static MeleeWebRender* owner;
static uint64_t zlist_generation;
static int fail(char* e,size_t n,const char* m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
static int valid(const MeleeWebRenderSettings* s,char* e,size_t n)
{
    if(!s||!s->width||!s->height||s->width>4096||s->height>4096||!s->gx_links||
       !isfinite(s->vertical_fov)||s->vertical_fov<=1||s->vertical_fov>=179||
       !isfinite(s->near_plane)||!isfinite(s->far_plane)||s->near_plane<=0||s->far_plane<=s->near_plane)
        return fail(e,n,"Invalid native render viewport/projection settings");
    float distance=0,horizontal=0;
    for(unsigned i=0;i<3;i++){
        if(!isfinite(s->eye[i])||!isfinite(s->interest[i])||fabsf(s->eye[i])>100000||fabsf(s->interest[i])>100000)
            return fail(e,n,"Native render camera position is invalid");
        float d=s->eye[i]-s->interest[i];distance+=d*d;if(i!=1)horizontal+=d*d;
    }
    if(distance<1e-6f||horizontal<1e-6f)return fail(e,n,"Native camera needs a nonsingular Y-up view");
    return 1;
}
static int live(MeleeWebRender* h,char* e,size_t n)
{
    if(!h||h!=owner||h->generation!=melee_web_gameplay_generation()||
       cm_804D6464!=h->view_camera||((Camera*)melee_web_camera_state())->gobj!=h->gobj)
        return fail(e,n,"Native render camera ownership changed");
    return 1;
}
int melee_web_render_update(MeleeWebRender* h,const MeleeWebRenderSettings* s,char* e,size_t n)
{
    if(!live(h,e,n)||!valid(s,e,n))return 0;
    if(h->drawing)return fail(e,n,"Cannot update native camera during drawing");
    if(h->original_controller)return fail(e,n,"Original match controller owns camera transforms");
    if(HSD_GObjLibInitData.gx_link_max<63&&(s->gx_links>>(HSD_GObjLibInitData.gx_link_max+1)))
        return fail(e,n,"Render GX links exceed the owned source registry");
    HSD_CObjSetViewportfx4(h->camera,0,(float)s->width,0,(float)s->height);
    HSD_CObjSetScissorx4(h->camera,0,s->width,0,s->height);
    Vec3 eye={s->eye[0],s->eye[1],s->eye[2]},interest={s->interest[0],s->interest[1],s->interest[2]};
    HSD_CObjSetEyePosition(h->camera,&eye);HSD_CObjSetInterest(h->camera,&interest);
    HSD_CObjSetRoll(h->camera,0);HSD_CObjSetNear(h->camera,s->near_plane);HSD_CObjSetFar(h->camera,s->far_plane);
    HSD_CObjSetPerspective(h->camera,s->vertical_fov,(float)s->width/(float)s->height);
    h->gobj->gxlink_prios=s->gx_links;h->settings=*s;
    return ok(e,n);
}
static MeleeWebRender* begin_render(const MeleeWebRenderSettings* s,int original,char* e,size_t n)
{
    if(!valid(s,e,n))return NULL;
    Camera* source=melee_web_camera_state();
    if(owner||source->gobj||cm_804D6464){fail(e,n,"Original camera storage already has an owner");return NULL;}
    if(!melee_web_native_world_enable(e,n))return NULL;
    uint64_t generation=melee_web_gameplay_generation();
    if(zlist_generation!=generation){
        if(zlist_alloc_data.used){fail(e,n,"Native render cannot replace an existing Z-list pool");return NULL;}
        HSD_ZListInitAllocData();zlist_generation=generation;
    }
    MeleeWebRender* h=calloc(1,sizeof(*h));if(!h){fail(e,n,"Cannot allocate native render owner");return NULL;}
    h->generation=generation;
    if(original){
        Camera_80030688();
        h->gobj=source->gobj;h->camera=h->gobj->hsd_obj;
        h->view_camera=cm_804D6464;h->original_controller=1;h->settings=*s;
        // Source creates its independent unshaken view camera without a GObj.
        // Give it the same registered camera destructor as the primary CObj.
        h->view_owner=GObj_Create(HSD_GOBJ_CLASS_CAMERA,0,0);
        HSD_GObjObject_80390A70(h->view_owner,HSD_GObj_CameraKind,h->view_camera);
        HSD_CObjSetViewportfx4(h->camera,0,s->width,0,s->height);
        HSD_CObjSetScissorx4(h->camera,0,s->width,0,s->height);
        Camera_80030730(Ground_801C20D0());
        Ground_EnableMatchCamera();Camera_8002F3AC();
        owner=h;
    }else{
        h->gobj=GObj_Create(HSD_GOBJ_CLASS_CAMERA,0,0);
        if(!h->gobj){free(h);fail(e,n,"Cannot allocate original camera GObj");return NULL;}
        h->camera=HSD_CObjAlloc();h->view_camera=h->camera;
        HSD_GObjObject_80390A70(h->gobj,HSD_GObj_CameraKind,h->camera);
        source->gobj=h->gobj;cm_804D6464=h->camera;owner=h;
        if(!melee_web_render_update(h,s,e,n)){melee_web_render_end(h,NULL,0);return NULL;}
    }
    ok(e,n);return h;
}
MeleeWebRender* melee_web_render_begin(const MeleeWebRenderSettings* s,char* e,size_t n){return begin_render(s,0,e,n);}
MeleeWebRender* melee_web_render_begin_match(const MeleeWebRenderSettings* s,char* e,size_t n){return begin_render(s,1,e,n);}
int melee_web_render_use_match_passes(MeleeWebRender* h,char* e,size_t n)
{
    if(!live(h,e,n))return 0;
    if(h->drawing||!h->original_controller||!h->gobj->render_cb)
        return fail(e,n,"Complete draw passes require the idle original match camera");
    h->match_passes=1;return ok(e,n);
}
int melee_web_render_use_scene_cameras(MeleeWebRender* h,char* e,size_t n)
{
    if(!live(h,e,n))return 0;
    if(h->drawing||!h->match_passes)
        return fail(e,n,"Scene camera traversal requires original match passes");
    h->scene_cameras=1;return ok(e,n);
}
int melee_web_render_draw(MeleeWebRender* h,char* e,size_t n)
{
    BOUNDARY("render ownership");
    if(!live(h,e,n))return 0;
    if(h->drawing)return fail(e,n,"Native render frame is not reentrant");
    BOUNDARY("select lights");
    if(!melee_web_stage_lights_select_current(e,n))return 0;
    const unsigned old_stage_pass=Camera_8003108C(),old_fighter_pass=Camera_80031060();
    HSD_CObj* previous=HSD_CObjGetCurrent();HSD_RenderPass pass=HSD_GetCurrentRenderPass();
    HSD_GObj* previous_camera_gobj=HSD_GObj_804D7818;
    HSD_GObj* previous_render_gobj=HSD_GObj_804D7814;
    BOUNDARY("VI mode");
    GXRenderModeObj old_mode=*HSD_VIGetRenderMode();
    GXRenderModeObj mode=old_mode;
    mode.fbWidth=mode.viWidth=h->settings.width;
    mode.efbHeight=mode.xfbHeight=mode.viHeight=h->settings.height;
    mode.aa=0;mode.field_rendering=0;*HSD_VIGetRenderMode()=mode;
    h->drawing=1;
    BOUNDARY("start render");
    HSD_StartRender(HSD_RP_SCREEN);
    BOUNDARY("invalidate state");
    HSD_StateInvalidate(HSD_STATE_ALL);
    HSD_StateSetColorUpdate(GX_TRUE);HSD_StateSetAlphaUpdate(GX_FALSE);
    HSD_StateSetDither(GX_FALSE);HSD_GObj_804D7818=h->gobj;
    if(h->original_controller&&!h->match_passes)Camera_8002A4AC(h->gobj);
    BOUNDARY("set camera");
    int accepted;
    if(h->scene_cameras){
        BOUNDARY("original scene camera traversal");
        HSD_GObj_80390FC0();
        accepted=1;
    }else if(h->match_passes){
        BOUNDARY("original match render callback");
        h->gobj->render_cb(h->gobj,0);
        accepted=1;
    }else if((accepted=HSD_CObjSetCurrent(h->camera))){
        BOUNDARY("light setup");
        HSD_LObjSetupInit(h->camera);
        static unsigned diagnostic_count;
        if(diagnostic_count++<1)for(unsigned slot=0;slot<2;slot++){
            HSD_GObj* fighter=Player_GetEntity(slot);
            if(fighter){Fighter* fp=fighter->user_data;fprintf(stderr,"Render slot%u flags=%u hide=%u/%u/%u cull=%u root=%x\n",slot,fp->x21FC_flag.u8,fp->x221F_b3,fp->invisible,fp->x221E_b5,ftLib_80086A8C(fighter),((HSD_JObj*)fighter->hsd_obj)->flags);}
        }
        // Selected-entry bridge follows the source stage layer order. The
        // original match camera still owns reflection/particle/shadow passes.
        if(h->settings.gx_links & (UINT64_C(1)<<3)) {
            h->gobj->gxlink_prios=UINT64_C(1)<<3;
            Camera_800310A0(2);HSD_GObj_80390ED0(h->gobj,7);
            Camera_800310A0(1);HSD_GObj_80390ED0(h->gobj,7);
        }
        Camera_800310A0(0);Camera_80031074(0);
        h->gobj->gxlink_prios=h->settings.gx_links;
        HSD_GObj_80390ED0(h->gobj,7);
        HSD_CObjEndCurrent();
    }
    Camera_800310A0(old_stage_pass);Camera_80031074(old_fighter_pass);
    HSD_GObj_804D7818=previous_camera_gobj;
    /* Original shadow drawing leaves its fighter in the current-render slot.
     * Our outer camera callback scope must restore the caller for teardown. */
    HSD_GObj_804D7814=previous_render_gobj;
    /* Full scene traversal legitimately ends on a HUD camera. Verify that
     * camera belongs to this SDK world's registered camera list before
     * restoring the caller; CObjEndCurrent intentionally retains it. */
    HSD_CObj* final_camera=HSD_CObjGetCurrent();
    int camera_owned=final_camera==h->camera;
    if(h->scene_cameras){
        for(HSD_GObj* camera=HSD_GObjGXLinkHead[HSD_GObjLibInitData.gx_link_max+1];
            camera;camera=camera->next_gx){
            if(camera->obj_kind==HSD_GObj_CameraKind&&camera->hsd_obj==final_camera)
                camera_owned=1;
        }
    }
    int restored=camera_owned&&melee_web_native_camera_restore_current(final_camera,previous);
    *HSD_VIGetRenderMode()=old_mode;HSD_StartRender(pass);
    h->drawing=0;
    if(!accepted)return fail(e,n,"Original HSD camera rejected the render target");
    if(!restored)return fail(e,n,"Source callback changed the current camera without restoration");
    return ok(e,n);
}
int melee_web_render_end(MeleeWebRender* h,char* e,size_t n)
{
    if(!h)return 1;if(!live(h,e,n))return 0;
    if(h->drawing)return fail(e,n,"Cannot release the camera during drawing");
    if(HSD_CObjGetCurrent()==h->camera&&!melee_web_native_camera_restore_current(h->camera,NULL))
        return fail(e,n,"Cannot release the active camera");
    cm_804D6464=NULL;((Camera*)melee_web_camera_state())->gobj=NULL;
    HSD_GObjPLink_80390228(h->gobj);
    if(h->view_owner)HSD_GObjPLink_80390228(h->view_owner);
    owner=NULL;free(h);return ok(e,n);
}
