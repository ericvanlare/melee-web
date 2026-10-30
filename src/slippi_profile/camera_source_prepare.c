/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "camera_source_prepare.h"
#include "gameplay_cpu_interval.h"
#include "gameplay_source_memory_runtime.h"
#include <melee/cm/camera.h>
#include <melee/cm/forward.h>
#include <melee/gr/forward.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/wobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/objalloc.h>
#include <stdio.h>
extern HSD_CObjInfo hsdCObj;
extern HSD_ObjAllocData gobj_alloc_data;
extern void slippi_particle_source_main_camera_identity(HSD_GObj**, GObj_RenderFunc*,
                                                       HSD_GObjEvent*, unsigned*);
/* Defined beside the actual static owners; this is not a caller-owned camera. */
extern void slippi_camera_source_identity(HSD_CObj**, int*, int*);

static int refuse(char* error, size_t size, const char* reason)
{ if (error && size) snprintf(error, size, "%s", reason); return 0; }
static int plain_wobj(const HSD_WObj* object)
{
    return object && object->parent.parent.class_info == &hsdWObj.parent.parent &&
        !(object->flags & 1u) && !object->aobj && !object->robj;
}
static int plain_camera(const HSD_CObj* camera)
{
    return camera && camera->parent.parent.class_info == &hsdCObj.parent.parent &&
        !(camera->flags & 2u) && camera->projection_type == PROJ_PERSPECTIVE &&
        plain_wobj(camera->eyepos) && plain_wobj(camera->interest) &&
        camera->eyepos != camera->interest;
}
static int selected_main(HSD_GObj* main)
{
    HSD_GObj* node; HSD_GObj* previous = NULL;
    size_t count = 0, matches = 0, bound = gobj_alloc_data.used;
    const unsigned link = HSD_GObjLibInitData.gx_link_max + 1u;
    if (!bound || link != 64u || !HSD_GObjGXLinkHead || !HSD_GObj_804D7820) return 0;
    for (node = HSD_GObjGXLinkHead[link]; node; node = node->next_gx) {
        if (count++ >= bound || node->gx_link != link || node->prev_gx != previous) return 0;
        if (node == main) ++matches;
        previous = node;
    }
    return matches == 1 && previous == HSD_GObj_804D7820[link];
}
int slippi_camera_source_prepare(uint64_t world, uint64_t token,
                                 char* error, size_t error_size)
{
    HSD_GObj* main = NULL; HSD_CObj* copy = NULL; HSD_CObj* camera;
    GObj_RenderFunc render = NULL; HSD_GObjEvent process = NULL;
    unsigned suppressed = 0; int mode = -1, stage = -1;
    MeleeWebSourceMemoryContext context = {0};
    MeleeWebSourceMemoryAllocation inverse = {0};
    /* Warm source/draw order and its first-use inverse allocation stay original.
     * Only an explicitly armed private CPU interval requests this preparation. */
    if (!token && !melee_web_cpu_interval_active()) return 1;
    if (!world || !melee_web_cpu_interval_admit(token) ||
        melee_web_cpu_interval_token() != token ||
        melee_web_source_memory_context_read(&context) != MELEE_WEB_SOURCE_MEMORY_READ_OK ||
        context.world_generation != world)
        return refuse(error, error_size, "Camera history requires its healthy exact CPU interval");
    slippi_particle_source_main_camera_identity(&main, &render, &process, &suppressed);
    slippi_camera_source_identity(&copy, &mode, &stage);
    if (mode != CAMERA_STANDARD || stage != Gr_Kind_Last || suppressed || !main ||
        !selected_main(main) || main->classifier != 0x10 || main->p_link != 0x12 ||
        main->p_priority != 0 || main->render_priority != 2 ||
        main->render_cb != render || !render || !process ||
        main->obj_kind != HSD_GObj_CameraKind || !main->proc ||
        main->proc->gobj != main || main->proc->s_link != 0x12 ||
        main->proc->on_invoke != process || main->proc->child)
        return refuse(error, error_size, "Camera history refuses unknown main camera mode or dispatch");
    camera = main->hsd_obj;
    if (!plain_camera(camera) || !plain_camera(copy) || camera == copy ||
        camera->eyepos == copy->eyepos || camera->eyepos == copy->interest ||
        camera->interest == copy->eyepos || camera->interest == copy->interest ||
        !camera->proj_mtx || copy->proj_mtx ||
        melee_web_source_memory_allocation_read(camera->proj_mtx, &inverse) !=
            MELEE_WEB_SOURCE_MEMORY_READ_OK || !inverse.live ||
        inverse.world_generation != world || inverse.requested_bytes != sizeof(Mtx))
        return refuse(error, error_size, "Camera history requires original plain cameras and existing inverse ownership");
    /* All fallible admission is complete. Reuse the original CPU calculations;
     * no render callback, GX call, allocation, or camera pointer replacement.
     * Copy world inputs are source-owned too, but its untouched view/inverse
     * caches are not eagerly materialized. */
    Camera_8002A4AC(main);
    HSD_CObjSetupViewingMtx(camera);
    (void) HSD_CObjGetInvViewingMtxPtrDirect(camera);
    if (error && error_size) error[0] = '\0';
    return 1;
}
