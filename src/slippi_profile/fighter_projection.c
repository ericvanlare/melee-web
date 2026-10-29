/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fighter_projection.h"
#include "gameplay_source_memory_runtime.h"
#include <melee/ft/types.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/list.h>
#include <sysdolphin/baselib/shadow.h>
#include <sysdolphin/baselib/tobj.h>
#include <sysdolphin/baselib/wobj.h>
#include <stdlib.h>
#include <string.h>

#define SHADOW_LB_VALUE_MASK 0x1Du
#define SHADOW_WOBJ_VALUE_MASK 0x3u
#define SHADOW_COBJ_VIEW_VALUE_MASK 0xC0000000u
/* lbShadow_8000F38C clears the list, then adds the Fighter root and at most
 * its one accessory root. This is the source path's exact bound. */
#define SHADOW_MEMBER_LIMIT 2u

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
    unsigned char flag, saved_flag, selected, listed;
    ShadowTuple shadow;
    ShadowValues shadow_values, saved_shadow_values;
    ShadowMember draw_members[SHADOW_MEMBER_LIMIT];
    unsigned draw_member_count;
    unsigned char draw_shadow_active;
    unsigned char shadow_bound, shadow_retired;
} Projection;
static Projection* owners;
static uint64_t world;
static int drawing, failed;

static int refuse(void) { failed = 1; return 0; }
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
static int shadow_tuple_matches(const Projection* p)
{
    const ShadowTuple* t = &p->shadow;
    if (!p->shadow_bound || p->shadow_retired ||
        &p->fp->x20A4 != t->lb || p->fp->x20A4.shadow != t->shadow ||
        t->shadow->camera != t->camera || t->shadow->texture != t->texture ||
        t->camera->eyepos != t->eye || t->camera->interest != t->interest ||
        t->camera->proj_mtx != NULL ||
        t->texture->imagedesc != t->image ||
        tobj_coord(t->texture) != TEX_COORD_SHADOW ||
        t->image->image_ptr != t->image_pixels ||
        t->image->width != t->image_width ||
        t->image->height != t->image_height ||
        t->image->format != t->image_format ||
        t->image->mipmap != t->image_mipmap ||
        memcmp(&t->shadow->scaleS, t->scale_s, sizeof(float)) != 0 ||
        memcmp(&t->shadow->scaleT, t->scale_t, sizeof(float)) != 0 ||
        memcmp(&t->shadow->transS, t->translate_s, sizeof(float)) != 0 ||
        memcmp(&t->shadow->transT, t->translate_t, sizeof(float)) != 0)
        return 0;
    return 1;
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
    for (node = p->shadow.shadow->objects; node; node = node->next) {
        unsigned i = p->draw_member_count;
        if (i == SHADOW_MEMBER_LIMIT) return 0;
        p->draw_members[i].object = node->data;
        p->draw_member_count = i + 1;
    }
    p->draw_shadow_active = p->shadow.shadow->active;
    return 1;
}

static int shadow_members_unchanged(const Projection* p)
{
    HSD_SList* node = p->shadow.shadow->objects;
    unsigned i;
    for (i = 0; i < p->draw_member_count; ++i) {
        if (!node || node->data != p->draw_members[i].object)
            return 0;
        node = node->next;
    }
    return node == NULL && p->shadow.shadow->active == p->draw_shadow_active;
}
int slippi_fighter_projection_world_begin(uint64_t w)
{
    if (!w || world || owners || drawing) return refuse();
    world=w; failed=0; return 1;
}
int slippi_fighter_projection_world_end(uint64_t w)
{
    if (!w || w!=world || drawing) return refuse();
    while (owners) { Projection* p=owners; owners=p->next; free(p); }
    world=0; failed=0; return 1;
}
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
        if (!p->shadow_bound || p->shadow_retired || !shadow_tuple_matches(p))
            return refuse();
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
        if (!p->shadow_bound || p->shadow_retired || p->shadow.shadow != shadow ||
            !current(p) || !shadow_tuple_matches(p))
            return refuse();
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
    /* Complete provenance preflight before any source field is swapped. Stale
     * lifetime receipts are inert, never dereferenced or adopted. */
    for(Projection* p=owners;p;p=p->next) {
        p->selected=current(p);
        p->listed=0;
    }
    if (!HSD_GObj_Entities) return refuse();
    for (HSD_GObj* g=HSD_GObj_Entities->fighters;g;g=g->next) {
        Projection* p;
        for(p=owners;p;p=p->next)
            if(p->fp==g->user_data && p->selected) break;
        if(!p || p->listed || !shadow_tuple_matches(p)) return refuse();
        p->listed=1;
    }
    for(Projection* p=owners;p;p=p->next)
        if(p->selected && !p->listed) return refuse();
    for(Projection* p=owners;p;p=p->next) if(p->selected) {
        read_shadow_values(p, &p->saved_shadow_values);
        if (!capture_shadow_members(p)) return refuse();
    }
    for(Projection* p=owners;p;p=p->next) if(p->selected) {
        memcpy(p->saved,&p->fp->x2188,sizeof(p->saved));
        p->saved_flag=p->fp->x221F_b0;
        memcpy(&p->fp->x2188,p->screen,sizeof(p->screen));
        p->fp->x221F_b0=p->flag;
        write_shadow_values(p, &p->shadow_values);
    }
    drawing=1; return 1;
}
int slippi_fighter_projection_draw_end(void)
{
    if (!world || !drawing) return refuse();
    /* Lifecycle hooks refuse destruction/construction during the view. Check
     * every identity again before reading or restoring any source fields. */
    for(Projection* p=owners;p;p=p->next)
        if(p->selected && (!current(p) || !shadow_tuple_matches(p))) return refuse();
    int topology_changed=0;
    for(Projection* p=owners;p;p=p->next) if(p->selected) {
        memcpy(p->screen,&p->fp->x2188,sizeof(p->screen));
        p->flag=p->fp->x221F_b0;
        read_shadow_values(p, &p->shadow_values);
        if (!shadow_members_unchanged(p)) topology_changed=1;
        memcpy(&p->fp->x2188,p->saved,sizeof(p->saved));
        /* Restore only this bit; other draw-authored flag bits stay visible. */
        p->fp->x221F_b0=p->saved_flag;p->selected=0;
        write_shadow_values(p, &p->saved_shadow_values);
    }
    drawing=0;
    if (topology_changed) return refuse();
    return !failed;
}
