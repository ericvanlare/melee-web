#ifndef MELEE_WEB_TEST_STADIUM_C1_STAGE_STATE_PROBE_H
#define MELEE_WEB_TEST_STADIUM_C1_STAGE_STATE_PROBE_H

#include <stddef.h>
#include <stdint.h>

enum {
    MELEE_WEB_STADIUM_C1_STAGE_LIST_UNAVAILABLE = 1u << 0,
    MELEE_WEB_STADIUM_C1_STAGE_MAP_GOBJ = 1u << 1,
    MELEE_WEB_STADIUM_C1_STAGE_INSTANCE = 1u << 2,
    MELEE_WEB_STADIUM_C1_GROUND_GOBJ = 1u << 3,
    MELEE_WEB_STADIUM_C1_STAGE_ITEMS = 1u << 4,
    MELEE_WEB_STADIUM_C1_STAGE_LIGHTS = 1u << 5,
    MELEE_WEB_STADIUM_C1_ORDINARY_GRDAT_SLOT = 1u << 6,
};

#ifdef __cplusplus
extern "C" {
#endif

/* Read source stage registries without copying or mutating their state. */
uint32_t melee_web_stadium_c1_stage_state_failures(void);
/* Observe stage instances and published StageInfo roots while a diagnostic
 * typed map owner occupies the archive slots. */
uint32_t melee_web_stadium_c1_stage_object_failures(void);

typedef struct MeleeWebStadiumC1StageInfoSnapshot
    MeleeWebStadiumC1StageInfoSnapshot;
typedef struct MeleeWebStadiumC1StageInfoView {
    int32_t grkind;
    int32_t xA0;
    int32_t x6E4[2];
    void* itemdata;
    void* coll_data;
    void* param;
    void* ald_yaku_all;
    void* map_ptcl;
    void* map_texg;
    void* yakumono_param;
    void* map_plit;
    void* quake_model_set;
} MeleeWebStadiumC1StageInfoView;

/* Read only the item owner fields needed by the retained-context preflight.
 * Keep upstream item layouts in this C translation unit; the native harness
 * consumes opaque pointer identities through this view. */
typedef struct MeleeWebStadiumC1ItemRuntimeGlobalsView {
    void* public_data;
    void* common_articles;
    void* common_data;
    void* pokemon_articles;
    void* character_articles;
    void* bounce_data;
    void* color_rows;
} MeleeWebStadiumC1ItemRuntimeGlobalsView;
typedef struct MeleeWebStadiumC1ItemPublicDataView {
    void* common_data;
    void* common_articles;
    void* character_articles;
    void* pokemon_articles;
    void* bounce_data;
    void* color_rows;
} MeleeWebStadiumC1ItemPublicDataView;

int melee_web_stadium_c1_item_runtime_globals_view(
    MeleeWebStadiumC1ItemRuntimeGlobalsView* view);
int melee_web_stadium_c1_item_public_data_view(
    void* public_data, MeleeWebStadiumC1ItemPublicDataView* view);
/* Ground's checked ALDYakuAll consumer rows are bounded to 0..7. */
int melee_web_stadium_c1_random_article_state_row(
    void* article, uint32_t row, void** state_table, void** script);

/* Owns a byte-exact copy of the source StageInfo declaration. The snapshot is
 * test-only and must be restored before release. */
MeleeWebStadiumC1StageInfoSnapshot*
melee_web_stadium_c1_stage_info_snapshot_begin(char* error, size_t error_size);
int melee_web_stadium_c1_stage_info_snapshot_view(
    const MeleeWebStadiumC1StageInfoSnapshot* snapshot,
    MeleeWebStadiumC1StageInfoView* view);
int melee_web_stadium_c1_stage_info_current_view(
    MeleeWebStadiumC1StageInfoView* view);
int melee_web_stadium_c1_stage_info_snapshot_restore(
    MeleeWebStadiumC1StageInfoSnapshot* snapshot, char* error,
    size_t error_size);
int melee_web_stadium_c1_stage_info_snapshot_release(
    MeleeWebStadiumC1StageInfoSnapshot* snapshot, char* error,
    size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
