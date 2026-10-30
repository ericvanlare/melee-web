/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "particle_pass_plan.h"

#include <stdio.h>
#include <stdint.h>
#include <melee/ef/eflib.h>
#include <melee/ef/types.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/tobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>

extern HSD_CObjInfo hsdCObj;

typedef struct {
    SlippiParticleCameraKind kind;
    unsigned gx_mask;
    int known_zero;
} CameraSelection;

static int refuse(char* error, size_t error_size, const char* message)
{
    if (error && error_size) snprintf(error, error_size, "%s", message);
    return 0;
}

static int process_is_link(const HSD_GObj* object, HSD_GObjEvent callback,
                           unsigned s_link)
{
    const HSD_GObjProc* process = object->proc;
    return process && process->gobj == object && process->s_link == s_link &&
           process->on_invoke == callback && process->child == NULL;
}

static int manager_process_is(const HSD_GObj* object,
                              HSD_GObjEvent callback)
{
    return process_is_link(object, callback, 15);
}

static int manager_is(const HSD_GObj* object,
                      const SlippiParticlePassSource* source,
                      unsigned link)
{
    if (object->classifier != HSD_GOBJ_CLASS_EFFECT ||
        object->p_link != (link == SLIPPI_PARTICLE_GX_MODEL ? 11 : 12) ||
        object->p_priority != 1 || object->gx_link != link ||
        object->render_priority != 2 ||
        object->render_cb != source->particle_callback ||
        object->obj_kind != HSD_GOBJ_OBJ_NONE || object->hsd_obj != NULL ||
        object->user_data_kind != HSD_GOBJ_USER_DATA_NONE ||
        object->user_data != NULL || object->user_data_remove_func != NULL)
        return 0;
    return manager_process_is(object, link == SLIPPI_PARTICLE_GX_MODEL
                                          ? source->model_manager_process
                                          : source->aux_manager_process);
}

static int class_is(const HSD_Class* object, const HSD_ClassInfo* expected)
{
    return object && object->class_info == expected;
}

static int acyclic_siblings(const HSD_JObj* first, const HSD_JObj* parent,
                            size_t bound, char* error, size_t error_size)
{
    const HSD_JObj* slow = first;
    const HSD_JObj* fast = first;
    const HSD_JObj* node;
    size_t count = 0;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast)
            return refuse(error, error_size,
                          "Effect JObj sibling chain contains a cycle");
    }
    for (node = first; node; node = node->next) {
        if (count++ >= bound || node->parent != parent)
            return refuse(error, error_size,
                          "Effect JObj child topology exceeds its live class bound");
    }
    return 1;
}

static int acyclic_next_chain(const void* first,
                              const void* (*next_of)(const void*),
                              size_t bound, const char* label,
                              char* error, size_t error_size)
{
    const void* slow = first;
    const void* fast = first;
    const void* node;
    size_t count = 0;
    while (fast) {
        fast = next_of(fast);
        if (!fast) break;
        fast = next_of(fast);
        slow = next_of(slow);
        if (slow == fast)
            return refuse(error, error_size,
                          "Effect display subobject chain contains a cycle");
    }
    for (node = first; node; node = next_of(node)) {
        if (count++ >= bound) {
            if (error && error_size)
                snprintf(error, error_size,
                         "Effect %s chain exceeded its live class bound", label);
            return 0;
        }
    }
    return 1;
}

static const void* dobj_next(const void* value)
{ return ((const HSD_DObj*) value)->next; }
static const void* tobj_next(const void* value)
{ return ((const HSD_TObj*) value)->next; }
static const void* pobj_next(const void* value)
{ return ((const HSD_PObj*) value)->next; }

static int effect_display_objects_safe(const HSD_GObj* owner,
                                       char* error, size_t error_size)
{
    const HSD_JObj* root = (const HSD_JObj*) owner->hsd_obj;
    const HSD_JObj* node;
    const size_t jobj_bound = HSD_CLASS_INFO(&hsdJObj)->head.nb_exist;
    const size_t dobj_bound = HSD_CLASS_INFO(&hsdDObj)->head.nb_exist;
    const size_t pobj_bound = HSD_CLASS_INFO(&hsdPObj)->head.nb_exist;
    const size_t tobj_bound = HSD_CLASS_INFO(&hsdTObj)->head.nb_exist;
    size_t nodes = 0;

    if (!root || !jobj_bound)
        return refuse(error, error_size,
                      "Owned effect has no bounded source JObj tree");

    /* HSD_GObj_JObjCallback reaches HSD_JObjDispAll. Admit only the concrete
     * base classes whose source display methods are the registered originals;
     * custom class dispatch is unknown behavior. Exact class identity is the
     * default-class proof for every node and display subobject. */
    for (node = root; node;) {
        const HSD_DObj* dobj;
        if (nodes++ >= jobj_bound ||
            !class_is((const HSD_Class*) node, HSD_CLASS_INFO(&hsdJObj)))
            return refuse(error, error_size,
                          "Owned effect uses an unbounded or custom JObj class");
        if (node->flags & JOBJ_PTCL)
            return refuse(error, error_size,
                          "Owned effect contains an active S/D particle callback node");
        if (node->flags & JOBJ_INSTANCE)
            return refuse(error, error_size,
                          "Owned effect uses an external JObj instance during display");
        if (node->child &&
            !acyclic_siblings(node->child, node, jobj_bound, error,
                              error_size))
            return 0;

        if (!(node->flags & (JOBJ_PTCL | JOBJ_SPLINE))) {
            dobj = node->u.dobj;
            if (!acyclic_next_chain(dobj, dobj_next,
                                    dobj_bound, "DObj", error, error_size))
                return 0;
            for (; dobj; dobj = dobj->next) {
                const HSD_MObj* mobj = dobj->mobj;
                const HSD_PObj* pobj = dobj->pobj;
                if (!class_is((const HSD_Class*) dobj,
                              HSD_CLASS_INFO(&hsdDObj)))
                    return refuse(error, error_size,
                                  "Owned effect uses a custom DObj display class");
                if (mobj && !class_is((const HSD_Class*) mobj,
                                      HSD_CLASS_INFO(&hsdMObj)))
                    return refuse(error, error_size,
                                  "Owned effect uses a custom MObj display class");
                if (mobj && !acyclic_next_chain(mobj->tobj, tobj_next,
                                                tobj_bound, "TObj", error,
                                                error_size))
                    return 0;
                if (mobj) {
                    const HSD_TObj* tobj;
                    for (tobj = mobj->tobj; tobj; tobj = tobj->next) {
                        if (HSD_OBJECT_METHOD(tobj) != HSD_CLASS_INFO(&hsdTObj))
                            return refuse(error, error_size,
                                          "Owned effect uses a custom TObj class");
                    }
                }
                if (!acyclic_next_chain(pobj, pobj_next,
                                        pobj_bound, "PObj", error,
                                        error_size))
                    return 0;
                for (; pobj; pobj = pobj->next) {
                    if (!class_is((const HSD_Class*) pobj,
                                  HSD_CLASS_INFO(&hsdPObj)))
                        return refuse(error, error_size,
                                      "Owned effect uses a custom PObj display class");
                }
            }
        }

        if (node->child) {
            node = node->child;
        } else {
            while (node != root && !node->next) node = node->parent;
            if (node == root) break;
            node = node->next;
        }
    }
    return 1;
}

int slippi_profile_particle_pass_jobj_display_safe(
    const HSD_GObj* owner, char* error, size_t error_size)
{
    if (!owner || owner->obj_kind != HSD_GObj_JObjKind || !owner->hsd_obj)
        return refuse(error, error_size,
                      "Registered GX11 display owner is not a JObj GObj");
    return effect_display_objects_safe(owner, error, error_size);
}

static int effect_owner_is(const HSD_GObj* object, unsigned gx_link)
{
    const EF_Effect* effect;
    if (gx_link != SLIPPI_PARTICLE_GX_MODEL ||
        object->classifier != HSD_GOBJ_CLASS_EFFECT ||
        (object->p_link != 11 && object->p_link != 12) ||
        object->p_priority != 0 ||
        object->gx_link != SLIPPI_PARTICLE_GX_MODEL ||
        object->render_priority != 1 ||
        object->render_cb != HSD_GObj_JObjCallback ||
        object->obj_kind != HSD_GObj_JObjKind || !object->hsd_obj ||
        object->user_data_kind != 8 ||
        object->user_data_remove_func != efLib_remove_user_data ||
        !object->user_data ||
        !process_is_link(object, efLib_Update, 15))
        return 0;
    effect = (const EF_Effect*) object->user_data;
    return effect->gobj == object;
}

static int select_camera(const HSD_GObj* camera,
                         const SlippiParticlePassSource* source,
                         CameraSelection* selection, char* error,
                         size_t error_size)
{
    if (camera == source->main_camera && source->main_camera &&
        camera->render_cb == source->main_callback && source->main_callback &&
        camera->hsd_obj == source->main_cobj && source->main_cobj) {
        if (!source->main_process_callback ||
            !process_is_link(camera, source->main_process_callback, 0x12))
            return refuse(error, error_size,
                          "Main camera process identity is unsupported");
        if (!source->screen_render_pass)
            return refuse(error, error_size,
                          "Main camera is not on the owned SCREEN render pass");
        selection->kind = SLIPPI_PARTICLE_CAMERA_MAIN_TAIL;
        selection->gx_mask = source->main_x398_b3
                                 ? 0
                                 : SLIPPI_PARTICLE_GX_MODEL_MASK;
        selection->known_zero = 0;
        return 1;
    }
    if (camera == source->hud_camera && source->hud_camera &&
        camera->render_cb == source->hud_callback && source->hud_callback &&
        camera->hsd_obj == source->hud_cobj && source->hud_cobj) {
        if (!source->screen_render_pass)
            return refuse(error, error_size,
                          "HUD camera is not on the owned SCREEN render pass");
        if (camera->gxlink_prios != 0xD00)
            return refuse(error, error_size,
                          "HUD camera mask differs from source GX8/10/11 schedule");
        selection->kind = SLIPPI_PARTICLE_CAMERA_HUD;
        selection->gx_mask = source->hud_hidden
                                 ? 0
                                 : SLIPPI_PARTICLE_GX_AUX_MASK;
        selection->known_zero = source->hud_hidden != 0;
        return 1;
    }
    if (camera == source->devtext_camera && source->devtext_camera &&
        camera->render_cb == HSD_GObj_803910D8 &&
        camera->hsd_obj == source->devtext_cobj && source->devtext_cobj) {
        if (camera->classifier != 0x15 || camera->p_link != 24 ||
            camera->p_priority != 0 ||
            camera->gx_link != SLIPPI_PARTICLE_GX_LINK_MAX + 1 ||
            camera->render_priority != 11 || camera->gxlink_prios != 0x20000)
            return refuse(error, error_size,
                          "DevText camera differs from exact source registration");
        selection->kind = SLIPPI_PARTICLE_CAMERA_DEVTEXT_ZERO;
        selection->gx_mask = 0;
        selection->known_zero = 1;
        return 1;
    }
    if (camera == source->ifcoget_camera && source->ifcoget_camera &&
        camera->render_cb == source->ifcoget_callback &&
        source->ifcoget_callback == HSD_GObj_803910D8 &&
        camera->hsd_obj == source->ifcoget_cobj && source->ifcoget_cobj) {
        if (camera->classifier != HSD_GOBJ_CLASS_CAMERA ||
            camera->p_link != 21 || camera->p_priority != 0 ||
            camera->gx_link != SLIPPI_PARTICLE_GX_LINK_MAX + 1 ||
            camera->render_priority != 9 || camera->gxlink_prios != 0x8400)
            return refuse(error, error_size,
                          "IfCoGet camera differs from exact source registration");
        selection->kind = SLIPPI_PARTICLE_CAMERA_IFCOGET_ZERO;
        selection->gx_mask = 0;
        selection->known_zero = 1;
        return 1;
    }
    if (camera == source->nametag_camera && source->nametag_camera &&
        camera->render_cb == source->nametag_callback &&
        source->nametag_callback && camera->hsd_obj == source->nametag_cobj &&
        source->nametag_cobj) {
        if (camera->gxlink_prios != 0x200)
            return refuse(error, error_size,
                          "Nametag camera mask differs from source GX9 schedule");
        selection->kind = SLIPPI_PARTICLE_CAMERA_NAMETAG_ZERO;
        selection->gx_mask = 0;
        selection->known_zero = 1;
        return 1;
    }
    if (camera == source->background_flash_camera &&
        source->background_flash_camera &&
        camera->render_cb == source->background_flash_callback &&
        source->background_flash_callback &&
        camera->hsd_obj == source->background_flash_cobj &&
        source->background_flash_cobj) {
        if (camera->gxlink_prios != 0x10000)
            return refuse(error, error_size,
                          "Background flash camera mask differs from source GX16 schedule");
        selection->kind = SLIPPI_PARTICLE_CAMERA_BACKGROUND_FLASH;
        selection->gx_mask = 0;
        selection->known_zero = 1;
        return 1;
    }
    if (camera == source->ground_camera && source->ground_camera &&
        camera->render_cb == source->ground_callback &&
        source->ground_callback && camera->hsd_obj == source->ground_cobj &&
        source->ground_cobj) {
        if (camera->gxlink_prios != 0x8)
            return refuse(error, error_size,
                          "Ground camera mask differs from source GX3 schedule");
        selection->kind = SLIPPI_PARTICLE_CAMERA_GROUND_ZERO;
        selection->gx_mask = 0;
        selection->known_zero = 1;
        return 1;
    }
    if (camera == source->magnify_camera && source->magnify_camera &&
        camera->render_cb == source->magnify_callback &&
        source->magnify_callback && camera->hsd_obj == source->magnify_cobj &&
        source->magnify_cobj) {
        if (camera->gxlink_prios != 0x10)
            return refuse(error, error_size,
                          "Magnify camera mask differs from source GX4 schedule");
        selection->kind = SLIPPI_PARTICLE_CAMERA_MAGNIFY_ZERO;
        selection->gx_mask = 0;
        selection->known_zero = 1;
        return 1;
    }
    return refuse(error, error_size,
                  "Camera callback or owned CObj identity is unsupported");
}

static int inspect_link(const SlippiParticlePassSource* source, unsigned link,
                        size_t* manager_count, char* error,
                        size_t error_size)
{
    const HSD_GObj* object = source->gx_heads[link];
    const HSD_GObj* previous = NULL;
    unsigned count = 0;
    while (object) {
        if (count >= source->live_gobj_bound)
            return refuse(error, error_size,
                          "GX link exceeded the live GObj allocator bound");
        if (object->gx_link != link || object->prev_gx != previous)
            return refuse(error, error_size,
                          "GX link topology disagrees with its source link");
        if (object->render_cb) {
            if (manager_is(object, source, link)) {
                if (*manager_count == SIZE_MAX)
                    return refuse(error, error_size,
                                  "Selected particle manager count overflowed");
                ++*manager_count;
            } else if (effect_owner_is(object, link)) {
                if (!effect_display_objects_safe(object, error, error_size))
                    return 0;
            } else {
                return refuse(error, error_size,
                              "Selected GX link has an unsupported render callback");
            }
        }
        previous = object;
        object = object->next_gx;
        ++count;
    }
    if (source->gx_tails[link] != previous)
        return refuse(error, error_size,
                      "GX link tail disagrees with its source topology");
    return 1;
}

int slippi_profile_particle_pass_plan_with_mask(
    const HSD_GObj* camera, const SlippiParticlePassSource* source,
    SlippiParticlePassVisit* visits, size_t visit_capacity,
    size_t* visit_count, unsigned* selected_link_mask, char* error,
    size_t error_size)
{
    CameraSelection selection;
    size_t managers = 0;
    size_t needed;
    unsigned code;
    unsigned link;

    if (!camera || !source || !visit_count)
        return refuse(error, error_size, "Particle pass planner input is null");
    if (source->gx_link_max != SLIPPI_PARTICLE_GX_LINK_MAX ||
        source->gx_slot_count != SLIPPI_PARTICLE_GX_LINK_SLOTS ||
        !source->gx_heads || !source->gx_tails ||
        !source->particle_callback || !source->model_manager_process ||
        !source->aux_manager_process)
        return refuse(error, error_size,
                      "Original GX link table or particle identity is unavailable");
    if (!select_camera(camera, source, &selection, error, error_size)) return 0;
    if (selection.known_zero || selection.gx_mask == 0) {
        if (error && error_size) error[0] = '\0';
        *visit_count = 0;
        if (selected_link_mask) *selected_link_mask = 0;
        return 1;
    }

    /* The source HSD dispatcher walks code first, then ascending GX link,
     * then next_gx. Preflight every selected list before exposing any plan. */
    if ((selection.gx_mask & SLIPPI_PARTICLE_GX_MODEL_MASK) &&
        !inspect_link(source, SLIPPI_PARTICLE_GX_MODEL, &managers, error,
                      error_size))
        return 0;
    if ((selection.gx_mask & SLIPPI_PARTICLE_GX_AUX_MASK) &&
        !inspect_link(source, SLIPPI_PARTICLE_GX_AUX, &managers, error,
                      error_size))
        return 0;
    if (managers > SIZE_MAX / 3)
        return refuse(error, error_size,
                      "Particle pass visit count overflowed");
    needed = managers * 3;
    if (needed > visit_capacity || (needed && !visits))
        return refuse(error, error_size,
                      "Particle pass output capacity is too small");

    size_t next = 0;
    for (code = 0; code < 3; ++code) {
        for (link = SLIPPI_PARTICLE_GX_MODEL;
             link <= SLIPPI_PARTICLE_GX_AUX; ++link) {
            const HSD_GObj* object;
            if (!(selection.gx_mask & (1u << link))) continue;
            for (object = source->gx_heads[link]; object;
                 object = object->next_gx) {
                /* Safe effect JObj nodes are preflighted for source safety,
                 * but the original HSD dispatcher must run them outside the
                 * particle-manager visit plan. Keep emission/count identical. */
                if (!manager_is(object, source, link)) continue;
                visits[next].camera_kind = selection.kind;
                visits[next].camera = camera;
                visits[next].code = code;
                visits[next].gx_link = link;
                visits[next].target_mask =
                    link == SLIPPI_PARTICLE_GX_MODEL
                        ? SLIPPI_PARTICLE_TARGET_MODEL_MASK
                        : SLIPPI_PARTICLE_TARGET_AUX_MASK;
                visits[next].manager = object;
                ++next;
            }
        }
    }
    if (error && error_size) error[0] = '\0';
    *visit_count = next;
    if (selected_link_mask) *selected_link_mask = selection.gx_mask;
    return 1;
}

int slippi_profile_particle_pass_plan(
    const HSD_GObj* camera, const SlippiParticlePassSource* source,
    SlippiParticlePassVisit* visits, size_t visit_capacity,
    size_t* visit_count, char* error, size_t error_size)
{
    return slippi_profile_particle_pass_plan_with_mask(
        camera, source, visits, visit_capacity, visit_count, NULL, error,
        error_size);
}
