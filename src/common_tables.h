#ifndef MELEE_WEB_COMMON_TABLES_H
#define MELEE_WEB_COMMON_TABLES_H
#include "common_schema.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Source domains: FTKIND_MAX; MAX_FT_PARTS; named enum TopN..TransN2 plus
 * ftParts_80074E58's explicit named index 0x35. Padding after the 54 names is
 * not another named part. Native bridge assertions verify these constants. */
#define MELEE_WEB_COMMON_FIGHTERS 33u
#define MELEE_WEB_COMMON_MAX_PARTS 140u
#define MELEE_WEB_COMMON_PART_NAMES 54u
#define MELEE_WEB_COMMON_MAX_ALTERNATES 32u
#define MELEE_WEB_COMMON_MAX_SHAKE 255u
#define MELEE_WEB_COMMON_STATIC_ROOT_MASK ((1u<<1)|(1u<<2)|(1u<<3)|(1u<<4)|(1u<<5)|\
    (1u<<9)|(1u<<10)|(1u<<11)|(1u<<12)|(1u<<13)|(1u<<14)|(1u<<15)|(1u<<18)|(1u<<19)|(1u<<21))

typedef struct MeleeWebCommonThrow { float velocity_mul,angle,heavy_mul; } MeleeWebCommonThrow;
typedef struct MeleeWebCommonParts {
    uint32_t part_count;
    uint8_t joint_to_part[MELEE_WEB_COMMON_MAX_PARTS];
    uint8_t part_to_joint[MELEE_WEB_COMMON_PART_NAMES];
} MeleeWebCommonParts;
typedef struct MeleeWebCommonAlternate { uint8_t slot,parent,insertion,source_joint; } MeleeWebCommonAlternate;
typedef struct MeleeWebCommonAlternates {
    uint32_t has_descriptor,count;
    MeleeWebCommonAlternate entries[MELEE_WEB_COMMON_MAX_ALTERNATES];
} MeleeWebCommonAlternates;
typedef struct MeleeWebCommonShake {
    uint32_t count;
    MeleeWebCommonVec2 samples[MELEE_WEB_COMMON_MAX_SHAKE];
} MeleeWebCommonShake;

#define MELEE_WEB_CROWD_FIELDS(X) \
 X(0x00,F32,kb_threshold_low) X(0x04,F32,kb_threshold_mid) X(0x08,F32,kb_threshold_high) \
 X(0x0c,F32,angle_min) X(0x10,F32,angle_max) X(0x14,F32,angle_mult) X(0x18,F32,x18) \
 X(0x1c,I32,x1C) X(0x20,I32,cheer_limit) X(0x24,I32,x24) X(0x28,I32,max_gasp_count) \
 X(0x2c,F32,horiz_margin) X(0x30,F32,recovery_y_high) X(0x34,F32,recovery_y_mid) \
 X(0x38,F32,recovery_y_low) X(0x3c,I32,fighters_near_blastzone) X(0x40,F32,blastzone_y_offset)
typedef struct MeleeWebCommonCrowd {
#define MELEE_WEB_CROWD_F32 float
#define MELEE_WEB_CROWD_I32 int32_t
#define MELEE_WEB_CROWD_FIELD(offset,kind,name) MELEE_WEB_CROWD_##kind name;
    MELEE_WEB_CROWD_FIELDS(MELEE_WEB_CROWD_FIELD)
#undef MELEE_WEB_CROWD_FIELD
#undef MELEE_WEB_CROWD_I32
#undef MELEE_WEB_CROWD_F32
} MeleeWebCommonCrowd;

/* Self-contained decoded values: copying/moving this struct never leaves
 * borrowed archive/native pointers. ready_mask records actually present roots.
 * It deliberately contains no root0, scripts, HSD descriptors, unknown17 or AI. */
typedef struct MeleeWebCommonTables {
    uint32_t ready_mask;
    /* ftCo_ItemThrow.c: LightThrowF..HeavyThrowLw4; ftswing.c's 6x5 motion
     * table; ft_80089118's i<9 loop. Counts never come from DAT padding. */
    MeleeWebCommonThrow item_throw[26];
    float swing[6][5], stale[9];
    MeleeWebCommonParts parts[MELEE_WEB_COMMON_FIGHTERS];
    MeleeWebCommonAlternates alternates[MELEE_WEB_COMMON_FIGHTERS];
    MeleeWebCommonShake damage_shake[3], grab_shake, smash_shake;
    float scale_modifiers[39], bunny_modifiers[15], metal_modifiers[9], gravity_weight[2];
    /* Player_GetUnk45 selects player colors 0..3 or the non-human color 4. */
    MeleeWebCommonColor primary_colors[5], secondary_colors[5];
    MeleeWebCommonCrowd crowd;
} MeleeWebCommonTables;

typedef struct MeleeWebCommonNative MeleeWebCommonNative;
/* Copies/validates values and constructs real source-layout pointer graphs.
 * Independent of a world arena; caller destroys only after every consumer.
 * No global root table is published by create/root/destroy. */
MeleeWebCommonNative* melee_web_common_tables_create(const MeleeWebCommonTables*,char* error,size_t error_size);
const void* melee_web_common_tables_root(const MeleeWebCommonNative*,uint32_t root_index);
void melee_web_common_tables_destroy(MeleeWebCommonNative*);
/* Bounded readback invokes original ftParts_GetBoneIndex/ftPartsRemap with a
 * scoped original global and restores the prior global before returning. */
int melee_web_common_parts_lookup(MeleeWebCommonNative*,uint32_t kind,uint32_t named_part,
                                  uint32_t* joint,char* error,size_t error_size);
int melee_web_common_parts_remap(MeleeWebCommonNative*,uint32_t to_kind,uint32_t from_kind,uint32_t joint,
                                 uint32_t* mapped_joint,char* error,size_t error_size);
#ifdef __cplusplus
}
#endif
#endif
