#ifndef MELEE_WEB_HSD_ANIMATION_BRIDGE_H
#define MELEE_WEB_HSD_ANIMATION_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebAnimationPose {
    float rotation[3], translation[3], scale[3];
    uint32_t flags;
} MeleeWebAnimationPose;

typedef struct MeleeWebAnimationTrack {
    const uint8_t* bytes;
    size_t length;
    uint16_t start_frame;
    uint8_t type, value_format, slope_format;
} MeleeWebAnimationTrack;

typedef struct MeleeWebAnimation MeleeWebAnimation;

/* Checks the entire packed stream before the original unchecked FObj reader
 * can see it. Only Euler rotation, translation and scale channels are allowed. */
int melee_web_animation_validate_track(const MeleeWebAnimationTrack* track,
                                      char* error, size_t error_size);

/* Each node count is its consecutive track count. The caller must establish
 * the animation-node to skeleton mapping; this seam uses matching preorder.
 * Copies all input data. Untracked SRT/flags retain bind values. Animated nodes
 * get lbanim's CLASSICAL_SCALE selection from tree_type bit zero. */
MeleeWebAnimation* melee_web_animation_create(
    uint32_t tree_type, uint32_t flags, float end_frame,
    const uint8_t* node_counts, size_t node_count,
    const MeleeWebAnimationTrack* tracks, size_t track_count,
    const MeleeWebAnimationPose* bind_pose, size_t pose_count,
    char* error, size_t error_size);
void melee_web_animation_destroy(MeleeWebAnimation* animation);

/* Request resets the pose to bind values and queues the original FIRST_PLAY
 * evaluation. The next advance evaluates exactly frame; subsequent calls
 * advance one original animation frame. No wall-clock or viewer loop policy. */
int melee_web_animation_request(MeleeWebAnimation* animation, float frame,
                                char* error, size_t error_size);
int melee_web_animation_advance(MeleeWebAnimation* animation,
                                char* error, size_t error_size);
const MeleeWebAnimationPose* melee_web_animation_pose(const MeleeWebAnimation* animation);
float melee_web_animation_frame(const MeleeWebAnimation* animation);
int melee_web_animation_finished(const MeleeWebAnimation* animation);

#ifdef __cplusplus
}
#endif
#endif
