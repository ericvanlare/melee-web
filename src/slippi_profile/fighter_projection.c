/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fighter_projection.h"
#include "gameplay_source_memory_runtime.h"
#include <melee/ft/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <stdlib.h>
#include <string.h>

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
    unsigned char flag, saved_flag, selected;
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
    if (!p) {
        p=calloc(1,sizeof(*p)); if(!p) return refuse();
        p->next=owners; owners=p;
    }
    p->fp=object;p->descriptor=descriptor;p->extent=extent;p->identity=identity;
    memcpy(p->screen,&p->fp->x2188,sizeof(p->screen));
    p->flag=p->fp->x221F_b0;p->selected=0;
    return 1;
}
int slippi_fighter_projection_retiring(void* object)
{
    if (!world || failed || drawing) return refuse();
    for(Projection* p=owners;p;p=p->next)
        if(p->fp==object && current(p)) return 1;
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
    for(Projection* p=owners;p;p=p->next) p->selected=current(p);
    if (!HSD_GObj_Entities) return refuse();
    for (HSD_GObj* g=HSD_GObj_Entities->fighters;g;g=g->next) {
        Projection* p;
        for(p=owners;p;p=p->next)
            if(p->fp==g->user_data && p->selected) break;
        if(!p) return refuse();
    }
    for(Projection* p=owners;p;p=p->next) if(p->selected) {
        memcpy(p->saved,&p->fp->x2188,sizeof(p->saved));
        p->saved_flag=p->fp->x221F_b0;
        memcpy(&p->fp->x2188,p->screen,sizeof(p->screen));
        p->fp->x221F_b0=p->flag;
    }
    drawing=1; return 1;
}
int slippi_fighter_projection_draw_end(void)
{
    if (!world || !drawing) return refuse();
    /* Lifecycle hooks refuse destruction/construction during the view. Check
     * every identity again before reading or restoring any source fields. */
    for(Projection* p=owners;p;p=p->next)
        if(p->selected && !current(p)) return refuse();
    for(Projection* p=owners;p;p=p->next) if(p->selected) {
        memcpy(p->screen,&p->fp->x2188,sizeof(p->screen));
        p->flag=p->fp->x221F_b0;
        memcpy(&p->fp->x2188,p->saved,sizeof(p->saved));
        /* Restore only this bit; other draw-authored flag bits stay visible. */
        p->fp->x221F_b0=p->saved_flag;p->selected=0;
    }
    drawing=0; return !failed;
}
