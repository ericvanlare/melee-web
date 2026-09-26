#ifndef MELEE_WEB_GAMEPLAY_COMMON_CONTEXT_H
#define MELEE_WEB_GAMEPLAY_COMMON_CONTEXT_H
#include "common_tables.h"
#include "hsd_native_joint.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MELEE_WEB_COMMON_RUNTIME_MASK (MELEE_WEB_COMMON_STATIC_ROOT_MASK | 1u | (1u << 16) | (1u << 20) | (1u << 22))
typedef struct MeleeWebCommonContext MeleeWebCommonContext;
/* Copies all supported scalars/tables and root20 descriptors plus raw payloads.
 * The resulting owner is independent of the input archive and model lifetime. */
MeleeWebCommonContext* melee_web_common_context_create(const MeleeWebCommonScalars*,
    const MeleeWebCommonTables*, const MeleeWebNativeGraph*, char*, size_t);
/* Borrows fully hydrated source ColorOverlay tables through context destruction.
 * Call before attach; common counts are source123 and6. */
typedef struct MeleeWebColorRow {void* program; uint8_t priority,layer;} MeleeWebColorRow;
int melee_web_common_context_set_color_tables(MeleeWebCommonContext*,const MeleeWebColorRow*,const MeleeWebColorRow*,char*,size_t);
/* Publishes a checked native HSD_Joint descriptor for Fighter_804D6514.
 * The context borrows the descriptor until destroy; the caller owns the
 * MeleeWebNativeJoint handle and must keep it alive through source teardown. */
int melee_web_common_context_set_root16(MeleeWebCommonContext*,void* joint,char*,size_t);
/* Borrows checked respawn platform descriptors until context destruction. */
int melee_web_common_context_set_respawn(MeleeWebCommonContext*,void* joint,void* animation,char*,size_t);
int melee_web_common_context_attach(MeleeWebCommonContext*, char*, size_t);
int melee_web_common_context_require(MeleeWebCommonContext*, uint32_t mask, char*, size_t);
/* Exact initialized root table for the source Fighter_LoadCommonData storage
 * adapter. Requires an attached owned world; unsupported roots stay NULL. */
void** melee_web_common_context_source_roots(void);
/* Runtime worlds bind the original archive loader before Fighter initialization.
 * Narrow descriptor fixtures may omit file IO; actual GameplayWorld always binds
 * a loader and retains its source archive until all fighters have been removed. */
typedef void** (*MeleeWebCommonSourceLoad)(void*);
int melee_web_common_context_set_source_loader(MeleeWebCommonContext*,
    MeleeWebCommonSourceLoad,void*,char*,size_t);
void** melee_web_common_context_load_source_roots(void);
/* Calls both original material consumers, retaining their real JObjs through
 * the native HSD lifetime lane. This is a narrow consumer trace, not
 * Fighter_800679B0 or full common loading. */
int melee_web_common_context_initialize_materials(MeleeWebCommonContext*, char*, size_t);
/* Original Fighter_FirstInitialize_80067A84 calls Fighter_800679B0 and adds
 * the x59C/x5A0 pool. Stage/light/archive services must
 * already be installed. Never initializes only a subset while reporting success. */
int melee_web_common_context_initialize_fighters(MeleeWebCommonContext*, char*, size_t);
int melee_web_common_context_destroy(MeleeWebCommonContext*, char*, size_t);
#ifdef __cplusplus
}
#endif
#endif
