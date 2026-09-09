#include "gameplay_stage_items.h"
#include "gameplay_article_data.h"
#include "gameplay_bootstrap.h"
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/it/forward.h>
#include <melee/it/it_26B1.h>
#include <melee/it/it_3F14.h>
#include <sysdolphin/baselib/gobj.h>
#include <stdlib.h>
#include <stdio.h>

enum { STAGE_ITEM_SLOTS = 30 };

struct MeleeWebStageItems {
    struct GroundItemData** previous_list;
    Article* previous[STAGE_ITEM_SLOTS];
    struct GroundItemData* entries;
    struct GroundItemData** list;
    uint8_t changed[STAGE_ITEM_SLOTS];
    uint64_t generation;
};

static MeleeWebStageItems* active;
static int fail(char* error, size_t size, const char* message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

MeleeWebStageItems* melee_web_stage_items_begin(
    const MeleeWebStageItemDesc* items, size_t count, char* error, size_t size)
{
    if (active || (!items && count) || count > STAGE_ITEM_SLOTS ||
        !melee_web_gameplay_stats().generation) {
        fail(error, size,
             "Stage-item publication requires one active checked source world");
        return NULL;
    }
    uint8_t seen[STAGE_ITEM_SLOTS] = {0};
    for (size_t i = 0; i < count; ++i) {
        const int slot = items[i].kind - It_Kind_Old_Kuri;
        if (slot < 0 || slot >= STAGE_ITEM_SLOTS || seen[slot] ||
            !items[i].article || melee_web_article_unresolved(items[i].article))
        {
            fail(error, size,
                 "Stage-item list has an invalid kind or incomplete Article");
            return NULL;
        }
        seen[slot] = 1;
    }
    MeleeWebStageItems* scope = calloc(1, sizeof(*scope));
    if (!scope) {
        fail(error, size, "Cannot allocate stage-item ownership scope");
        return NULL;
    }
    scope->entries = calloc(count ? count : 1, sizeof(*scope->entries));
    scope->list = calloc(count + 1, sizeof(*scope->list));
    if (!scope->entries || !scope->list) {
        free(scope->entries); free(scope->list); free(scope);
        fail(error, size, "Cannot allocate stage-item descriptor list");
        return NULL;
    }
    scope->previous_list = stage_info.itemdata;
    scope->generation = melee_web_gameplay_stats().generation;
    for (size_t i = 0; i < count; ++i) {
        const int slot = items[i].kind - It_Kind_Old_Kuri;
        scope->previous[slot] = it_804A0F60[slot];
        scope->changed[slot] = 1;
        scope->entries[i].unk0 = items[i].kind;
        scope->entries[i].unk4 = items[i].article;
        scope->list[i] = &scope->entries[i];
        it_8026B40C(items[i].article, items[i].kind);
    }
    stage_info.itemdata = scope->list;
    active = scope;
    if (error && size) *error = 0;
    return scope;
}

int melee_web_stage_items_end(MeleeWebStageItems* scope, char* error, size_t size)
{
    if (!scope) return 1;
    if (scope != active || scope->generation != melee_web_gameplay_stats().generation)
        return fail(error, size, "Stage-item scope lost its source-world ownership");
    if (((HSD_GObj**) HSD_GObj_Entities)[9])
        return fail(error, size, "Remove live items before releasing stage Articles");
    stage_info.itemdata = scope->previous_list;
    for (unsigned i = 0; i < STAGE_ITEM_SLOTS; ++i)
        if (scope->changed[i]) it_804A0F60[i] = scope->previous[i];
    active = NULL;
    free(scope->list); free(scope->entries); free(scope);
    if (error && size) *error = 0;
    return 1;
}
