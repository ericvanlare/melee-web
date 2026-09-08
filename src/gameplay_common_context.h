#ifndef MELEE_WEB_GAMEPLAY_COMMON_CONTEXT_H
#define MELEE_WEB_GAMEPLAY_COMMON_CONTEXT_H
#include "common_tables.h"
#include "hsd_native_joint.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MELEE_WEB_COMMON_RUNTIME_MASK (MELEE_WEB_COMMON_STATIC_ROOT_MASK | 1u | (1u << 20))
typedef struct MeleeWebCommonContext MeleeWebCommonContext;
/* Copies all supported scalars/tables and root20 descriptors plus raw payloads.
 * The resulting owner is independent of the input archive and model lifetime. */
MeleeWebCommonContext* melee_web_common_context_create(const MeleeWebCommonScalars*,
    const MeleeWebCommonTables*, const MeleeWebNativeGraph*, char*, size_t);
int melee_web_common_context_attach(MeleeWebCommonContext*, char*, size_t);
int melee_web_common_context_require(MeleeWebCommonContext*, uint32_t mask, char*, size_t);
/* Exact initialized root table for the source Fighter_LoadCommonData storage
 * adapter. Requires an attached owned world; unsupported roots stay NULL. */
void** melee_web_common_context_source_roots(void);
/* Calls both original material consumers, retaining their real JObjs in GObjs.
 * This is a narrow consumer trace, not Fighter_800679B0 or full common loading. */
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
