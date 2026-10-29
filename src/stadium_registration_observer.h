#ifndef MELEE_WEB_STADIUM_REGISTRATION_OBSERVER_H
#define MELEE_WEB_STADIUM_REGISTRATION_OBSERVER_H
#include <stddef.h>
#include <stdint.h>
#include "gameplay_rng_draw_owner.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Selected actual source callbacks, not all heap allocations or all writers. */
enum { MELEE_WEB_REGISTRATION_ROWS = 65536, MELEE_WEB_REGISTRATION_SLOTS = 8192 };
typedef enum {
    MW_REG_WORLD_BEGIN=1, MW_REG_WORLD_END=2,
    MW_REG_ALLOC_RETURN=3, MW_REG_FREE_ENTER=4, MW_REG_FREE_RETURN=5,
    MW_REG_SETUP_ENTER=6, MW_REG_SETUP_RETURN=7, MW_REG_LINK_RETURN=8,
    MW_REG_SCHED_UNLINK_RETURN=9, MW_REG_OWNER_UNLINK_RETURN=10,
    MW_REG_REMOVE_REQUEST=11, MW_REG_LINK_ENTER=12, MW_REG_SCHED_UNLINK_ENTER=13,
    MW_REG_OWNER_UNLINK_ENTER=14, MW_REG_REMOVE_RETURN=15
} MeleeWebRegistrationKind;
typedef struct {
    uint64_t world_generation, object_generation, gobj_generation, proc_generation;
    uintptr_t pool, object, gobj, proc, callback, previous, next, child;
    uint32_t sequence, kind, pool_kind, owner_role, classifier, p_link, s_link;
    uint32_t pool_size, deferred, residual_live, call_index, call_depth, snapshot_is_entry, neighbors_observed;
} MeleeWebRegistrationRow;
void melee_web_stadium_registration_bind(const MeleeWebRngDrawOwnerBindings*, const void* gobj_pool, const void* proc_pool);
int melee_web_stadium_registration_arm_checked(void);
int melee_web_stadium_registration_enabled(void);
void melee_web_stadium_registration_world_begin(uint64_t generation);
void melee_web_stadium_registration_world_end(uint64_t generation);
void melee_web_stadium_registration_alloc(const void* pool, void* object, uint32_t size);
void melee_web_stadium_registration_free(unsigned returning, const void* pool, void* object, uint32_t size);
void melee_web_stadium_registration_setup_enter(void* gobj, uintptr_t callback, unsigned priority, unsigned classifier, unsigned p_link);
uint32_t melee_web_stadium_registration_proc(unsigned kind, void* proc, void* gobj, uintptr_t callback,
    unsigned classifier, unsigned p_link, unsigned s_link, void* previous, void* next, void* child, unsigned deferred);
void melee_web_stadium_registration_proc_return(uint32_t token, unsigned kind,
    void* previous, void* next, void* child, unsigned snapshot_is_entry);
/* Browser reads only at an existing settled boundary. Seal succeeds only after
 * actual world end with no pending source calls, overflow or invalid joins.
 * Generations are observer-local actual ObjAlloc ordinals, never OS leases. */
int melee_web_stadium_registration_seal(void);
size_t melee_web_stadium_registration_count(void);
int melee_web_stadium_registration_read(size_t index, MeleeWebRegistrationRow* out);
const char* melee_web_stadium_registration_status_json(void);
const char* melee_web_stadium_registration_read_json(uint32_t index);
#ifdef __cplusplus
}
#endif
#endif
