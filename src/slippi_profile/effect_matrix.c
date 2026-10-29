/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "effect_matrix.h"

#include "dynamics_fix.h"
#include "gameplay_bootstrap.h"
#include <melee/ef/eflib.h>
#include <melee/ef/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/objalloc.h>
#include <stdio.h>

extern HSD_ObjAllocData gobj_alloc_data;
extern HSD_GObj** plinklow_gobjs;
/* Present in the original eflib.c unit; its header intentionally keeps this
 * internal callback private. */
extern void efLib_render_callback(HSD_GObj*, int);

enum { EF_MODEL_LINK = 11, EF_AUX_LINK = 12, EF_OWNER_LINK_MAX = 12 };
typedef enum {
    OWNER_NODE_OTHER,
    OWNER_NODE_MANAGER,
    OWNER_NODE_EFFECT,
    OWNER_NODE_UNSUPPORTED_EFFECT,
} OwnerNodeKind;

static int refuse(char* error, size_t error_size, const char* message)
{
    if (error && error_size) snprintf(error, error_size, "%s", message);
    return 0;
}

static int one_process(HSD_GObj* object, HSD_GObjEvent callback,
                       unsigned priority)
{
    HSD_GObjProc* process = object->proc;
    return process && process->gobj == object &&
           process->s_link == priority && process->on_invoke == callback &&
           process->child == NULL;
}

static int particle_manager(HSD_GObj* object, unsigned link)
{
    HSD_GObjEvent process_callback;
    unsigned gx_link;
    if (link == EF_MODEL_LINK) {
        process_callback = efLib_particles_proc_main;
        gx_link = 7;
    } else {
        process_callback = efLib_particles_proc_aux;
        gx_link = 8;
    }
    return object->classifier == HSD_GOBJ_CLASS_EFFECT &&
           object->p_link == link && object->p_priority == 1 &&
           object->gx_link == gx_link && object->render_priority == 2 &&
           object->render_cb == efLib_render_callback &&
           object->obj_kind == HSD_GOBJ_OBJ_NONE && object->hsd_obj == NULL &&
           object->user_data_kind == HSD_GOBJ_USER_DATA_NONE &&
           object->user_data == NULL && object->user_data_remove_func == NULL &&
           one_process(object, process_callback, 15);
}

static int effect_owner(HSD_GObj* object, unsigned link)
{
    EF_Effect* effect;
    if (object->classifier != HSD_GOBJ_CLASS_EFFECT ||
        object->p_link != link || object->p_priority != 0 ||
        object->gx_link != 7 || object->render_priority != 1 ||
        object->render_cb != HSD_GObj_JObjCallback ||
        object->obj_kind != HSD_GObj_JObjKind || object->hsd_obj == NULL ||
        object->user_data_kind != 8 ||
        object->user_data_remove_func != efLib_remove_user_data ||
        object->user_data == NULL || !one_process(object, efLib_Update, 15))
        return 0;
    effect = (EF_Effect*) object->user_data;
    return effect->gobj == object;
}

static unsigned rejected_effect_process_signature(HSD_GObj* object)
{
    unsigned signature = 0;
    HSD_GObjProc* process = object->proc;
    if (!process) return signature;
    signature |= 1u << 0;
    if (process->gobj == object) signature |= 1u << 1;
    if (process->s_link == 15) signature |= 1u << 2;
    if (process->child == NULL) signature |= 1u << 3;
    if (process->on_invoke == efLib_particles_proc_main) signature |= 1u << 4;
    if (process->on_invoke == efLib_particles_proc_aux) signature |= 1u << 5;
    if (process->on_invoke == efLib_Update) signature |= 1u << 6;
    return signature;
}

static int refuse_unsupported_effect(HSD_GObj* object, unsigned link,
                                     char* error, size_t error_size)
{
    if (error && error_size)
        snprintf(error, error_size,
                 "Unsupported class-8 GObj class=%u link=%u/%u obj=%u ud=%u proc=%02x",
                 (unsigned) object->classifier, (unsigned) object->p_link,
                 link, (unsigned) object->obj_kind,
                 (unsigned) object->user_data_kind,
                 rejected_effect_process_signature(object));
    return 0;
}

static OwnerNodeKind classify(HSD_GObj* object, unsigned link)
{
    /* The authored scheduler links are shared by other original systems.
     * Do not inspect kind-specific pointers unless this row is an effect. */
    if (object->classifier != HSD_GOBJ_CLASS_EFFECT)
        return OWNER_NODE_OTHER;
    if (particle_manager(object, link))
        return OWNER_NODE_MANAGER;
    if (effect_owner(object, link))
        return OWNER_NODE_EFFECT;
    return OWNER_NODE_UNSUPPORTED_EFFECT;
}

static int inspect_list(unsigned link, char* error, size_t error_size)
{
    HSD_GObj* object = ((HSD_GObj**) HSD_GObj_Entities)[link];
    HSD_GObj* previous = NULL;
    unsigned count = 0;
    while (object) {
        OwnerNodeKind kind;
        if (count >= gobj_alloc_data.used)
            return refuse(error, error_size,
                          "Effect GObj list exceeded the live allocator bound");
        if (object->p_link != link || object->prev != previous)
            return refuse(error, error_size,
                          "Effect GObj list topology disagrees with its source link");
        kind = classify(object, link);
        if (kind == OWNER_NODE_UNSUPPORTED_EFFECT)
            return refuse_unsupported_effect(object, link, error, error_size);
        previous = object;
        object = object->next;
        ++count;
    }
    if (plinklow_gobjs[link] != previous)
        return refuse(error, error_size,
                      "Effect GObj tail disagrees with its source list");
    return 1;
}

static int refresh_list(unsigned link, char* error, size_t error_size)
{
    HSD_GObj* object = ((HSD_GObj**) HSD_GObj_Entities)[link];
    HSD_GObj* previous = NULL;
    unsigned count = 0;
    while (object) {
        OwnerNodeKind kind;
        if (count >= gobj_alloc_data.used)
            return refuse(error, error_size,
                          "Effect GObj list changed beyond the live allocator bound");
        if (object->p_link != link || object->prev != previous)
            return refuse(error, error_size,
                          "Effect GObj owner changed after source preflight");
        kind = classify(object, link);
        if (kind == OWNER_NODE_UNSUPPORTED_EFFECT)
            return refuse_unsupported_effect(object, link, error, error_size);
        HSD_GObj* next = object->next;
        if (kind == OWNER_NODE_EFFECT)
            slippi_profile_refresh_display_root_matrices(
                (HSD_JObj*) object->hsd_obj);
        previous = object;
        object = next;
        ++count;
    }
    if (plinklow_gobjs[link] != previous)
        return refuse(error, error_size,
                      "Effect GObj tail changed after source preflight");
    return 1;
}

int slippi_profile_effect_matrices_post_step(char* error,
                                              size_t error_size)
{
    if (!melee_web_gameplay_world_exists() ||
        !melee_web_gameplay_generation())
        return refuse(error, error_size,
                      "Effect matrices require the live owned gameplay world");
    if (!HSD_GObj_Entities || !plinklow_gobjs ||
        HSD_GObjLibInitData.p_link_max < EF_OWNER_LINK_MAX)
        return refuse(error, error_size,
                      "Original effect-owner table or link capacity is unavailable");

    if (!inspect_list(EF_MODEL_LINK, error, error_size) ||
        !inspect_list(EF_AUX_LINK, error, error_size))
        return 0;
    if (!refresh_list(EF_MODEL_LINK, error, error_size) ||
        !refresh_list(EF_AUX_LINK, error, error_size))
        return 0;
    if (error && error_size) error[0] = '\0';
    return 1;
}
