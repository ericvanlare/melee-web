#ifndef MELEE_WEB_GAMEPLAY_COLLISION_H
#define MELEE_WEB_GAMEPLAY_COLLISION_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebCollision MeleeWebCollision;
/* Stage construction owns the source load until adoption succeeds. After
 * removing stage consumers, retire an unadopted load or leave its live owner. */
int melee_web_collision_retire_unadopted(const void* source_map,
                                         char* error, size_t size);
typedef struct MeleeWebCollisionVertex { float x, y; } MeleeWebCollisionVertex;
typedef struct MeleeWebCollisionRange { int16_t start, count; } MeleeWebCollisionRange;
typedef struct MeleeWebCollisionLine {
    uint16_t v0, v1;
    int16_t prev0, next0, prev1, next1;
    uint16_t hi_flags, lo_flags;
} MeleeWebCollisionLine;
typedef struct MeleeWebCollisionJoint {
    MeleeWebCollisionRange ranges[5]; /* Floor, ceiling, right wall, left wall, dynamic. */
    float left, bottom, right, top;
    MeleeWebCollisionRange vertices;
} MeleeWebCollisionJoint;
typedef struct MeleeWebCollisionInput {
    const MeleeWebCollisionVertex* vertices;
    size_t vertex_count;
    const MeleeWebCollisionLine* lines;
    size_t line_count;
    const MeleeWebCollisionJoint* joints;
    size_t joint_count;
    MeleeWebCollisionRange ranges[5];
    uint32_t source_reserved_2c;
    int32_t stage_kind; /* Original GrKind, not StKind. */
    float stage_scale; /* Actual grGroundParam.y, or an explicitly authored test scale. */
} MeleeWebCollisionInput;
typedef struct MeleeWebCollisionLineResult {
    float v0[3], v1[3], normal[3];
    uint32_t kind, material_flags, runtime_flags;
    int32_t previous, next, joint;
    uint16_t pruned_hi_flags;
    int has_normal;
} MeleeWebCollisionLineResult;
typedef struct MeleeWebCollisionFloorResult {
    int32_t line; /* -1 means no floor at this x along the selected floor chain. */
    float displacement_y, normal[3];
    uint32_t material_flags;
} MeleeWebCollisionFloorResult;
typedef struct MeleeWebCollisionReadiness {
    uint32_t vertices, lines, joints, floor_islands, ceiling_islands, empty_lines;
    int storage_owned, original_indices_initialized;
    int stage_joint_bindings_ready, stage_callbacks_ready; /* Remain false in this seam. */
} MeleeWebCollisionReadiness;

/* Copies typed arrays, runs original mpLibLoad/pruning/island initialization,
 * then exposes static queries BEFORE original stage joint binding/callbacks.
 * Requires the isolated gameplay world and exclusive mpLib ownership. Dynamic
 * line ranges and cyclic floor/ceiling chains reject explicitly. This is not a
 * fully initialized stage or a fighter physics simulation. The original GObj
 * userdata destructor releases storage automatically on world shutdown. */
MeleeWebCollision* melee_web_collision_create(const MeleeWebCollisionInput*, char* error, size_t error_size);
/* Adopt collision arrays and the original link-6 updater after retail
 * Stage_8022524C has called mpLibLoad/mpLib_80058820. Does not load or allocate
 * another collision map. */
MeleeWebCollision* melee_web_collision_adopt_loaded(const MeleeWebCollisionInput*, char* error, size_t error_size);
/* Results' original dummy-stage entry calls mpLibLoad(NULL) itself. Adopt
 * its default-map arrays and original process so their ordinary GObj destructor
 * releases the same storage before another scene may construct collision. */
int melee_web_collision_source_available(void);
MeleeWebCollision* melee_web_collision_adopt_dummy(char* error, size_t error_size);
int melee_web_collision_readiness(MeleeWebCollision*, MeleeWebCollisionReadiness*, char* error, size_t error_size);
int melee_web_collision_line(MeleeWebCollision*, int32_t line, MeleeWebCollisionLineResult*, char* error, size_t error_size);
int melee_web_collision_floor(MeleeWebCollision*, int32_t starting_line, float x, float y,
                              MeleeWebCollisionFloorResult*, char* error, size_t error_size);
/* Also releases a handle whose storage was already removed by world shutdown. */
int melee_web_collision_destroy(MeleeWebCollision*, char* error, size_t error_size);

#ifdef __cplusplus
}
#endif
#endif
