#include "stadium_registration_observer.h"
#include "gameplay_source_memory_runtime.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define API EMSCRIPTEN_KEEPALIVE
#else
#define API
#endif

typedef struct {
    uintptr_t object, pool;
    uint64_t generation;
    uint8_t live, pending_free;
    uint32_t free_token;
} Slot;
static MeleeWebRegistrationRow rows[MELEE_WEB_REGISTRATION_ROWS];
static Slot slots[MELEE_WEB_REGISTRATION_SLOTS];
static const MeleeWebRngDrawOwnerBindings* bindings;
static uintptr_t gobj_pool, proc_pool;
static uint64_t world, generation;
static size_t count;
static unsigned armed, sealed, invalid, overflow, worlds;
static size_t unselected_proc_hook_calls;
static uintptr_t pending_gobj, pending_callback;
static unsigned pending_priority;
static uint32_t call_stack[64];
static size_t call_depth;
static char json_row[1536], json_status[512];
_Static_assert(sizeof(rows)+sizeof(slots)+sizeof(json_row)+sizeof(json_status) <= 16*1024*1024,
               "registration observer hard memory budget");
static void fail(void) { invalid=1; }
static int active(void) {
    if (!armed || sealed || invalid) return 0;
    MeleeWebSourceMemoryContext c={0};
    if (!world || melee_web_source_memory_context_read(&c)!=MELEE_WEB_SOURCE_MEMORY_READ_OK ||
        c.world_generation!=world) { fail(); return 0; }
    return 1;
}
static unsigned pool_kind(uintptr_t p) { return p==gobj_pool ? 1 : p==proc_pool ? 2 : 0; }
static unsigned role(uintptr_t callback) {
    if (!bindings) return 0;
    if (callback==(uintptr_t)bindings->stadium_callback) return 1;
    if (callback==(uintptr_t)bindings->particle_callback) return 2;
    if (callback==(uintptr_t)bindings->fighter_callback) return 3;
    return 0;
}
static Slot* find(uintptr_t object, uintptr_t pool) {
    for(size_t i=0;i<MELEE_WEB_REGISTRATION_SLOTS;++i)
        if(slots[i].live && slots[i].object==object && slots[i].pool==pool) return &slots[i];
    return NULL;
}
static MeleeWebRegistrationRow* append(unsigned kind) {
    if(invalid) return NULL;
    if(count==MELEE_WEB_REGISTRATION_ROWS) { overflow=1; fail(); return NULL; }
    MeleeWebRegistrationRow* r=&rows[count];
    memset(r,0,sizeof(*r)); r->sequence=(uint32_t)count; r->kind=kind;
    r->world_generation=world;r->call_index=UINT32_MAX;r->call_depth=(uint32_t)call_depth; ++count; return r;
}
void melee_web_stadium_registration_bind(const MeleeWebRngDrawOwnerBindings* b,const void* gp,const void* pp) {
    if(bindings==b && gobj_pool==(uintptr_t)gp && proc_pool==(uintptr_t)pp && b) return;
    if(bindings || armed || !b || !gp || !pp || gp==pp || !b->stadium_callback ||
       !b->particle_callback || !b->fighter_callback ||
       b->stadium_callback==b->particle_callback || b->stadium_callback==b->fighter_callback ||
       b->particle_callback==b->fighter_callback) { fail(); return; }
    bindings=b;gobj_pool=(uintptr_t)gp;proc_pool=(uintptr_t)pp;
}
int melee_web_stadium_registration_arm_checked(void) {
    MeleeWebSourceMemoryContext c={0};
    if(armed || invalid || !bindings ||
       melee_web_source_memory_context_read(&c)!=MELEE_WEB_SOURCE_MEMORY_READ_INACTIVE) { fail();return 0; }
    armed=1; return 1;
}
int melee_web_stadium_registration_enabled(void) {return armed && !sealed && !invalid;}
void melee_web_stadium_registration_world_begin(uint64_t g) {
    if(!armed || sealed) return;
    if(world || !g || pending_gobj) { fail();return; }
    world=g;
    if(!active()) return;
    ++worlds; append(MW_REG_WORLD_BEGIN);
}
void melee_web_stadium_registration_world_end(uint64_t g) {
    if(!armed || sealed) return;
    if(!world || world!=g || pending_gobj || call_depth) { fail();return; }
    unsigned residual=0;
    for(size_t i=0;i<MELEE_WEB_REGISTRATION_SLOTS;++i) {
        if(slots[i].pending_free) fail();
        if(slots[i].live) ++residual;
    }
    if(active()) { MeleeWebRegistrationRow* r=append(MW_REG_WORLD_END);if(r)r->residual_live=residual; }
    /* Actual source-context retirement forgets pools; this is not ObjFree. */
    memset(slots,0,sizeof(slots));world=0;
}
void melee_web_stadium_registration_alloc(const void* p,void* object,uint32_t size) {
    if(!armed || !pool_kind((uintptr_t)p)) return;
    if(!active()) return;
    Slot* slot=NULL;
    if(object) {
        if(find((uintptr_t)object,(uintptr_t)p) || !size || generation==UINT64_MAX) { fail();return; }
        for(size_t i=0;i<MELEE_WEB_REGISTRATION_SLOTS;++i) if(!slots[i].live) { slot=&slots[i];break; }
        if(!slot) { overflow=1;fail();return; }
        *slot=(Slot){(uintptr_t)object,(uintptr_t)p,++generation,1,0,UINT32_MAX};
    }
    MeleeWebRegistrationRow* r=append(MW_REG_ALLOC_RETURN);
    if(r) { r->pool=(uintptr_t)p;r->pool_kind=pool_kind((uintptr_t)p);r->object=(uintptr_t)object;
        r->pool_size=size;r->object_generation=slot?slot->generation:0; }
}
void melee_web_stadium_registration_free(unsigned returning,const void* p,void* object,uint32_t size) {
    if(!armed || !pool_kind((uintptr_t)p)) return;
    if(!active()) return;
    Slot* slot=find((uintptr_t)object,(uintptr_t)p);
    if(!slot || slot->pending_free!=(returning!=0)) { fail();return; }
    MeleeWebRegistrationRow* r=append(returning?MW_REG_FREE_RETURN:MW_REG_FREE_ENTER);
    if(r) {r->pool=(uintptr_t)p;r->pool_kind=pool_kind((uintptr_t)p);r->object=(uintptr_t)object;
        r->pool_size=size;r->object_generation=slot->generation;}
    if(r) {
        if(returning) {
            if(!call_depth || call_stack[call_depth-1]!=slot->free_token) {fail();return;}
            r->call_index=slot->free_token;r->call_depth=(uint32_t)--call_depth;
        } else {
            if(call_depth==64) {overflow=1;fail();return;}
            r->call_index=r->sequence;slot->free_token=r->sequence;call_stack[call_depth++]=r->sequence;
        }
    }
    if(returning) {slot->live=0;slot->pending_free=0;} else slot->pending_free=1;
}
void melee_web_stadium_registration_setup_enter(void* gobj,uintptr_t callback,unsigned priority,unsigned classifier,unsigned p_link) {
    if(!armed || sealed) return;
    if(!role(callback)) {++unselected_proc_hook_calls;return;}
    if(!active()) return;
    Slot* g=find((uintptr_t)gobj,gobj_pool);
    if(!g || g->pending_free || pending_gobj) { fail();return; }
    pending_gobj=(uintptr_t)gobj;pending_callback=callback;pending_priority=priority;
    MeleeWebRegistrationRow* r=append(MW_REG_SETUP_ENTER);
    if(r) {r->gobj=(uintptr_t)gobj;r->callback=callback;r->gobj_generation=g->generation;
        r->owner_role=role(callback);r->classifier=classifier;r->p_link=p_link;r->s_link=priority;
        if(call_depth==64) {overflow=1;fail();return;}
        r->call_index=r->sequence;call_stack[call_depth++]=r->sequence;}
}
uint32_t melee_web_stadium_registration_proc(unsigned kind,void* proc,void* gobj,uintptr_t callback,
    unsigned classifier,unsigned p_link,unsigned s_link,void* previous,void* next,void* child,unsigned deferred) {
    if(!armed || sealed) return UINT32_MAX;
    if(!role(callback)) {++unselected_proc_hook_calls;return UINT32_MAX;}
    if(!active()) return UINT32_MAX;
    Slot* g=find((uintptr_t)gobj,gobj_pool);Slot* p=find((uintptr_t)proc,proc_pool);
    if(!g || !p || g->pending_free || p->pending_free ||
       (kind!=MW_REG_SETUP_RETURN && kind!=MW_REG_LINK_ENTER && kind!=MW_REG_SCHED_UNLINK_ENTER &&
        kind!=MW_REG_OWNER_UNLINK_ENTER && kind!=MW_REG_REMOVE_REQUEST) ||
       (kind==MW_REG_SETUP_RETURN &&
       (pending_gobj!=(uintptr_t)gobj || pending_callback!=callback || pending_priority!=s_link))) {
        fail();return UINT32_MAX;
    }
    MeleeWebRegistrationRow* r=append(kind);
    if(!r) return UINT32_MAX;
    r->gobj=(uintptr_t)gobj;r->proc=(uintptr_t)proc;r->callback=callback;
    r->gobj_generation=g->generation;r->proc_generation=p->generation;r->owner_role=role(callback);
    r->classifier=classifier;r->p_link=p_link;r->s_link=s_link;r->previous=(uintptr_t)previous;
    r->next=(uintptr_t)next;r->child=(uintptr_t)child;r->deferred=deferred;
    r->neighbors_observed=(kind==MW_REG_SETUP_RETURN);
    if(kind==MW_REG_SETUP_RETURN) {
        if(!call_depth || rows[call_stack[call_depth-1]].kind!=MW_REG_SETUP_ENTER) {fail();return UINT32_MAX;}
        r->call_index=call_stack[--call_depth];r->call_depth=(uint32_t)call_depth;
        pending_gobj=pending_callback=0;pending_priority=0;
    } else {
        if(call_depth==64) {overflow=1;fail();return UINT32_MAX;}
        r->call_index=r->sequence;call_stack[call_depth++]=r->sequence;
    }
    return r->sequence;
}
void melee_web_stadium_registration_proc_return(uint32_t token,unsigned kind,
    void* previous,void* next,void* child,unsigned snapshot_is_entry) {
    if(!armed || token==UINT32_MAX) return;
    if(!active()) return;
    if(token>=count || !call_depth || call_stack[call_depth-1]!=token) {fail();return;}
    MeleeWebRegistrationRow entry=rows[token];
    unsigned expected=entry.kind==MW_REG_LINK_ENTER?MW_REG_LINK_RETURN:
        entry.kind==MW_REG_SCHED_UNLINK_ENTER?MW_REG_SCHED_UNLINK_RETURN:
        entry.kind==MW_REG_OWNER_UNLINK_ENTER?MW_REG_OWNER_UNLINK_RETURN:
        entry.kind==MW_REG_REMOVE_REQUEST?MW_REG_REMOVE_RETURN:0;
    if(!expected || kind!=expected || snapshot_is_entry!=(kind==MW_REG_REMOVE_RETURN)) {fail();return;}
    MeleeWebRegistrationRow* r=append(kind);
    if(!r) return;
    uint32_t seq=r->sequence;*r=entry;r->sequence=seq;r->kind=kind;
    r->call_index=token;r->call_depth=(uint32_t)--call_depth;r->snapshot_is_entry=snapshot_is_entry;
    if(!snapshot_is_entry) {r->neighbors_observed=1;r->previous=(uintptr_t)previous;r->next=(uintptr_t)next;r->child=(uintptr_t)child;}
}
API int melee_web_stadium_registration_seal(void) {
    if(!armed || sealed || invalid || world || pending_gobj || call_depth || !worlds) {fail();return 0;}
    sealed=1;return 1;
}
size_t melee_web_stadium_registration_count(void) {return count;}
int melee_web_stadium_registration_read(size_t index,MeleeWebRegistrationRow* out) {
    if(!out || index>=count) return 0;
    *out=rows[index];return 1;
}
API const char* melee_web_stadium_registration_status_json(void) {
    snprintf(json_status,sizeof(json_status),"{\"schema\":\"melee-web-source-registration-status-v1\",\"armed\":%s,\"sealed\":%s,\"invalid\":%s,\"overflow\":%s,\"count\":%zu,\"worlds\":%u,\"active_world\":\"%" PRIu64 "\",\"pending_setup\":%s,\"row_capacity\":%u,\"slot_capacity\":%u,\"storage_bytes\":%zu,\"unselected_proc_hook_calls\":%zu}",armed?"true":"false",sealed?"true":"false",invalid?"true":"false",overflow?"true":"false",count,worlds,world,pending_gobj?"true":"false",MELEE_WEB_REGISTRATION_ROWS,MELEE_WEB_REGISTRATION_SLOTS,sizeof(rows)+sizeof(slots),unselected_proc_hook_calls);
    return json_status;
}
API const char* melee_web_stadium_registration_read_json(uint32_t index) {
    MeleeWebRegistrationRow r;
    if(!melee_web_stadium_registration_read(index,&r)) return NULL;
    int n=snprintf(json_row,sizeof(json_row),"{\"schema\":\"melee-web-source-registration-row-v1\",\"sequence\":%u,\"kind\":%u,\"world_generation\":\"%" PRIu64 "\",\"object_generation\":\"%" PRIu64 "\",\"gobj_generation\":\"%" PRIu64 "\",\"proc_generation\":\"%" PRIu64 "\",\"pool\":\"0x%" PRIxPTR "\",\"object\":\"0x%" PRIxPTR "\",\"gobj\":\"0x%" PRIxPTR "\",\"proc\":\"0x%" PRIxPTR "\",\"callback\":\"0x%" PRIxPTR "\",\"previous\":\"0x%" PRIxPTR "\",\"next\":\"0x%" PRIxPTR "\",\"child\":\"0x%" PRIxPTR "\",\"pool_kind\":%u,\"owner_role\":%u,\"classifier\":%u,\"p_link\":%u,\"s_link\":%u,\"pool_size\":%u,\"deferred\":%u,\"residual_live\":%u,\"call_index\":%u,\"call_depth\":%u,\"snapshot_is_entry\":%u,\"neighbors_observed\":%u}",r.sequence,r.kind,r.world_generation,r.object_generation,r.gobj_generation,r.proc_generation,r.pool,r.object,r.gobj,r.proc,r.callback,r.previous,r.next,r.child,r.pool_kind,r.owner_role,r.classifier,r.p_link,r.s_link,r.pool_size,r.deferred,r.residual_live,r.call_index,r.call_depth,r.snapshot_is_entry,r.neighbors_observed);
    if(n<0 || (size_t)n>=sizeof(json_row)) {fail();return NULL;}return json_row;
}
