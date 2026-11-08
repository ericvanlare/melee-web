/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fighter_projection.h"
#include "gameplay_cpu_interval.h"
#include "gameplay_source_memory_runtime.h"
#include "camera_source_prepare.h"
#include "particle_source_schedule.h"
#include <melee/ft/types.h>
#if defined(MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE
#include <melee/ft/ftdrawcommon.h>
#include <melee/ft/ftlib.h>
#include <melee/if/ifmagnify.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/objalloc.h>
#endif
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/list.h>
#include <sysdolphin/baselib/shadow.h>
#include <sysdolphin/baselib/tobj.h>
#include <sysdolphin/baselib/wobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHADOW_LB_VALUE_MASK 0x1Du
#define SHADOW_WOBJ_VALUE_MASK 0x3u
#define SHADOW_COBJ_VIEW_VALUE_MASK 0xC0000000u
/* lbShadow_8000F38C clears the list, then adds the Fighter root and at most
 * its one accessory root. This is the source path's exact bound. */
#define SHADOW_MEMBER_LIMIT 2u
extern HSD_TObj* tobj_shadows;
extern HSD_TObj* tobj_toon;

typedef struct ShadowValues {
    unsigned char lb_flags;
    unsigned char intensity;
    Vec3 eye_position, interest_position;
    uint32_t eye_flags, interest_flags, camera_flags;
    unsigned char up_or_roll[sizeof(((HSD_CObj*)0)->u)];
    unsigned char near_value[sizeof(float)], far_value[sizeof(float)];
    unsigned char viewport[sizeof(((HSD_CObj*)0)->viewport)];
    unsigned char scissor[sizeof(((HSD_CObj*)0)->scissor)];
    unsigned char projection[sizeof(((HSD_CObj*)0)->projection_param)];
    unsigned char projection_type;
    unsigned char view_matrix[sizeof(((HSD_CObj*)0)->view_mtx)];
    unsigned char texture_matrix[sizeof(((HSD_TObj*)0)->mtx)];
    uint32_t texture_dirty;
} ShadowValues;

typedef struct ShadowTuple {
    LbShadow* lb;
    HSD_Shadow* shadow;
    HSD_CObj* camera;
    HSD_WObj* eye;
    HSD_WObj* interest;
    HSD_TObj* texture;
    HSD_ImageDesc* image;
    void* image_pixels;
    unsigned char scale_s[sizeof(float)], scale_t[sizeof(float)];
    unsigned char translate_s[sizeof(float)], translate_t[sizeof(float)];
    uint16_t image_width, image_height;
    GXTexFmt image_format;
    uint32_t image_mipmap;
} ShadowTuple;

typedef struct ShadowMember {
    void* object;
} ShadowMember;

typedef enum ProjectionFailure {
    PROJECTION_FAILURE_NONE,
    PROJECTION_FAILURE_CONTRACT,
    PROJECTION_FAILURE_DRAW_STATE,
    PROJECTION_FAILURE_CPU_INTERVAL_CHANGED,
    PROJECTION_FAILURE_FIGHTER_IDENTITY,
    PROJECTION_FAILURE_SHADOW_OWNER,
    PROJECTION_FAILURE_SHADOW_OBJECT,
    PROJECTION_FAILURE_SHADOW_CAMERA,
    PROJECTION_FAILURE_SHADOW_CAMERA_CHILDREN,
    PROJECTION_FAILURE_SHADOW_INVERSE,
    PROJECTION_FAILURE_SHADOW_TEXTURE,
    PROJECTION_FAILURE_SHADOW_IMAGE_DESC,
    PROJECTION_FAILURE_SHADOW_COORD,
    PROJECTION_FAILURE_SHADOW_IMAGE_PIXELS,
    PROJECTION_FAILURE_SHADOW_IMAGE_LAYOUT,
    PROJECTION_FAILURE_SHADOW_DESCRIPTOR,
    PROJECTION_FAILURE_SHADOW_MEMBER_COUNT,
    PROJECTION_FAILURE_SHADOW_MEMBER_ORDER,
    PROJECTION_FAILURE_SHADOW_MEMBER_NODE,
    PROJECTION_FAILURE_SHADOW_ACTIVE,
    PROJECTION_FAILURE_SHADOW_TEXTURE_CHAIN,
    PROJECTION_FAILURE_SHADOW_TEXTURE_ALIAS,
    PROJECTION_FAILURE_SHADOW_OPERATION,
    PROJECTION_FAILURE_SHADOW_TOON_OWNER,
    PROJECTION_FAILURE_SHADOW_TOON_LINK,
} ProjectionFailure;

/* These are presentation values only. The actual source constructor supplies
 * the owner and pool descriptor; no draw/capture-time adoption is allowed.
 * Retain creation receipts until world end: a checked source restore can make
 * an earlier actual lifetime current again. Only the current authenticated
 * pool + Fighter identity may activate its view. */
typedef struct Projection {
    struct Projection* next;
    Fighter* fp;
    const void* descriptor;
    size_t extent;
    MeleeWebFighterProjectionIdentity identity;
    unsigned char screen[sizeof(((Fighter*)0)->x2188)];
    unsigned char saved[sizeof(((Fighter*)0)->x2188)];
    unsigned char plan_screen[sizeof(((Fighter*)0)->x2188)];
    unsigned char flag, saved_flag, selected, listed;
    unsigned char plan_flag, plan_ready;
    ShadowTuple shadow;
    ShadowValues shadow_values, saved_shadow_values;
    ShadowMember draw_members[SHADOW_MEMBER_LIMIT];
    HSD_SList* canonical_shadow_head;
    HSD_SList* canonical_shadow_nodes[SHADOW_MEMBER_LIMIT];
    HSD_SList* canonical_shadow_next[SHADOW_MEMBER_LIMIT];
    void* canonical_shadow_objects[SHADOW_MEMBER_LIMIT];
    HSD_SList draw_nodes[SHADOW_MEMBER_LIMIT];
    HSD_TObj* draw_texture_next;
    HSD_GObj* draw_gobj;
    void* draw_fighter_root;
    void* draw_accessory;
    unsigned canonical_shadow_count;
    unsigned draw_member_count;
    unsigned char draw_shadow_active, draw_active_after;
    unsigned char draw_texture_seen;
    unsigned char shadow_bound, shadow_retired;
} Projection;
static Projection* owners;
static uint64_t world;
static int drawing, failed, draw_interval_active;
static ProjectionFailure first_failure;
static unsigned failure_members_before, failure_members_after;
static unsigned char failure_active_before, failure_active_after;
static int failure_has_shadow_counts;
static char failure_text[160];
static HSD_TObj** texture_head_slot;
static HSD_TObj* texture_head_before;
static unsigned texture_owner_count;
static int texture_view_open;
static Projection* pending_texture_owner;
static int pending_texture_operation;
static HSD_TObj* toon_binding;
static HSD_TObj* toon_saved_next;
static HSD_TObj* toon_last_next;
static int toon_link_registered;
static int private_armed;
static int warm_draw_valid;
static uint32_t warm_draw_source_frame;
#if defined(MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE
extern uint32_t melee_web_match_source_frames(void);
extern int slippi_magnifier_source_hud_visible(void);
extern int slippi_camera_source_fighter_pass_enabled(void);
extern HSD_ObjAllocData gobj_alloc_data;
#endif

static int refuse_with(ProjectionFailure reason)
{
    if (!failed) {
        failed = 1;
        first_failure = reason == PROJECTION_FAILURE_NONE
            ? PROJECTION_FAILURE_CONTRACT : reason;
    }
    return 0;
}
static int refuse(void) { return refuse_with(PROJECTION_FAILURE_CONTRACT); }

static const char* failure_name(ProjectionFailure reason)
{
    switch (reason) {
    case PROJECTION_FAILURE_DRAW_STATE: return "draw interval state changed";
    case PROJECTION_FAILURE_CPU_INTERVAL_CHANGED: return "CPU interval armed state changed during draw";
    case PROJECTION_FAILURE_FIGHTER_IDENTITY: return "fighter lifetime identity changed";
    case PROJECTION_FAILURE_SHADOW_OWNER: return "shadow owner tuple changed";
    case PROJECTION_FAILURE_SHADOW_OBJECT: return "shadow object changed";
    case PROJECTION_FAILURE_SHADOW_CAMERA: return "shadow camera changed";
    case PROJECTION_FAILURE_SHADOW_CAMERA_CHILDREN: return "shadow camera WObj tuple changed";
    case PROJECTION_FAILURE_SHADOW_INVERSE: return "shadow camera inverse appeared";
    case PROJECTION_FAILURE_SHADOW_TEXTURE: return "shadow texture changed";
    case PROJECTION_FAILURE_SHADOW_IMAGE_DESC: return "shadow image descriptor changed";
    case PROJECTION_FAILURE_SHADOW_COORD: return "shadow texture coordinate mode changed";
    case PROJECTION_FAILURE_SHADOW_IMAGE_PIXELS: return "shadow image pixels changed";
    case PROJECTION_FAILURE_SHADOW_IMAGE_LAYOUT: return "shadow image layout changed";
    case PROJECTION_FAILURE_SHADOW_DESCRIPTOR: return "shadow texture transform changed";
    case PROJECTION_FAILURE_SHADOW_MEMBER_COUNT: return "shadow member count changed";
    case PROJECTION_FAILURE_SHADOW_MEMBER_ORDER: return "shadow member order changed";
    case PROJECTION_FAILURE_SHADOW_MEMBER_NODE: return "shadow canonical member node changed";
    case PROJECTION_FAILURE_SHADOW_ACTIVE: return "shadow active state changed";
    case PROJECTION_FAILURE_SHADOW_TEXTURE_CHAIN: return "shadow texture chain changed";
    case PROJECTION_FAILURE_SHADOW_TEXTURE_ALIAS: return "shadow texture aliases a material chain";
    case PROJECTION_FAILURE_SHADOW_OPERATION: return "unsupported shadow projection operation";
    case PROJECTION_FAILURE_SHADOW_TOON_OWNER: return "shadow toon texture owner changed";
    case PROJECTION_FAILURE_SHADOW_TOON_LINK: return "shadow toon texture link changed";
    case PROJECTION_FAILURE_NONE: return "no projection failure";
    case PROJECTION_FAILURE_CONTRACT:
    default: return "projection contract refusal";
    }
}

static int terminal_owner_failure(ProjectionFailure reason)
{
    return reason == PROJECTION_FAILURE_FIGHTER_IDENTITY ||
           reason == PROJECTION_FAILURE_SHADOW_OWNER ||
           reason == PROJECTION_FAILURE_SHADOW_OBJECT ||
           reason == PROJECTION_FAILURE_SHADOW_CAMERA ||
           reason == PROJECTION_FAILURE_SHADOW_CAMERA_CHILDREN ||
           reason == PROJECTION_FAILURE_SHADOW_TEXTURE;
}

const char* slippi_fighter_projection_failure_reason(void)
{
    const char* name = failure_name(first_failure);
    if (failure_has_shadow_counts) {
        snprintf(failure_text, sizeof(failure_text),
                 "%s (members %u->%u; active %u->%u)", name,
                 failure_members_before, failure_members_after,
                 (unsigned)failure_active_before, (unsigned)failure_active_after);
        return failure_text;
    }
    return name;
}

static int refuse_shadow_topology(ProjectionFailure reason,
                                  const Projection* p, unsigned final_count)
{
    if (!failed) {
        failure_members_before = p->canonical_shadow_count;
        failure_members_after = final_count;
        failure_active_before = p->draw_shadow_active;
        failure_active_after = p->draw_active_after;
        failure_has_shadow_counts = 1;
    }
    return refuse_with(reason);
}
static int same(MeleeWebFighterProjectionIdentity a,
                MeleeWebFighterProjectionIdentity b)
{
    return a.world==b.world && a.epoch==b.epoch &&
           a.allocation==b.allocation && a.fighter==b.fighter && a.pool==b.pool &&
           a.source==b.source && a.cell_offset==b.cell_offset;
}
static int current(Projection* p)
{
    return melee_web_source_memory_fighter_projection_matches(
        p->fp,p->descriptor,p->extent,&p->identity);
}

static unsigned char lb_shadow_value_flags(const LbShadow* lb)
{
    return (unsigned char)((lb->x0_b0 ? 0x01u : 0u) |
                           (lb->x0_b2 ? 0x04u : 0u) |
                           (lb->x0_b3 ? 0x08u : 0u) |
                           (lb->x0_b4 ? 0x10u : 0u));
}

static void set_lb_shadow_value_flags(LbShadow* lb, unsigned char flags)
{
    lb->x0_b0 = (flags & 0x01u) != 0;
    lb->x0_b2 = (flags & 0x04u) != 0;
    lb->x0_b3 = (flags & 0x08u) != 0;
    lb->x0_b4 = (flags & 0x10u) != 0;
}

static int bind_shadow(Projection* p)
{
    HSD_Shadow* shadow = p->fp->x20A4.shadow;
    HSD_CObj* camera;
    HSD_TObj* texture;
    HSD_ImageDesc* image;
    if (!shadow || !(camera = shadow->camera) ||
        !(p->shadow.eye = camera->eyepos) ||
        !(p->shadow.interest = camera->interest) ||
        /* This source-created shadow camera has no independently attested
         * inverse allocation. Keep any path that requests one fail-closed. */
        camera->proj_mtx != NULL ||
        !(texture = shadow->texture) || !(image = texture->imagedesc) ||
        !image->image_ptr || image->width == 0 || image->height == 0 ||
        tobj_coord(texture) != TEX_COORD_SHADOW)
        return 0;
    p->shadow.lb = &p->fp->x20A4;
    p->shadow.shadow = shadow;
    p->shadow.camera = camera;
    p->shadow.texture = texture;
    p->shadow.image = image;
    p->shadow.image_pixels = image->image_ptr;
    memcpy(p->shadow.scale_s, &shadow->scaleS, sizeof(float));
    memcpy(p->shadow.scale_t, &shadow->scaleT, sizeof(float));
    memcpy(p->shadow.translate_s, &shadow->transS, sizeof(float));
    memcpy(p->shadow.translate_t, &shadow->transT, sizeof(float));
    p->shadow.image_width = image->width;
    p->shadow.image_height = image->height;
    p->shadow.image_format = image->format;
    p->shadow.image_mipmap = image->mipmap;
    p->shadow_bound = 1;
    return 1;
}

/* This follows only the exact tuple installed by the real Fighter constructor.
 * It never adopts child pointers discovered during drawing. */
static ProjectionFailure shadow_tuple_failure(const Projection* p)
{
    const ShadowTuple* t = &p->shadow;
    if (!p->shadow_bound || p->shadow_retired || &p->fp->x20A4 != t->lb)
        return PROJECTION_FAILURE_SHADOW_OWNER;
    if (p->fp->x20A4.shadow != t->shadow)
        return PROJECTION_FAILURE_SHADOW_OBJECT;
    if (t->shadow->camera != t->camera)
        return PROJECTION_FAILURE_SHADOW_CAMERA;
    if (t->shadow->texture != t->texture)
        return PROJECTION_FAILURE_SHADOW_TEXTURE;
    if (t->camera->eyepos != t->eye || t->camera->interest != t->interest)
        return PROJECTION_FAILURE_SHADOW_CAMERA_CHILDREN;
    if (t->camera->proj_mtx != NULL)
        return PROJECTION_FAILURE_SHADOW_INVERSE;
    if (t->texture->imagedesc != t->image)
        return PROJECTION_FAILURE_SHADOW_IMAGE_DESC;
    if (tobj_coord(t->texture) != TEX_COORD_SHADOW)
        return PROJECTION_FAILURE_SHADOW_COORD;
    if (t->image->image_ptr != t->image_pixels)
        return PROJECTION_FAILURE_SHADOW_IMAGE_PIXELS;
    if (t->image->width != t->image_width || t->image->height != t->image_height ||
        t->image->format != t->image_format || t->image->mipmap != t->image_mipmap)
        return PROJECTION_FAILURE_SHADOW_IMAGE_LAYOUT;
    if (memcmp(&t->shadow->scaleS, t->scale_s, sizeof(float)) != 0 ||
        memcmp(&t->shadow->scaleT, t->scale_t, sizeof(float)) != 0 ||
        memcmp(&t->shadow->transS, t->translate_s, sizeof(float)) != 0 ||
        memcmp(&t->shadow->transT, t->translate_t, sizeof(float)) != 0)
        return PROJECTION_FAILURE_SHADOW_DESCRIPTOR;
    return PROJECTION_FAILURE_NONE;
}

static void read_shadow_values(const Projection* p, ShadowValues* v)
{
    const ShadowTuple* t = &p->shadow;
    v->lb_flags = lb_shadow_value_flags(t->lb);
    v->intensity = t->shadow->intensity;
    memcpy(&v->eye_position, &t->eye->pos, sizeof(v->eye_position));
    memcpy(&v->interest_position, &t->interest->pos, sizeof(v->interest_position));
    v->eye_flags = t->eye->flags & SHADOW_WOBJ_VALUE_MASK;
    v->interest_flags = t->interest->flags & SHADOW_WOBJ_VALUE_MASK;
    v->camera_flags = t->camera->flags & SHADOW_COBJ_VIEW_VALUE_MASK;
    memcpy(v->up_or_roll, &t->camera->u, sizeof(v->up_or_roll));
    memcpy(v->near_value, &t->camera->near, sizeof(v->near_value));
    memcpy(v->far_value, &t->camera->far, sizeof(v->far_value));
    memcpy(v->viewport, &t->camera->viewport, sizeof(v->viewport));
    memcpy(v->scissor, &t->camera->scissor, sizeof(v->scissor));
    memcpy(v->projection, &t->camera->projection_param, sizeof(v->projection));
    v->projection_type = t->camera->projection_type;
    memcpy(v->view_matrix, &t->camera->view_mtx, sizeof(v->view_matrix));
    memcpy(v->texture_matrix, &t->texture->mtx, sizeof(v->texture_matrix));
    v->texture_dirty = t->texture->flags & TEX_MTX_DIRTY;
}

static void write_shadow_values(const Projection* p, const ShadowValues* v)
{
    const ShadowTuple* t = &p->shadow;
    set_lb_shadow_value_flags(t->lb, v->lb_flags & SHADOW_LB_VALUE_MASK);
    t->shadow->intensity = v->intensity;
    memcpy(&t->eye->pos, &v->eye_position, sizeof(v->eye_position));
    memcpy(&t->interest->pos, &v->interest_position, sizeof(v->interest_position));
    t->eye->flags = (t->eye->flags & ~SHADOW_WOBJ_VALUE_MASK) |
                    (v->eye_flags & SHADOW_WOBJ_VALUE_MASK);
    t->interest->flags = (t->interest->flags & ~SHADOW_WOBJ_VALUE_MASK) |
                         (v->interest_flags & SHADOW_WOBJ_VALUE_MASK);
    t->camera->flags = (t->camera->flags & ~SHADOW_COBJ_VIEW_VALUE_MASK) |
                       (v->camera_flags & SHADOW_COBJ_VIEW_VALUE_MASK);
    memcpy(&t->camera->u, v->up_or_roll, sizeof(v->up_or_roll));
    memcpy(&t->camera->near, v->near_value, sizeof(v->near_value));
    memcpy(&t->camera->far, v->far_value, sizeof(v->far_value));
    memcpy(&t->camera->viewport, v->viewport, sizeof(v->viewport));
    memcpy(&t->camera->scissor, v->scissor, sizeof(v->scissor));
    memcpy(&t->camera->projection_param, v->projection, sizeof(v->projection));
    t->camera->projection_type = v->projection_type;
    memcpy(&t->camera->view_mtx, v->view_matrix, sizeof(v->view_matrix));
    memcpy(&t->texture->mtx, v->texture_matrix, sizeof(v->texture_matrix));
    t->texture->flags = (t->texture->flags & ~TEX_MTX_DIRTY) |
                        (v->texture_dirty & TEX_MTX_DIRTY);
}

/* Source may free/recreate list nodes while retaining the same ordered JObj
 * membership. Compare values, not node addresses, and never rewind its refs or
 * ownership pointers. */
static int capture_shadow_members(Projection* p)
{
    HSD_SList* node;
    p->draw_member_count = 0;
    p->canonical_shadow_count = 0;
    p->canonical_shadow_head = p->shadow.shadow->objects;
    memset(p->canonical_shadow_nodes,0,sizeof(p->canonical_shadow_nodes));
    memset(p->canonical_shadow_next,0,sizeof(p->canonical_shadow_next));
    memset(p->canonical_shadow_objects,0,sizeof(p->canonical_shadow_objects));
    for (node = p->shadow.shadow->objects; node; node = node->next) {
        unsigned i = p->draw_member_count;
        if (i == SHADOW_MEMBER_LIMIT) return 0;
        p->draw_members[i].object = node->data;
        p->canonical_shadow_nodes[i] = node;
        p->canonical_shadow_next[i] = node->next;
        p->canonical_shadow_objects[i] = node->data;
        p->draw_member_count = i + 1;
        p->canonical_shadow_count = i + 1;
    }
    p->draw_shadow_active = p->shadow.shadow->active;
    return 1;
}

static ProjectionFailure shadow_members_failure(const Projection* p,
                                                 unsigned* final_count)
{
    HSD_SList* node = p->shadow.shadow->objects;
    unsigned i;
    int order_matches = 1;
    for (i = 0; node && i <= SHADOW_MEMBER_LIMIT; ++i) {
        if (i < p->draw_member_count && node->data != p->draw_members[i].object)
            order_matches = 0;
        if (i == SHADOW_MEMBER_LIMIT) break;
        node = node->next;
    }
    if (node) {
        i = SHADOW_MEMBER_LIMIT + 1;
        order_matches = 0;
    }
    *final_count = i;
    if (i != p->draw_member_count)
        return PROJECTION_FAILURE_SHADOW_MEMBER_COUNT;
    if (!order_matches)
        return PROJECTION_FAILURE_SHADOW_MEMBER_ORDER;
    if (p->shadow.shadow->active != p->draw_shadow_active)
        return PROJECTION_FAILURE_SHADOW_ACTIVE;
    return PROJECTION_FAILURE_NONE;
}

static Projection* selected_shadow_owner(void* shadow)
{
    for (Projection* p=owners;p;p=p->next)
        if (p->selected && p->shadow.shadow == shadow) return p;
    return NULL;
}

static Projection* selected_texture_owner(HSD_TObj* texture)
{
    for (Projection* p=owners;p;p=p->next)
        if (p->selected && p->shadow.texture == texture) return p;
    return NULL;
}

static ProjectionFailure validate_texture_chain(void)
{
    for (Projection* p=owners;p;p=p->next)
        if (p->selected) p->draw_texture_seen=0;
    HSD_TObj* node = texture_head_slot ? *texture_head_slot : NULL;
    unsigned count=0;
    while (node) {
        Projection* p=selected_texture_owner(node);
        if (!p || !p->shadow.shadow->active || p->draw_texture_seen ||
            count == texture_owner_count)
            return PROJECTION_FAILURE_SHADOW_TEXTURE_CHAIN;
        p->draw_texture_seen=1;
        count++;
        node=node->next;
    }
    for (Projection* p=owners;p;p=p->next) if (p->selected) {
        if (!!p->draw_texture_seen != !!p->shadow.shadow->active)
            return PROJECTION_FAILURE_SHADOW_TEXTURE_CHAIN;
        if (!p->shadow.shadow->active && p->shadow.texture->next != NULL)
            return PROJECTION_FAILURE_SHADOW_TEXTURE_ALIAS;
    }
    return PROJECTION_FAILURE_NONE;
}

static int begin_texture_view(void)
{
    ProjectionFailure reason=PROJECTION_FAILURE_NONE;
    texture_head_slot=&tobj_shadows;
    texture_head_before=tobj_shadows;
    texture_owner_count=0;
    texture_view_open=0;
    pending_texture_owner=NULL;
    pending_texture_operation=0;
    toon_binding=tobj_toon;
    toon_saved_next=toon_binding ? toon_binding->next : NULL;
    toon_last_next=toon_saved_next;
    toon_link_registered=0;
    if (toon_binding && selected_texture_owner(toon_binding)) {
        reason=PROJECTION_FAILURE_SHADOW_TEXTURE_ALIAS;
        goto refused;
    }
    /* toon_saved_next is opaque historical state: Setup overwrites it and
     * its former chain may have been retired. Live consumers preflight their
     * actual ordinary chain before each prepend. */
    for (Projection* p=owners;p;p=p->next) if (p->selected) {
        for (Projection* other=owners;other!=p;other=other->next)
            if (other->selected && other->shadow.texture == p->shadow.texture)
                { reason=PROJECTION_FAILURE_SHADOW_TEXTURE_ALIAS; goto refused; }
        p->draw_texture_next=p->shadow.texture->next;
        p->draw_texture_seen=0;
        texture_owner_count++;
    }
    reason=validate_texture_chain();
    if (reason != PROJECTION_FAILURE_NONE) goto refused;
    texture_view_open=1;
    return 1;
refused:
    texture_head_slot=NULL;
    texture_head_before=NULL;
    texture_owner_count=0;
    toon_binding=NULL;toon_saved_next=NULL;toon_last_next=NULL;
    toon_link_registered=0;
    return refuse_with(reason);
}

static ProjectionFailure restore_texture_view(void)
{
    ProjectionFailure reason=PROJECTION_FAILURE_NONE;
    if (!texture_view_open || texture_head_slot != &tobj_shadows) {
        reason=PROJECTION_FAILURE_SHADOW_TEXTURE_CHAIN;
    } else {
        reason=validate_texture_chain();
    }
    if (tobj_toon != toon_binding) {
        if (reason == PROJECTION_FAILURE_NONE)
            reason=PROJECTION_FAILURE_SHADOW_TOON_OWNER;
    } else if (toon_binding) {
        if (toon_binding->next != toon_last_next &&
            reason == PROJECTION_FAILURE_NONE)
            reason=PROJECTION_FAILURE_SHADOW_TOON_LINK;
        toon_binding->next=toon_saved_next;
    }
    if (pending_texture_owner && reason == PROJECTION_FAILURE_NONE)
        reason=PROJECTION_FAILURE_SHADOW_OPERATION;
    if (texture_head_slot) *texture_head_slot=texture_head_before;
    for (Projection* p=owners;p;p=p->next)
        if (p->selected && p->shadow.texture)
            p->shadow.texture->next=p->draw_texture_next;
    texture_view_open=0;
    texture_head_slot=NULL;
    texture_head_before=NULL;
    texture_owner_count=0;
    pending_texture_owner=NULL;
    pending_texture_operation=0;
    toon_binding=NULL;toon_saved_next=NULL;toon_last_next=NULL;
    toon_link_registered=0;
    return reason;
}

static void write_projected_members(Projection* p)
{
    for (unsigned i=0;i<SHADOW_MEMBER_LIMIT;i++) {
        p->draw_nodes[i].data = i < p->draw_member_count
            ? p->draw_members[i].object : NULL;
        p->draw_nodes[i].next = i+1 < p->draw_member_count
            ? &p->draw_nodes[i+1] : NULL;
    }
    p->shadow.shadow->objects = p->draw_member_count ? &p->draw_nodes[0] : NULL;
}

static int borrowed_member_is_current(Projection* p, void* object)
{
    HSD_GObj* g;
    int listed=0;
    if (!p || !current(p) || !p->draw_gobj || !p->draw_fighter_root)
        return 0;
    for (g=HSD_GObj_Entities ? HSD_GObj_Entities->fighters : NULL;
         g;g=g->next)
        if (g == p->draw_gobj) { listed=1; break; }
    if (!listed || g->user_data != p->fp || g->hsd_obj != p->draw_fighter_root)
        return 0;
    if (object == p->draw_fighter_root) return 1;
    return object && object == p->draw_accessory &&
           p->fp->x20A0_accessory == p->draw_accessory;
}

static ProjectionFailure projected_members_failure(const Projection* p,
                                                    unsigned* final_count)
{
    const HSD_SList* expected = p->draw_member_count ? &p->draw_nodes[0] : NULL;
    if (p->shadow.shadow->objects != expected)
        return PROJECTION_FAILURE_SHADOW_MEMBER_NODE;
    unsigned count=0;
    const HSD_SList* node=p->shadow.shadow->objects;
    while (node) {
        if (count == SHADOW_MEMBER_LIMIT || node != &p->draw_nodes[count]) {
            *final_count=count+1;
            return PROJECTION_FAILURE_SHADOW_MEMBER_COUNT;
        }
        if (node->data != p->draw_members[count].object ||
            node->next != (count+1 < p->draw_member_count
                               ? &p->draw_nodes[count+1] : NULL)) {
            *final_count=count+1;
            return PROJECTION_FAILURE_SHADOW_MEMBER_ORDER;
        }
        count++;
        node=node->next;
    }
    *final_count=count;
    for (unsigned i=0;i<p->canonical_shadow_count;i++)
        if (!p->canonical_shadow_nodes[i] ||
            p->canonical_shadow_nodes[i]->data != p->canonical_shadow_objects[i] ||
            p->canonical_shadow_nodes[i]->next != p->canonical_shadow_next[i])
            return PROJECTION_FAILURE_SHADOW_MEMBER_NODE;
    return PROJECTION_FAILURE_NONE;
}

static ProjectionFailure restore_member_view(Projection* p,
                                             unsigned* final_count)
{
    ProjectionFailure reason=projected_members_failure(p,final_count);
    p->draw_active_after=p->shadow.shadow->active;
    p->shadow.shadow->objects=p->canonical_shadow_head;
    p->shadow.shadow->active=p->draw_shadow_active;
    return reason;
}

int slippi_fighter_projection_shadow_members(void* shadow, void* object, int operation)
{
    if (!drawing || !draw_interval_active) return -1;
    if (failed) return 0;
    Projection* p=selected_shadow_owner(shadow);
    if (!p || !texture_view_open || !p->shadow.shadow) {
        refuse_with(PROJECTION_FAILURE_SHADOW_OPERATION);
        return 0;
    }
    if (!current(p)) {
        refuse_with(PROJECTION_FAILURE_FIGHTER_IDENTITY);
        return 0;
    }
    ProjectionFailure tuple=shadow_tuple_failure(p);
    if (tuple != PROJECTION_FAILURE_NONE) {
        refuse_with(tuple);
        return 0;
    }
    if (operation == 0) {
        p->draw_member_count=0;
        write_projected_members(p);
        return 1;
    }
    if (operation == 1) {
        if (!object) return 1;
        if (!borrowed_member_is_current(p,object)) {
            refuse_with(PROJECTION_FAILURE_SHADOW_OWNER);
            return 0;
        }
        for (unsigned i=0;i<p->draw_member_count;i++)
            if (p->draw_members[i].object == object) return 1;
        if (p->draw_member_count == SHADOW_MEMBER_LIMIT) {
            refuse_with(PROJECTION_FAILURE_SHADOW_MEMBER_COUNT);
            return 0;
        }
        for (unsigned i=p->draw_member_count;i>0;i--)
            p->draw_members[i].object=p->draw_members[i-1].object;
        p->draw_members[0].object=object;
        p->draw_member_count++;
        write_projected_members(p);
        return 1;
    }
    if (operation == 2) {
        if (!object) return 1;
        if (!borrowed_member_is_current(p,object)) {
            refuse_with(PROJECTION_FAILURE_SHADOW_OWNER);
            return 0;
        }
        unsigned found=p->draw_member_count;
        for (unsigned i=0;i<p->draw_member_count;i++)
            if (p->draw_members[i].object == object) { found=i; break; }
        if (found == p->draw_member_count) return 1;
        for (unsigned i=found+1;i<p->draw_member_count;i++)
            p->draw_members[i-1].object=p->draw_members[i].object;
        p->draw_member_count--;
        write_projected_members(p);
        return 1;
    }
    refuse_with(PROJECTION_FAILURE_SHADOW_OPERATION);
    return 0;
}

int slippi_fighter_projection_shadow_active(void* shadow, int active)
{
    if (!drawing || !draw_interval_active) return -1;
    if (failed) return 0;
    Projection* p=selected_shadow_owner(shadow);
    if (!p || !texture_view_open) {
        refuse_with(PROJECTION_FAILURE_SHADOW_OPERATION);
        return 0;
    }
    if (!current(p)) {
        refuse_with(PROJECTION_FAILURE_FIGHTER_IDENTITY);
        return 0;
    }
    ProjectionFailure tuple=shadow_tuple_failure(p);
    if (tuple != PROJECTION_FAILURE_NONE) {
        refuse_with(tuple);
        return 0;
    }
    active=!!active;
    if (!!p->shadow.shadow->active == active) return 1;
    if (pending_texture_owner || !p->shadow.image_pixels) {
        refuse_with(PROJECTION_FAILURE_SHADOW_OPERATION);
        return 0;
    }
    pending_texture_owner=p;
    pending_texture_operation=active ? 1 : -1;
    return 1;
}

int slippi_fighter_projection_shadow_texture_operation(void* head_slot,
                                                       void* texture, int add)
{
    if (!drawing || !draw_interval_active) return -1;
    if (failed) return 0;
    Projection* p=pending_texture_owner;
    if (!texture_view_open || head_slot != &tobj_shadows || !p ||
        p->shadow.texture != texture || pending_texture_operation != (add ? 1 : -1)) {
        refuse_with(PROJECTION_FAILURE_SHADOW_OPERATION);
        return 0;
    }
    if (!current(p) || shadow_tuple_failure(p) != PROJECTION_FAILURE_NONE) {
        refuse_with(PROJECTION_FAILURE_FIGHTER_IDENTITY);
        return 0;
    }
    pending_texture_owner=NULL;
    pending_texture_operation=0;
    return 1;
}

int slippi_fighter_projection_mobj_preflight(void* ordinary_head,
                                            void* toon_texture)
{
    if (!drawing || !draw_interval_active) return -1;
    if (failed) return 0;
    if (!texture_view_open) {
        refuse_with(PROJECTION_FAILURE_SHADOW_TEXTURE_CHAIN);
        return 0;
    }
    for (Projection* p=owners;p;p=p->next) if (p->selected && !current(p)) {
        refuse_with(PROJECTION_FAILURE_FIGHTER_IDENTITY);
        return 0;
    }
    if (toon_texture && selected_texture_owner((HSD_TObj*)toon_texture)) {
        refuse_with(PROJECTION_FAILURE_SHADOW_TEXTURE_ALIAS);
        return 0;
    }
    HSD_TObj* node=(HSD_TObj*)ordinary_head;
    while (node) {
        if (selected_texture_owner(node)) {
            refuse_with(PROJECTION_FAILURE_SHADOW_TEXTURE_ALIAS);
            return 0;
        }
        if (node == (HSD_TObj*)toon_texture) {
            refuse_with(PROJECTION_FAILURE_SHADOW_TOON_LINK);
            return 0;
        }
        node=node->next;
    }
    return 1;
}

int slippi_fighter_projection_mobj_compile(void)
{
    if (!drawing || !draw_interval_active) return -1;
    refuse_with(PROJECTION_FAILURE_SHADOW_OPERATION);
    return 0;
}

int slippi_fighter_projection_mobj_toon_link(void* toon_texture,
                                             void* next_texture)
{
    if (!drawing || !draw_interval_active) return -1;
    if (failed) return 0;
    HSD_TObj* toon=(HSD_TObj*)toon_texture;
    HSD_TObj* next=(HSD_TObj*)next_texture;
    if (!texture_view_open || !toon || toon != toon_binding ||
        tobj_toon != toon_binding || selected_texture_owner(toon) ||
        next == toon) {
        refuse_with(PROJECTION_FAILURE_SHADOW_TOON_OWNER);
        return 0;
    }
    for (Projection* p=owners;p;p=p->next) if (p->selected && !current(p)) {
        refuse_with(PROJECTION_FAILURE_FIGHTER_IDENTITY);
        return 0;
    }
    HSD_TObj* expected=toon_link_registered ? toon_last_next : toon_saved_next;
    if (toon->next != expected) {
        refuse_with(PROJECTION_FAILURE_SHADOW_TOON_LINK);
        return 0;
    }
    toon_last_next=next;
    toon_link_registered=1;
    return 1;
}

int slippi_fighter_projection_world_begin(uint64_t w)
{
    if (!w || world || owners || drawing || texture_view_open) return refuse();
    world=w; failed=0; first_failure=PROJECTION_FAILURE_NONE;
    failure_has_shadow_counts=0;
    draw_interval_active=0;
    private_armed=0;warm_draw_valid=0;warm_draw_source_frame=0;
    texture_head_slot=NULL;texture_head_before=NULL;texture_owner_count=0;
    pending_texture_owner=NULL;pending_texture_operation=0;
    toon_binding=NULL;toon_saved_next=NULL;toon_last_next=NULL;toon_link_registered=0;
    return 1;
}
int slippi_fighter_projection_world_end(uint64_t w)
{
    if (!w || w!=world || drawing || texture_view_open) return refuse();
    while (owners) { Projection* p=owners; owners=p->next; free(p); }
    world=0; failed=0; draw_interval_active=0;
    private_armed=0;warm_draw_valid=0;warm_draw_source_frame=0;
    texture_head_slot=NULL;texture_head_before=NULL;texture_owner_count=0;
    pending_texture_owner=NULL;pending_texture_operation=0;
    toon_binding=NULL;toon_saved_next=NULL;toon_last_next=NULL;toon_link_registered=0;
    return 1;
}
#if defined(MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE
int slippi_fighter_projection_private_arm(uint64_t w, uint32_t source_frame)
{
    unsigned count = 0;
    if (!w || w != world || failed || drawing || private_armed ||
        melee_web_cpu_interval_active() || !warm_draw_valid ||
        warm_draw_source_frame != source_frame)
        return refuse();
    for (Projection* p=owners;p;p=p->next) {
        if (!current(p)) continue;
        if (p->shadow_retired || shadow_tuple_failure(p) != PROJECTION_FAILURE_NONE ||
            p->selected || !p->listed)
            return refuse_with(PROJECTION_FAILURE_FIGHTER_IDENTITY);
        ++count;
    }
    if (!count) return refuse();
    /* The most recent successful ordinary draw supplied the carry. All owner
     * checks finish before any source Fighter field is seeded. */
    for (Projection* p=owners;p;p=p->next) if (current(p)) {
        memcpy(&p->fp->x2188,p->screen,sizeof(p->screen));
        p->fp->x221F_b0=p->flag;
    }
    private_armed=1;
    warm_draw_valid=0;
    return 1;
}
int slippi_fighter_projection_private_armed(void) { return private_armed && !failed; }
int slippi_fighter_projection_magnifier_private_draw(void)
{
    return private_armed && drawing && draw_interval_active && !failed;
}
static Projection* projection_for_fighter(Fighter* fp)
{
    for (Projection* p=owners;p;p=p->next)
        if (p->fp==fp && !p->shadow_retired && current(p)) return p;
    return NULL;
}

/* GObj_SetupGXLink sets the callback/link fields, not a camera mask.
 * Prove that the exact fighter is linked into the source traversal. */
static int fighter_gx_member(const HSD_GObj* fighter)
{
    const unsigned link=5;
    const HSD_GObj* previous=NULL;
    unsigned remaining=gobj_alloc_data.used, matches=0;
    if (HSD_GObjLibInitData.gx_link_max<link || !HSD_GObjGXLinkHead ||
        !HSD_GObj_804D7820)
        return 0;
    for (const HSD_GObj* node=HSD_GObjGXLinkHead[link];node;node=node->next_gx) {
        if (!remaining || node->gx_link!=link || node->prev_gx!=previous)
            return 0;
        --remaining;
        matches+=node==fighter;
        previous=node;
    }
    return matches==1 && previous==HSD_GObj_804D7820[link];
}

int slippi_fighter_projection_source_prepare(uint64_t w, uint64_t token,
    HSD_GObj* main_camera, HSD_GObj* hud_camera, HSD_GObj* magnifier_camera,
    unsigned hud_hidden)
{
    HSD_GObj* scheduled_main = NULL;
    HSD_GObj* scheduled_hud = NULL;
    HSD_GObj* scheduled_magnifier = NULL;
    unsigned scheduled_hidden = 0;
    HSD_CObj* camera;
    MeleeWebSourceMemoryContext context = {0};
    HSD_GObj* fighters[6] = {0};
    Projection* slot_projection[6] = {0};
    unsigned char eligible[6] = {0};
    unsigned char edge_ready[6] = {0};
    unsigned char edge_value[6] = {0};
    if (!private_armed) return 1;
    if (!w || w!=world || failed || drawing || !token ||
        !melee_web_cpu_interval_active() ||
        !melee_web_cpu_interval_admit(token) ||
        melee_web_cpu_interval_token()!=token ||
        melee_web_source_memory_context_read(&context)!=MELEE_WEB_SOURCE_MEMORY_READ_OK ||
        context.world_generation!=w || !main_camera || !hud_camera ||
        !magnifier_camera || hud_hidden>1 || !HSD_GObj_Entities)
        return refuse();
    if (!slippi_particle_source_magnifier_identity(w,token,&scheduled_main,
            &scheduled_hud,&scheduled_magnifier,&scheduled_hidden) ||
        scheduled_main!=main_camera || scheduled_hud!=hud_camera ||
        scheduled_magnifier!=magnifier_camera || scheduled_hidden!=hud_hidden ||
        !magnifier_camera->hsd_obj)
        return refuse();
    const int hud_visible=slippi_magnifier_source_hud_visible();
    const int fighter_pass=slippi_camera_source_fighter_pass_enabled();
    camera=(HSD_CObj*)main_camera->hsd_obj;
    for (Projection* p=owners;p;p=p->next) p->plan_ready=0;

    /* Phase one is read-only with respect to the Fighter and magnifier
     * statics. Preserve the original camera traversal order: eligibility uses
     * the preceding source cull, while plan_screen/plan_flag model the later
     * main-camera callback against the freshly prepared exact CObj. */
    for (unsigned slot=0;slot<6;++slot) {
        ifMagnifyPlayer* player=&ifMagnify_804A1DE0.player[slot];
        HSD_GObj* fighter=Player_GetEntity((s32)slot);
        Projection* p=NULL;
        fighters[slot]=fighter;
        if (fighter) {
            if (!fighter->user_data) return refuse();
            p=projection_for_fighter((Fighter*)fighter->user_data);
            if (!p || p->shadow_retired || shadow_tuple_failure(p)!=PROJECTION_FAILURE_NONE)
                return refuse_with(PROJECTION_FAILURE_FIGHTER_IDENTITY);
            /* Player_GetEntity is a source owner lookup, not proof that this
             * object still has the authored main-camera renderer registration. */
            if (fighter->classifier != HSD_GOBJ_CLASS_FIGHTER ||
                fighter->p_link != 8 || fighter->p_priority != 0 ||
                fighter->render_priority != 0 || fighter->user_data_kind != 4 ||
                fighter->gx_link != 5 ||
                fighter->render_cb != ftDrawCommon_80080E18 ||
                !fighter_gx_member(fighter))
                return refuse_with(PROJECTION_FAILURE_FIGHTER_IDENTITY);
            for (unsigned prior=0;prior<slot;++prior)
                if (fighters[prior]==fighter) return refuse();
            slot_projection[slot]=p;
            eligible[slot]=(unsigned char)(hud_visible &&
                !player->state.ignore_offscreen && ftLib_80086B64(fighter) &&
                ftLib_80086ED0(fighter));
        }
        if (!p) continue;
        memcpy(p->plan_screen,&p->fp->x2188,sizeof(p->plan_screen));
        p->plan_flag=p->fp->x221F_b0;
        p->plan_ready=0;
        /* ftDrawCommon_80080E18 skips this fighter before invoking
         * ftLib_80086A8C, so retain the authored carry unchanged. */
        if (!fighter_pass || p->fp->x221F_b3) continue;
        p->plan_ready=1;
        if (p->fp->x2229_b3 || p->fp->x2220_b7) {
            p->plan_flag=0;
            continue;
        }
        int wrote=0,culled=0,draw=0;
        S32Vec2 projected_screen;
        memcpy(&projected_screen,p->plan_screen,sizeof(projected_screen));
        if (!slippi_camera_source_project_subject(camera,p->fp->x890_cameraBox,
                &projected_screen,&wrote,&culled,&draw))
            return refuse();
        memcpy(p->plan_screen,&projected_screen,sizeof(projected_screen));
        (void)wrote;
        (void)draw;
        p->plan_flag=(unsigned char)culled;
    }
    for (unsigned slot=0;slot<6;++slot) if (eligible[slot]) {
        Projection* p=slot_projection[slot];
        if (!p || !fighters[slot]) return refuse();
        ifMagnifyPlayer projected=ifMagnify_804A1DE0.player[slot];
        S32Vec2 screen;
        Vec2 direction,edge_position;
        memcpy(&screen,p->plan_screen,sizeof(screen));
        direction.x=screen.x-320.0f;
        direction.y=-((f32)screen.y-240.0f);
        ifMagnify_802FB73C(&projected,&direction,&edge_position);
        edge_value[slot]=projected.state.edge;
        edge_ready[slot]=1;
    }

    /* Every owner and branch has been admitted. Commit the exact old render
     * side effects once for this traversal; source PRE now captures the
     * rollback-owned pair and magnifier edge state directly. */
    for (Projection* p=owners;p;p=p->next) if (p->plan_ready) {
        memcpy(&p->fp->x2188,p->plan_screen,sizeof(p->plan_screen));
        p->fp->x221F_b0=p->plan_flag;
        memcpy(p->screen,p->plan_screen,sizeof(p->screen));
        p->flag=p->plan_flag;
    }
    for (unsigned slot=0;slot<6;++slot) {
        ifMagnifyPlayer* player=&ifMagnify_804A1DE0.player[slot];
        player->state.is_offscreen=eligible[slot];
        if (edge_ready[slot]) player->state.edge=edge_value[slot];
    }
    return !failed;
}
#else
int slippi_fighter_projection_private_arm(uint64_t w,uint32_t frame)
{ (void)w; (void)frame; return 1; }
int slippi_fighter_projection_private_armed(void) { return 0; }
int slippi_fighter_projection_magnifier_private_draw(void) { return 0; }
int slippi_fighter_projection_source_prepare(uint64_t w,uint64_t token,
    HSD_GObj* main_camera,HSD_GObj* hud_camera,HSD_GObj* magnifier_camera,
    unsigned hud_hidden)
{
    (void)w; (void)token; (void)main_camera; (void)hud_camera;
    (void)magnifier_camera; (void)hud_hidden;
    return 1;
}
#endif
int slippi_fighter_projection_created(void* object,const void* descriptor,size_t extent)
{
    MeleeWebFighterProjectionIdentity identity;
    if (!world || failed || drawing || extent!=sizeof(Fighter) ||
        !melee_web_source_memory_fighter_projection_identity(
            object,descriptor,extent,&identity) || identity.world!=world)
        return refuse();
    Projection* p;
    for(p=owners;p;p=p->next)
        if(p->fp==object && same(p->identity,identity)) break;
    if (p) {
        ProjectionFailure reason = shadow_tuple_failure(p);
        if (reason != PROJECTION_FAILURE_NONE) return refuse_with(reason);
        return 1;
    }
    p=calloc(1,sizeof(*p)); if(!p) return refuse();
    p->next=owners; owners=p;
    p->fp=object;p->descriptor=descriptor;p->extent=extent;p->identity=identity;
    if (!bind_shadow(p)) return refuse();
    memcpy(p->screen,&p->fp->x2188,sizeof(p->screen));
    p->flag=p->fp->x221F_b0;p->selected=0;
    read_shadow_values(p, &p->shadow_values);
    return 1;
}
int slippi_fighter_projection_shadow_retiring(void* lbshadow, void* shadow)
{
    Projection* p;
    if (!world) return 1;
    if (failed || drawing) return refuse();
    for (p=owners;p;p=p->next) {
        if ((void*)p->shadow.lb != lbshadow) continue;
        if (!p->shadow_bound || p->shadow_retired || p->shadow.shadow != shadow)
            return refuse_with(PROJECTION_FAILURE_SHADOW_OBJECT);
        if (!current(p)) return refuse_with(PROJECTION_FAILURE_FIGHTER_IDENTITY);
        ProjectionFailure reason = shadow_tuple_failure(p);
        if (reason != PROJECTION_FAILURE_NONE) return refuse_with(reason);
        p->shadow_retired = 1;
        return 1;
    }
    return refuse();
}
int slippi_fighter_projection_retiring(void* object)
{
    if (!world || failed || drawing) return refuse();
    for(Projection* p=owners;p;p=p->next)
        if(p->fp==object && current(p) && p->shadow_bound && p->shadow_retired) return 1;
    return refuse();
}
int slippi_fighter_projection_require(void* object)
{
    if (!world || failed || !drawing) return refuse();
    for(Projection* p=owners;p;p=p->next)
        if(p->fp==object && p->selected && current(p)) return 1;
    return refuse();
}
int slippi_fighter_projection_draw_begin(void)
{
    if (!world || failed || drawing) return refuse();
    const int interval_active = melee_web_cpu_interval_active();
    /* Complete provenance preflight before any source field is swapped. Stale
     * lifetime receipts are inert, never dereferenced or adopted. */
    for(Projection* p=owners;p;p=p->next) {
        p->selected=current(p);
        p->listed=0;
        p->draw_gobj=NULL;
        p->draw_fighter_root=NULL;
        p->draw_accessory=NULL;
    }
    if (!HSD_GObj_Entities) return refuse();
    for (HSD_GObj* g=HSD_GObj_Entities->fighters;g;g=g->next) {
        Projection* p;
        for(p=owners;p;p=p->next)
            if(p->fp==g->user_data && p->selected) break;
        if (!p || p->listed) return refuse();
        ProjectionFailure reason = shadow_tuple_failure(p);
        if (reason != PROJECTION_FAILURE_NONE) return refuse_with(reason);
        p->draw_gobj=g;
        p->draw_fighter_root=g->hsd_obj;
        p->draw_accessory=p->fp->x20A0_accessory;
        p->listed=1;
    }
    for(Projection* p=owners;p;p=p->next)
        if(p->selected && !p->listed) return refuse();
    for(Projection* p=owners;p;p=p->next) if(p->selected) {
        read_shadow_values(p, &p->saved_shadow_values);
        if (!capture_shadow_members(p)) return refuse();
        if (interval_active)
            for (unsigned i=0;i<p->canonical_shadow_count;i++)
                if (!borrowed_member_is_current(p,p->canonical_shadow_objects[i]))
                    return refuse_with(PROJECTION_FAILURE_SHADOW_OWNER);
    }
    if (interval_active) {
        if (!begin_texture_view()) return 0;
        for(Projection* p=owners;p;p=p->next) if(p->selected)
            write_projected_members(p);
    }
    for(Projection* p=owners;p;p=p->next) if(p->selected) {
        memcpy(p->saved,&p->fp->x2188,sizeof(p->saved));
        p->saved_flag=p->fp->x221F_b0;
        if (!(interval_active && private_armed)) {
            memcpy(&p->fp->x2188,p->screen,sizeof(p->screen));
            p->fp->x221F_b0=p->flag;
        }
        write_shadow_values(p, &p->shadow_values);
    }
    draw_interval_active = interval_active;
    drawing=1; return 1;
}
int slippi_fighter_projection_draw_end(void)
{
    const int completed_cpu_interval=draw_interval_active;
    const int completed_private_interval=completed_cpu_interval && private_armed;
    if (!world || !drawing) return refuse_with(PROJECTION_FAILURE_DRAW_STATE);
    /* Lifecycle hooks refuse destruction/construction during the view. Check
     * every identity again before reading or restoring any source fields. */
    for(Projection* p=owners;p;p=p->next) if (p->selected) {
        if (!current(p)) return refuse_with(PROJECTION_FAILURE_FIGHTER_IDENTITY);
        ProjectionFailure reason = shadow_tuple_failure(p);
        if (terminal_owner_failure(reason)) return refuse_with(reason);
        if (reason != PROJECTION_FAILURE_NONE) refuse_with(reason);
    }
    if (melee_web_cpu_interval_active() != draw_interval_active)
        refuse_with(PROJECTION_FAILURE_CPU_INTERVAL_CHANGED);
    ProjectionFailure topology_failure = PROJECTION_FAILURE_NONE;
    Projection* topology_owner = NULL;
    unsigned topology_final_count = 0;
    for(Projection* p=owners;p;p=p->next) if(p->selected) {
        if (completed_private_interval) {
            if (memcmp(p->saved,&p->fp->x2188,sizeof(p->saved)) != 0 ||
                p->saved_flag != p->fp->x221F_b0)
                refuse_with(PROJECTION_FAILURE_DRAW_STATE);
        } else {
            memcpy(p->screen,&p->fp->x2188,sizeof(p->screen));
            p->flag=p->fp->x221F_b0;
        }
        read_shadow_values(p, &p->shadow_values);
        unsigned final_count = 0;
        if (draw_interval_active) {
            ProjectionFailure reason=projected_members_failure(p,&final_count);
            if (topology_failure == PROJECTION_FAILURE_NONE &&
                reason != PROJECTION_FAILURE_NONE) {
                topology_failure=reason;
                topology_owner=p;
                topology_final_count=final_count;
            }
        } else {
            ProjectionFailure reason=shadow_members_failure(p,&final_count);
            /* Keep the existing source-only behavior outside an armed CPU
             * interval. The private rollback view alone owns temporary lists. */
            if (topology_failure == PROJECTION_FAILURE_NONE &&
                reason != PROJECTION_FAILURE_NONE &&
                final_count > SHADOW_MEMBER_LIMIT) {
                topology_failure=reason;
                topology_owner=p;
                topology_final_count=final_count;
            }
            p->draw_active_after=p->shadow.shadow->active;
        }
    }
    if (draw_interval_active) {
        ProjectionFailure texture_reason=restore_texture_view();
        if (topology_failure == PROJECTION_FAILURE_NONE &&
            texture_reason != PROJECTION_FAILURE_NONE) {
            topology_failure=texture_reason;
            for (Projection* p=owners;p;p=p->next)
                if (p->selected) { topology_owner=p; break; }
        }
        for(Projection* p=owners;p;p=p->next) if(p->selected) {
            unsigned final_count=0;
            ProjectionFailure reason=restore_member_view(p,&final_count);
            if (topology_failure == PROJECTION_FAILURE_NONE &&
                reason != PROJECTION_FAILURE_NONE) {
                topology_failure=reason;
                topology_owner=p;
                topology_final_count=final_count;
            }
        }
    }
    for(Projection* p=owners;p;p=p->next) if(p->selected) {
        memcpy(&p->fp->x2188,p->saved,sizeof(p->saved));
        /* Restore only this bit; other draw-authored flag bits stay visible. */
        p->fp->x221F_b0=p->saved_flag;p->selected=0;
        write_shadow_values(p, &p->saved_shadow_values);
    }
    drawing=0;
    draw_interval_active=0;
    if (topology_failure != PROJECTION_FAILURE_NONE)
        return topology_failure == PROJECTION_FAILURE_SHADOW_MEMBER_COUNT ||
                       topology_failure == PROJECTION_FAILURE_SHADOW_MEMBER_ORDER ||
                       topology_failure == PROJECTION_FAILURE_SHADOW_ACTIVE
                   ? refuse_shadow_topology(topology_failure, topology_owner,
                                            topology_final_count)
                   : refuse_with(topology_failure);
#if defined(MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE
    if (!failed && !completed_cpu_interval && !private_armed) {
        warm_draw_source_frame=melee_web_match_source_frames();
        warm_draw_valid=1;
    }
#endif
    return !failed;
}
