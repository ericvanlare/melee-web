#ifndef MELEE_WEB_GAMEPLAY_STAGE_ITEMS_H
#define MELEE_WEB_GAMEPLAY_STAGE_ITEMS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebStageItemDesc {
    int32_t kind;
    void* article;
} MeleeWebStageItemDesc;

typedef struct MeleeWebStageItems MeleeWebStageItems;

/* Publish decoded stage-owned Articles through the same item table and
 * stage_info.itemdata list used by Ground_801C0800. The scope retains and
 * restores the previous source globals; every live stage item must be removed
 * before it ends. */
MeleeWebStageItems* melee_web_stage_items_begin(
    const MeleeWebStageItemDesc*, size_t count, char* error, size_t error_size);
int melee_web_stage_items_end(
    MeleeWebStageItems*, char* error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
