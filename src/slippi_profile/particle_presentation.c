/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "particle_presentation_source.h"
#include "particle_pass_plan.h"
#include "gameplay_cpu_interval.h"
#include "gameplay_source_memory_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* Receipts are minted only by the real source constructors. No draw/capture
 * lookup can create one. Keep retired receipts until the world ends because a
 * checked CPU restore can make a captured incarnation current again. */
typedef struct Cell {
    struct Cell* next;
    void* object;
    HSD_ObjAllocData* pool;
    uint64_t serial;
    MeleeWebSourcePoolCellIdentity identity;
    unsigned live, captured, app;
    uint8_t virtual_key;
    const HSD_GObj* virtual_camera;
    int virtual_fresh;
    HSD_Particle* expected_next;
    HSD_psAppSRT* expected_app;
    uint32_t expected_kind;
    unsigned expected_link;
    float expected_size;
    HSD_psAppSRT expected_srt;
} Cell;
typedef struct AppFact {
    Cell* cell;
    HSD_psAppSRT value;
} AppFact;
typedef struct Pass {
    SlippiParticlePassVisit visit;
    SlippiParticlePreparedDispatch data;
    AppFact* apps;
    size_t app_count, app_capacity;
} Pass;
typedef struct Cache {
    s32 integers[2];
    f32 floats[GX_PROJECTION_SZ];
    GXColor color;
    Mtx matrix;
    const void* source;
    unsigned type;
} Cache;
static struct {
    uint64_t world, serial, anchor_serial, anchor_token, anchor_nonce;
    Cell* cells;
    HSD_ObjAllocData *particle_pool, *app_pool;
    size_t particle_extent, app_extent;
    Pass* passes;
    size_t count, cursor, prepare_index;
    unsigned failed, preparing, validating, drawing, dispatch, drawn;
    const HSD_GObj* camera;
    uint64_t interval;
    uint8_t frame;
    Cache caches[SLIPPI_PARTICLE_CACHE_COUNT];
    s32 matrix_slots[2];
    u32 tev[2];
    struct HSD_Fog* fog;
    const void *matrix_slots_source, *tev_source, *fog_source;
} state;

/* Existing failure latch is also the one-record diagnostic latch. Logging
 * does not replace error buffers or alter any guard/cleanup decision. */
static int failure_at(const char* function, unsigned line, const char* reason)
{
    const int first = !state.failed;
    state.failed = 1;
    if (first)
        fprintf(stderr, "Particle helper first failure: %s:%u: %.256s\n",
                function, line, reason);
    return 0;
}
#define fail() failure_at(__func__, __LINE__, "guard refused")
static int report(char* out, size_t size, const char* what)
{
    failure_at(__func__, __LINE__, what);
    if (out && size) snprintf(out, size, "%s", what);
    return 0;
}
uint32_t slippi_particle_presentation_failure(void) { return state.failed; }
static Cell* find(void* object, unsigned app)
{
    Cell* c;
    for (c = state.cells; c; c = c->next)
        if (c->live && c->object == object && c->app == app) return c;
    return NULL;
}
static int valid(Cell* c)
{
    return c && c->live && melee_web_source_memory_pool_cell_matches(
        c->object, c->pool, c->app ? state.app_extent : state.particle_extent,
        &c->identity);
}
static void discard(void)
{
    size_t i;
    for (i = 0; i < state.count; ++i) free(state.passes[i].apps);
    free(state.passes);
    state.passes = NULL;
    state.count = state.cursor = 0;
    state.camera = NULL;
    state.dispatch = state.drawn = 0;
}
int slippi_particle_presentation_world_begin(uint64_t world)
{
    if (!world || state.world || state.cells || state.drawing) return fail();
    state.world = world;
    state.failed = 0;
    state.serial = state.anchor_serial = state.anchor_token = 0;
    memset(state.caches, 0, sizeof(state.caches));
    state.matrix_slots_source = state.tev_source = state.fog_source = NULL;
    return 1;
}
int slippi_particle_presentation_world_end(uint64_t world)
{
    Cell *c, *next;
    if (state.world != world || state.drawing || state.preparing ||
        state.anchor_token || melee_web_cpu_interval_active()) return fail();
    discard();
    for (c = state.cells; c; c = next) { next = c->next; free(c); }
    state.cells = NULL;
    state.world = 0;
    return !state.failed;
}
int slippi_particle_presentation_source_mutation_allowed(void)
{
    /* Ordinary preparation before a CPU anchor keeps the original source path.
     * A known draw-time canonical writer must refuse BEFORE its first store. */
    return !state.drawing && !(state.failed && state.anchor_token) ? 1 : fail();
}
int slippi_particle_presentation_pool_reinitialization_allowed(void)
{
    return !state.drawing && !state.preparing && !state.anchor_token ? 1 : fail();
}
static int pool_init(HSD_ObjAllocData* p, size_t extent, unsigned align, int app)
{
    HSD_ObjAllocData** slot = app ? &state.app_pool : &state.particle_pool;
    size_t* bytes = app ? &state.app_extent : &state.particle_extent;
    if (!p || extent != (app ? sizeof(HSD_psAppSRT) : sizeof(HSD_Particle)) ||
        align != 4 || state.drawing || state.anchor_token) return fail();
    if (*slot && *slot != p) return fail();
    /* The actual unarmed source initializer replaces the pool's lifetime.
     * Do not carry formerly live cells into the new source free chain. */
    if (*slot == p) {
        Cell* c;
        for(c=state.cells;c;c=c->next) if(c->pool==p) c->live=0;
    }
    *slot = p; *bytes = extent;
    return 1;
}
int slippi_particle_presentation_pool_initialized(HSD_ObjAllocData* p,
                                                  size_t n, unsigned align)
{ return pool_init(p, n, align, 0); }
int slippi_particle_presentation_appsrt_pool_initialized(HSD_ObjAllocData* p,
                                                         size_t n, unsigned align)
{ return pool_init(p, n, align, 1); }
static int created(void* object, HSD_ObjAllocData* pool, unsigned app)
{
    Cell* c;
    if (!state.world) return 1; /* Unowned ordinary source path; never adopted later. */
    if (state.failed || state.drawing || !object ||
        pool != (app ? state.app_pool : state.particle_pool) ||
        find(object, app) || state.serial == UINT64_MAX) return fail();
    c = calloc(1, sizeof(*c));
    if (!c) return fail();
    if (!melee_web_source_memory_pool_cell_identity(
            object, pool, app ? state.app_extent : state.particle_extent,
            &c->identity)) { free(c); return fail(); }
    c->object = object; c->pool = pool; c->app = app;
    c->serial = ++state.serial; c->live = 1;
    c->next = state.cells; state.cells = c;
    return 1;
}
int slippi_particle_presentation_particle_created(HSD_Particle* p,
                                                  HSD_ObjAllocData* pool)
{ return created(p, pool, 0); }
int slippi_particle_presentation_appsrt_created(HSD_psAppSRT* p,
                                               HSD_ObjAllocData* pool)
{ return created(p, pool, 1); }
static int retiring(void* object, HSD_ObjAllocData* pool, unsigned app)
{
    Cell* c = find(object, app);
    if (!state.world) return 1;
    if (state.drawing || (state.failed && state.anchor_token) || !valid(c) || c->pool != pool)
        return fail();
    c->live = 0;
    return 1;
}
int slippi_particle_presentation_particle_retiring(HSD_Particle* p,
                                                   HSD_ObjAllocData* pool)
{ return retiring(p, pool, 0); }
int slippi_particle_presentation_appsrt_retiring(HSD_psAppSRT* p,
                                                HSD_ObjAllocData* pool)
{ return retiring(p, pool, 1); }
int slippi_particle_presentation_appsrt_attached(void* owner, HSD_psAppSRT* p,
                                                unsigned kind)
{
    if (!state.world) return 1;
    if (!owner || (kind != 0 && kind != 1) || state.drawing ||
        state.failed || !valid(find(p, 1))) return fail();
    return 1;
}
int slippi_particle_presentation_appsrt_detaching(void* owner, HSD_psAppSRT* p,
                                                 unsigned kind)
{ return slippi_particle_presentation_appsrt_attached(owner, p, kind); }

SlippiParticlePresentationAnchor slippi_particle_presentation_anchor_capture(void)
{
    SlippiParticlePresentationAnchor a = {0,0,0,0,0};
    Cell* c;
    uint64_t token;
    if (!state.world || state.failed || state.drawing ||
        state.anchor_nonce==UINT64_MAX || state.anchor_token) {
        fail(); return a;
    }
    for (c=state.cells;c;c=c->next) {
        if (c->live && !valid(c)) { fail(); return a; }
        if (c->live && (c->app ? a.appsrt_count : a.particle_count)==UINT32_MAX) {
            fail(); return (SlippiParticlePresentationAnchor){0,0,0,0,0};
        }
        if (c->live) { if(c->app) ++a.appsrt_count; else ++a.particle_count; }
    }
    token=++state.anchor_nonce;
    for(c=state.cells;c;c=c->next) c->captured=c->live;
    state.anchor_serial=state.serial; state.anchor_token=token;
    a.world=state.world; a.serial=state.anchor_serial; a.token=token;
    return a;
}
int slippi_particle_presentation_anchor_admit(SlippiParticlePresentationAnchor a)
{
    Cell* c;
    uint32_t particles=0, apps=0;
    if (state.failed || state.drawing || !a.world || a.world!=state.world ||
        a.serial!=state.anchor_serial || !a.token || a.token!=state.anchor_token ||
        (state.interval && !melee_web_cpu_interval_admit(state.interval))) return fail();
    /* Before source commit, current source may represent a later incarnation.
     * Captured receipts retain their original positive pool proof. Never read
     * a retired object here; checked commit admits the authoritative source rows. */
    for(c=state.cells;c;c=c->next) if(c->captured) {
        if(c->serial>a.serial) return fail();
        if(c->app) ++apps; else ++particles;
    }
    if(particles!=a.particle_count || apps!=a.appsrt_count) return fail();
    return 1;
}
void slippi_particle_presentation_anchor_disarm(SlippiParticlePresentationAnchor a)
{
    if (state.anchor_token && (a.world!=state.world || a.token!=state.anchor_token ||
        a.serial!=state.anchor_serial || state.drawing)) { fail(); return; }
    state.anchor_token=state.anchor_serial=0; state.interval=0;
}
int slippi_particle_presentation_reset_after_restore(SlippiParticlePresentationAnchor a)
{
    Cell* c;
    if(!slippi_particle_presentation_anchor_admit(a)) return 0;
    /* Admit every captured cell before changing any host receipt. */
    for(c=state.cells;c;c=c->next) if(c->captured &&
        !melee_web_source_memory_pool_cell_matches(c->object,c->pool,
            c->app?state.app_extent:state.particle_extent,&c->identity)) return fail();
    for(c=state.cells;c;c=c->next) c->live=c->captured;
    discard();
    memset(state.caches,0,sizeof(state.caches));
    state.matrix_slots_source=state.tev_source=state.fog_source=NULL;
    return 1;
}
int slippi_particle_presentation_interval_end(uint64_t world, uint64_t token)
{
    if(world!=state.world || state.drawing ||
       (state.interval && state.interval!=token)) return fail();
    discard(); state.anchor_token=state.anchor_serial=0; state.interval=0;
    return 1;
}

/* Called by the source bridge during complete, read-only pass preflight. */
size_t slippi_particle_presentation_particle_bound(void)
{
    Cell* c; size_t count=0;
    for(c=state.cells;c;c=c->next) if(c->live && !c->app) ++count;
    return count;
}
int slippi_particle_presentation_particle_require(HSD_Particle* p)
{ return valid(find(p,0)) ? 1 : fail(); }
int slippi_particle_presentation_app_require(HSD_psAppSRT* p)
{ return valid(find(p,1)) ? 1 : fail(); }
int slippi_particle_presentation_app_future(HSD_psAppSRT* p, unsigned key,
                                           const HSD_GObj* camera)
{
    Cell* c=find(p,1);
    if(!state.validating || !valid(c) || !key || key>255) return fail();
    if(c->virtual_key==key &&
       (!c->virtual_fresh || c->virtual_camera!=camera)) return fail();
    c->virtual_key=(uint8_t)key; c->virtual_fresh=1; c->virtual_camera=camera;
    return 1;
}
void slippi_particle_presentation_app_prepared(HSD_psAppSRT* p,
                                              unsigned key, int refreshed)
{
    Pass* pass;
    Cell* c=find(p,1);
    size_t i;
    (void)refreshed; /* Fresh/hit provenance was checked for the complete plan. */
    if(!state.preparing || !valid(c) || key!=slippi_particle_source_frame_key()) { fail(); return; }
    pass=&state.passes[state.prepare_index];
    for(i=0;i<pass->app_count;++i) if(pass->apps[i].cell==c) return;
    if(pass->app_count==pass->app_capacity) { fail(); return; }
    pass->apps[pass->app_count].cell=c;
    pass->apps[pass->app_count++].value=*p;
}
int slippi_particle_presentation_record_prepared(
    const SlippiParticlePassVisit* visit,
    const SlippiParticlePreparedDispatch* prepared)
{
    if(!state.preparing || !visit || !prepared ||
       visit!=&state.passes[state.prepare_index].visit) return fail();
    state.passes[state.prepare_index].data=*prepared;
    return !state.failed;
}
static void remember_source(void)
{
    Cell* c;
    for(c=state.cells;c;c=c->next) if(c->live) {
        if(c->app) c->expected_srt=*(HSD_psAppSRT*)c->object;
        else {
            HSD_Particle* p=c->object;
            c->expected_next=p->next; c->expected_app=p->appsrt;
            c->expected_kind=p->kind; c->expected_link=p->linkNo;
            memcpy(&c->expected_size,&p->size,sizeof(float));
        }
    }
}
static int source_unchanged(void)
{
    Cell* c;
    for(c=state.cells;c;c=c->next) if(c->live) {
        if(!valid(c)) return 0;
        if(c->app) {
            if(memcmp(&c->expected_srt,c->object,sizeof(HSD_psAppSRT))) return 0;
        } else {
            HSD_Particle* p=c->object;
            if(p->next!=c->expected_next || p->appsrt!=c->expected_app ||
               p->kind!=c->expected_kind || p->linkNo!=c->expected_link ||
               memcmp(&p->size,&c->expected_size,sizeof(float))) return 0;
        }
    }
    return 1;
}
int slippi_particle_presentation_prepare(const SlippiParticlePassVisit* visits,
    size_t count,uint64_t world,uint64_t interval,char* error,size_t size)
{
    Cell* c;
    size_t i, app_count=0;
    uint8_t key;
    if(!interval) return !state.drawing && !state.failed;
    if(state.failed || state.world!=world || state.drawing || state.preparing ||
       !state.anchor_token || (state.interval && interval!=state.interval) ||
       !melee_web_cpu_interval_admit(interval) ||
       (count && !visits) || count>SIZE_MAX/sizeof(Pass))
        return report(error,size,"Particle preparation owner/interval refused");
    for(c=state.cells;c;c=c->next) if(c->live) {
        if(!valid(c)) return report(error,size,"Particle pool lifetime changed");
        if(c->app) ++app_count;
    }
    discard();
    state.passes=count?calloc(count,sizeof(Pass)):NULL;
    if(count && !state.passes) return report(error,size,"Particle pass allocation failed");
    state.count=count;
    for(i=0;i<count;++i) {
        state.passes[i].visit=visits[i];
        if(app_count>SIZE_MAX/sizeof(AppFact)) return report(error,size,"AppSRT capacity overflow");
        state.passes[i].apps=app_count?calloc(app_count,sizeof(AppFact)):NULL;
        if(app_count && !state.passes[i].apps) return report(error,size,"AppSRT pass allocation failed");
        state.passes[i].app_capacity=app_count;
    }
    for(c=state.cells;c;c=c->next) if(c->live && c->app) {
        c->virtual_key=((HSD_psAppSRT*)c->object)->frameNum;
        c->virtual_fresh=0; c->virtual_camera=NULL;
    }
    key=slippi_particle_source_frame_key();
    state.validating=1;
    for(i=0;i<count;++i) {
        const SlippiParticlePassVisit* v=&state.passes[i].visit;
        if(v->code==0) key=key<255?(uint8_t)(key+1):1;
        if(!slippi_particle_source_preflight_visit(v,key)) break;
    }
    state.validating=0;
    if(i!=count || state.failed) return report(error,size,"Particle source pass preflight refused");
    state.preparing=1;
    for(i=0;i<count;++i) {
        state.prepare_index=i;
        if(!slippi_particle_source_prepare_visit(&state.passes[i].visit)) break;
    }
    state.preparing=0;
    if(i!=count || state.failed) return report(error,size,"Particle source preparation failed");
    remember_source();
    state.interval=interval; state.cursor=state.drawn=0;
    return 1;
}
int slippi_particle_presentation_preparing(void) { return state.preparing; }
int slippi_particle_presentation_dispatch_active(void) { return state.dispatch; }
int slippi_particle_presentation_draw_begin(uint64_t world,uint64_t token,
                                           char* error,size_t size)
{
    Cell* c;
    if(!token) return !state.failed && !state.drawing;
    if(state.failed || state.world!=world || state.interval!=token ||
       !melee_web_cpu_interval_admit(token) || state.drawing || state.drawn)
        return report(error,size,"Particle draw plan missing/repeated/stale");
    for(c=state.cells;c;c=c->next) if(c->live && !valid(c))
        return report(error,size,"Particle draw lifetime changed");
    if(!source_unchanged()) return report(error,size,"Particle source changed after preparation");
    state.drawing=1; state.cursor=0; state.camera=NULL;
    return 1;
}
int slippi_particle_presentation_camera_begin(const HSD_GObj* camera,
                                               char* error,size_t size)
{
    if(!state.drawing) return !state.failed;
    if(state.camera || state.dispatch || !camera)
        return report(error,size,"Particle camera nesting refused");
    state.camera=camera; return 1;
}
int slippi_particle_presentation_camera_end(const HSD_GObj* camera,
                                             char* error,size_t size)
{
    if(!state.drawing) return !state.failed;
    if(state.camera!=camera || state.dispatch ||
       (state.cursor<state.count && state.passes[state.cursor].visit.camera==camera))
        return report(error,size,"Particle camera omitted a prepared visit");
    state.camera=NULL; return 1;
}
SlippiParticleDispatchResult slippi_particle_presentation_dispatch_begin(
    const HSD_GObj* manager,unsigned code,unsigned gx,unsigned mask,
    char* error,size_t size)
{
    Pass* p;
    if(!state.drawing) {
        if(state.failed || melee_web_cpu_interval_active()) {
            report(error,size,"Particle callback outside admitted draw");
            return SLIPPI_PARTICLE_DISPATCH_REFUSED;
        }
        return SLIPPI_PARTICLE_DISPATCH_SOURCE;
    }
    if(state.failed || state.dispatch || state.cursor>=state.count) goto bad;
    p=&state.passes[state.cursor];
    if(p->visit.camera!=state.camera || p->visit.manager!=manager ||
       p->visit.code!=code || p->visit.gx_link!=gx || p->visit.target_mask!=mask) goto bad;
    state.dispatch=1; state.frame=p->data.key;
    return SLIPPI_PARTICLE_DISPATCH_ROUTED;
bad:
    report(error,size,"Particle callback differs from prepared source visit");
    return SLIPPI_PARTICLE_DISPATCH_REFUSED;
}
int slippi_particle_presentation_dispatch_end(char* error,size_t size)
{
    if(!state.drawing) return !state.failed;
    if(!state.dispatch) return report(error,size,"Particle dispatch end without begin");
    state.dispatch=0; ++state.cursor; return !state.failed;
}
int slippi_particle_presentation_draw_end(char* error,size_t size)
{
    int ok;
    if(!state.drawing) return !state.failed;
    ok=!state.failed && !state.dispatch && !state.camera &&
       state.cursor==state.count && melee_web_cpu_interval_admit(state.interval) &&
       source_unchanged();
    state.drawing=state.dispatch=0; state.camera=NULL; state.drawn=1;
    if(!ok) return report(error,size,"Particle draw did not consume exact source schedule");
    return 1;
}
SlippiParticleDispatchResult slippi_particle_presentation_render_data(
    unsigned target,unsigned code,uint8_t* key,HSD_Particle** sorted,
    HSD_Particle** nonedge)
{
    Pass* p;
    if(!state.drawing) return SLIPPI_PARTICLE_DISPATCH_SOURCE;
    if(state.failed || !state.dispatch || target>=16 || !key || !sorted || !nonedge ||
       code!=state.passes[state.cursor].visit.code ||
       !(state.passes[state.cursor].visit.target_mask&(1u<<target))) {
        fail(); return SLIPPI_PARTICLE_DISPATCH_REFUSED;
    }
    p=&state.passes[state.cursor]; *key=p->data.key;
    *sorted=p->data.sorted[target]; *nonedge=p->data.nonedge[target];
    return SLIPPI_PARTICLE_DISPATCH_ROUTED;
}
HSD_psAppSRT* slippi_particle_presentation_app_srt_view(HSD_psAppSRT* canonical,
                                                      HSD_psAppSRT* scratch)
{
    Pass* p;
    Cell* c;
    size_t i;
    if(!state.drawing) return canonical;
    if(!state.dispatch || state.failed || !scratch || !valid(c=find(canonical,1))) {
        fail(); return NULL;
    }
    p=&state.passes[state.cursor];
    for(i=0;i<p->app_count;++i) if(p->apps[i].cell==c) {
        *scratch=p->apps[i].value;
        /* Only the camera projection is recomputed by the actual leaf. The
         * canonical world dependency has already run in original pass order. */
        scratch->status=PS_APPSTATUS_STILL; scratch->frameNum=0;
        return scratch;
    }
    fail(); return NULL;
}

static Cache* cache(unsigned slot,unsigned type,const void* source)
{
    Cache* c;
    if(slot>=SLIPPI_PARTICLE_CACHE_COUNT || !source || !state.drawing || state.failed) {
        fail(); return NULL;
    }
    c=&state.caches[slot];
    if(c->source && (c->source!=source || c->type!=type)) { fail(); return NULL; }
    if(!c->source) {
        c->source=source;c->type=type;
        switch(type) {
        case 1: memcpy(c->integers,source,sizeof(s32));break;
        case 2: memcpy(c->floats,source,sizeof(f32));break;
        case 3: memcpy(&c->color,source,sizeof(GXColor));break;
        case 4: memcpy(c->matrix,source,sizeof(Mtx));break;
        case 5: memcpy(c->floats,source,GX_PROJECTION_SZ*sizeof(f32));break;
        default: fail();return NULL;
        }
    }
    return c;
}
uint8_t* slippi_particle_presentation_frame_slot(uint8_t* fallback)
{ return state.drawing?&state.frame:fallback; }
s32* slippi_particle_presentation_s32_slot(SlippiParticlePresentationCacheSlot s,s32* f)
{ Cache*c;if(!state.drawing)return f;c=cache(s,1,f);return c?c->integers:NULL; }
f32* slippi_particle_presentation_f32_slot(SlippiParticlePresentationCacheSlot s,f32* f)
{ Cache*c;if(!state.drawing)return f;c=cache(s,2,f);return c?c->floats:NULL; }
GXColor* slippi_particle_presentation_color_slot(SlippiParticlePresentationCacheSlot s,GXColor* f)
{ Cache*c;if(!state.drawing)return f;c=cache(s,3,f);return c?&c->color:NULL; }
Mtx* slippi_particle_presentation_mtx_slot(SlippiParticlePresentationCacheSlot s,Mtx* f)
{ Cache*c;if(!state.drawing)return f;c=cache(s,4,f);return c?&c->matrix:NULL; }
f32* slippi_particle_presentation_projection_slot(f32* f)
{ Cache*c;if(!state.drawing)return f;c=cache(SLIPPI_PARTICLE_CACHE_PRJ,5,f);return c?c->floats:NULL; }
s32 (*slippi_particle_presentation_slot_array2(s32 (*f)[2]))[2]
{
    if(!state.drawing)return f;
    if(state.matrix_slots_source && state.matrix_slots_source!=f) {fail();return NULL;}
    if(!state.matrix_slots_source){state.matrix_slots_source=f;memcpy(state.matrix_slots,f,sizeof(state.matrix_slots));}
    return &state.matrix_slots;
}
u32 (*slippi_particle_presentation_tev_slots(u32 (*f)[2]))[2]
{
    if(!state.drawing)return f;
    if(state.tev_source && state.tev_source!=f){fail();return NULL;}
    if(!state.tev_source){state.tev_source=f;memcpy(state.tev,f,sizeof(state.tev));}
    return &state.tev;
}
struct HSD_Fog** slippi_particle_presentation_fog_slot(struct HSD_Fog** f)
{
    if(!state.drawing)return f;
    if(state.fog_source && state.fog_source!=f){fail();return NULL;}
    if(!state.fog_source){state.fog_source=f;state.fog=*f;}
    return &state.fog;
}
