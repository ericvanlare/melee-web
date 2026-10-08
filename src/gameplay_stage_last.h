#ifndef MELEE_WEB_GAMEPLAY_STAGE_LAST_H
#define MELEE_WEB_GAMEPLAY_STAGE_LAST_H
#include <stddef.h>
#include "gameplay_effect_banks.h"
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
#include "gameplay_stadium_display_owner.h"
#endif
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
 * teardown to the caller through melee_web_stage_last_end. owner_out is
 * required and receives a live scope when post-E8 ownership must be retained
 * after a failed initialization check. */
MeleeWebStageLast* melee_web_stage_begin_kind_on_init_diagnostic(
    int stage_kind, void* yakumono, MeleeWebEffectBank*,
    MeleeWebStageLast** owner_out, char*, size_t);
/* Asset-free reducer for the diagnostic bind-refusal reporting path. It
 * injects the bind failure and E8 journal event, then exercises the real
 * display-owner cancellation guard. The partial owner remains live until the
 * standalone control process exits; no archive or source Stage routine runs. */
int melee_web_stage_last_on_init_bind_refusal_controls(void);
/* Read the already-captured private Stadium map2 journal while its StageLast
 * owner is still active. This copies the existing record; it does not inspect
 * or extend the private Ground layout. Call only before stage_last_end. */
int melee_web_stage_last_stadium_map2_buffer_snapshot(
    const MeleeWebStageLast*, MeleeWebStadiumMap2BufferOwner* out);
/* Snapshot the actual guarded source taps while their StageLast owner is live. */
int melee_web_stage_last_stadium_source_journal_snapshot(
    const MeleeWebStageLast*, MeleeWebStadiumSourceJournal* out);
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
