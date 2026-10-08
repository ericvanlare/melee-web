#ifndef MELEE_WEB_GAMEPLAY_STAGE_LAST_H
#define MELEE_WEB_GAMEPLAY_STAGE_LAST_H
#include <stddef.h>
#include "gameplay_effect_banks.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebStageLast MeleeWebStageLast;
/* Begin a source-owned stage callback scope for one supported StKind. The
 * caller must have published that stage's GroundParam, native map archive,
 * lights and effect bank first. source_ordered selects the original
 * Stage_802251E8/Stage_8022524C setup and adopts its collision owner. Native
 * descriptors, material programs and particle bank must outlive this scope. */
MeleeWebStageLast* melee_web_stage_begin_kind(int stage_kind, void* yakumono,
    MeleeWebEffectBank*, int defer_start, int source_ordered, char*, size_t);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
/* Diagnostic-only PStadium boundary: run original source-ordered OnInit,
 * return its live owner scope before camera/OnStart, and leave inspection and
 * teardown to the caller through melee_web_stage_last_end. */
MeleeWebStageLast* melee_web_stage_begin_kind_on_init_diagnostic(
    int stage_kind, void* yakumono, MeleeWebEffectBank*, char*, size_t);
#endif
/* Requires full native map/overrides, stage particle bank64, original effect
 * runtime, numeric stage/collision and original camera contexts already live.
 * These compatibility wrappers select Final Destination. */
MeleeWebStageLast* melee_web_stage_last_begin(void* yakumono,MeleeWebEffectBank* map_bank,char*,size_t);
/* Create stage objects now; original Ready completion starts its manager. */
MeleeWebStageLast* melee_web_stage_last_begin_intro(void*,MeleeWebEffectBank*,char*,size_t);
/* End after source callbacks/GX complete, before effect runtime or map owners. */
int melee_web_stage_last_end(MeleeWebStageLast*,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
