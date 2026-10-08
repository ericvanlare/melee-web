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

/* Keep the authored Ground/StageInfo layouts inside this C translation unit.
 * The native harness is C++ and these upstream declarations contain C-only
 * anonymous structures and reserved-word members. */
typedef struct MeleeWebStadiumC1GroundMapObjectView {
    int32_t map_id;
    void* gobj;
    void* camera;
} MeleeWebStadiumC1GroundMapObjectView;
typedef struct MeleeWebStadiumC1GroundStageProfile {
    int32_t grkind;
    int32_t callback_row_present;
    int32_t callback_flags_b2;
    int32_t joint_table_present;
    int32_t collision_row_present;
    size_t joint_count;
} MeleeWebStadiumC1GroundStageProfile;
typedef struct MeleeWebStadiumC1FtDeviceSnapshot
    MeleeWebStadiumC1FtDeviceSnapshot;

size_t melee_web_stadium_c1_ground_map_slot_count(void);
void* melee_web_stadium_c1_ground_map_slot(size_t index);
size_t melee_web_stadium_c1_ground_marker_slot_count(void);
void* melee_web_stadium_c1_ground_marker_slot(size_t index);
/* This profile reader is deliberately limited to the validated Stadium map1
 * callback row; StageData does not carry an authored callback-array length. */
int melee_web_stadium_c1_ground_map_profile(
    int map_id, MeleeWebStadiumC1GroundStageProfile* profile);
void* melee_web_stadium_c1_ground_map_lookup(int map_id);
void* melee_web_stadium_c1_ground_map_create(int map_id);
int melee_web_stadium_c1_ground_map_remove(void* object);
void* melee_web_stadium_c1_ground_map_joint(void* object, int depth);
int melee_web_stadium_c1_ground_map_object_view(
    void* user_data, MeleeWebStadiumC1GroundMapObjectView* view);
MeleeWebStadiumC1FtDeviceSnapshot*
melee_web_stadium_c1_ft_device_snapshot_create(void);
int melee_web_stadium_c1_ft_device_snapshot_restore(
    const MeleeWebStadiumC1FtDeviceSnapshot* snapshot);
int melee_web_stadium_c1_ft_device_snapshot_matches(
    const MeleeWebStadiumC1FtDeviceSnapshot* snapshot);
int melee_web_stadium_c1_ft_device_snapshot_release(
    MeleeWebStadiumC1FtDeviceSnapshot* snapshot);
size_t melee_web_stadium_c1_ft_device_snapshot_addresses(
    const MeleeWebStadiumC1FtDeviceSnapshot* snapshot,
    const void** addresses, size_t capacity);

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
