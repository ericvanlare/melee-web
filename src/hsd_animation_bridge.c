#include "hsd_probe_compat.h"
#include "hsd_animation_bridge.h"

#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct MeleeWebAnimation {
    size_t node_count, track_count;
    float end_frame;
    int failed;
    HSD_AObj* nodes;
    HSD_FObj* tracks;
    uint8_t* streams;
    MeleeWebAnimationPose* bind;
    MeleeWebAnimationPose* pose;
    MeleeWebAnimationPose* pending;
};

static int reject(char* error, size_t size, const char* reason)
{
    if (error && size) snprintf(error, size, "%s", reason);
    return 0;
}

void melee_web_animation_destroy(MeleeWebAnimation* animation)
{
    if (!animation) return;
    /* Runtime objects point only into these owned arrays. We intentionally do
     * not route host allocations through HSD's console object allocator. */
    free(animation->nodes);
    free(animation->tracks);
    free(animation->streams);
    free(animation->bind);
    free(animation->pose);
    free(animation->pending);
    free(animation);
}

MeleeWebAnimation* melee_web_animation_create(
    uint32_t tree_type, uint32_t flags, float end_frame,
    const uint8_t* node_counts, size_t node_count,
    const MeleeWebAnimationTrack* tracks, size_t track_count,
    const MeleeWebAnimationPose* bind_pose, size_t pose_count,
    char* error, size_t error_size)
{
    size_t expected_tracks = 0, byte_count = 0;
    if (error && error_size) error[0] = '\0';
    if (!node_counts || !tracks || !bind_pose || node_count == 0 || node_count > 140 ||
        pose_count != node_count || track_count == 0 || track_count > 1260) {
        reject(error, error_size, "Animation requires matching bounded node and bind-pose counts");
        return NULL;
    }
    if (tree_type > 1 || (flags & ~(AOBJ_LOOP | AOBJ_NO_UPDATE)) ||
        !isfinite(end_frame) || end_frame <= 0 || end_frame > 65535) {
        reject(error, error_size, "Unsupported animation type, flags or end frame");
        return NULL;
    }
    for (size_t n = 0; n < node_count; ++n) {
        if (node_counts[n] > 9 || node_counts[n] > track_count - expected_tracks) {
            reject(error, error_size, "Animation node track count is inconsistent");
            return NULL;
        }
        if (node_counts[n] && (bind_pose[n].flags & (JOBJ_USE_QUATERNION | JOBJ_JOINT1 | JOBJ_JOINT2))) {
            reject(error, error_size, "Animation attachment requires unsupported quaternion or IK joint callbacks");
            return NULL;
        }
        unsigned channel_mask = 0;
        for (unsigned t = 0; t < node_counts[n]; ++t) {
            const MeleeWebAnimationTrack* track = &tracks[expected_tracks++];
            if (!melee_web_animation_validate_track(track, error, error_size)) return NULL;
            if (channel_mask & (1U << track->type)) {
                reject(error, error_size, "Animation node has duplicate SRT channels");
                return NULL;
            }
            channel_mask |= 1U << track->type;
            byte_count += track->length;
            if (byte_count > 4U * 1024U * 1024U) {
                reject(error, error_size, "Animation streams exceed the memory limit");
                return NULL;
            }
        }
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (!isfinite(bind_pose[n].rotation[axis]) || !isfinite(bind_pose[n].translation[axis]) ||
                !isfinite(bind_pose[n].scale[axis])) {
                reject(error, error_size, "Animation bind pose must be finite");
                return NULL;
            }
        }
    }
    if (expected_tracks != track_count) {
        reject(error, error_size, "Animation node and track totals differ");
        return NULL;
    }
    MeleeWebAnimation* result = calloc(1, sizeof(*result));
    if (!result) {
        reject(error, error_size, "Unable to allocate animation state");
        return NULL;
    }
    result->node_count = node_count;
    result->track_count = track_count;
    result->end_frame = end_frame;
    result->nodes = calloc(node_count, sizeof(*result->nodes));
    result->tracks = calloc(track_count, sizeof(*result->tracks));
    result->streams = malloc(byte_count);
    result->bind = malloc(node_count * sizeof(*result->bind));
    result->pose = malloc(node_count * sizeof(*result->pose));
    result->pending = malloc(node_count * sizeof(*result->pending));
    if (!result->nodes || !result->tracks || !result->streams || !result->bind ||
        !result->pose || !result->pending) {
        melee_web_animation_destroy(result);
        reject(error, error_size, "Unable to allocate animation storage");
        return NULL;
    }
    memcpy(result->bind, bind_pose, node_count * sizeof(*result->bind));
    size_t track_index = 0, byte_offset = 0;
    for (size_t n = 0; n < node_count; ++n) {
        HSD_AObj* aobj = &result->nodes[n];
        // Initial state and assignments follow HSD_AObjAlloc and lbanim's
        // lbAnim_8001E6D8/lbAnim_InitFrames; ownership is the only substitution.
        aobj->flags = AOBJ_NO_ANIM;
        aobj->framerate = 1.0f;
        if (node_counts[n] == 0) continue;
        HSD_AObjSetFlags(aobj, flags);
        HSD_AObjSetRewindFrame(aobj, 0.0f);
        HSD_AObjSetEndFrame(aobj, end_frame);
        aobj->fobj = &result->tracks[track_index];
        if (tree_type & 1) result->bind[n].flags |= JOBJ_CLASSICAL_SCALE;
        else result->bind[n].flags &= ~JOBJ_CLASSICAL_SCALE;
        for (unsigned t = 0; t < node_counts[n]; ++t, ++track_index) {
            HSD_FObj* fobj = &result->tracks[track_index];
            const MeleeWebAnimationTrack* source = &tracks[track_index];
            fobj->next = t + 1 < node_counts[n] ? fobj + 1 : NULL;
            fobj->startframe = (s16) source->start_frame;
            fobj->obj_type = source->type;
            fobj->frac_value = source->value_format;
            fobj->frac_slope = source->slope_format;
            fobj->ad_head = result->streams + byte_offset;
            fobj->length = (u32) source->length;
            memcpy(fobj->ad_head, source->bytes, source->length);
            byte_offset += source->length;
        }
    }
    memcpy(result->pose, result->bind, node_count * sizeof(*result->pose));
    return result;
}

int melee_web_animation_request(MeleeWebAnimation* animation, float frame,
                                char* error, size_t error_size)
{
    if (!animation) return reject(error, error_size, "Missing animation state");
    if (!isfinite(frame) || frame < 0 || frame > animation->end_frame)
        return reject(error, error_size, "Requested animation frame is outside the clip");
    animation->failed = 0;
    memcpy(animation->pose, animation->bind, animation->node_count * sizeof(*animation->pose));
    for (size_t n = 0; n < animation->node_count; ++n)
        if (animation->nodes[n].fobj) HSD_AObjReqAnim(&animation->nodes[n], frame);
    if (error && error_size) error[0] = '\0';
    return 1;
}

typedef struct PoseUpdate {
    MeleeWebAnimationPose* pose;
    int failed;
} PoseUpdate;

static void update_srt(void* object, enum_t type, HSD_ObjData* value)
{
    PoseUpdate* update = object;
    if (!isfinite(value->fv)) {
        update->failed = 1;
        return;
    }
    if (type >= HSD_A_J_ROTX && type <= HSD_A_J_ROTZ)
        update->pose->rotation[type - HSD_A_J_ROTX] = value->fv;
    else if (type >= HSD_A_J_TRAX && type <= HSD_A_J_TRAZ)
        update->pose->translation[type - HSD_A_J_TRAX] = value->fv;
    else if (type >= HSD_A_J_SCAX && type <= HSD_A_J_SCAZ) {
        // JObjUpdateFunc's ordinary scale channel rule; even a tiny negative
        // value becomes positive 0.001. Interpolation itself is original HSD.
        update->pose->scale[type - HSD_A_J_SCAX] = fabsf(value->fv) < 1e-3f ? 1e-3f : value->fv;
    } else update->failed = 1;
}

int melee_web_animation_advance(MeleeWebAnimation* animation,
                                char* error, size_t error_size)
{
    if (!animation || animation->failed)
        return reject(error, error_size, "Animation unavailable; request a frame before retrying failed evaluation");
    memcpy(animation->pending, animation->pose, animation->node_count * sizeof(*animation->pending));
    HSD_AObjInitEndCallBack();
    for (size_t n = 0; n < animation->node_count; ++n) {
        if (!animation->nodes[n].fobj) continue;
        PoseUpdate update = {&animation->pending[n], 0};
        HSD_AObjInterpretAnim(&animation->nodes[n], &update, update_srt);
        if (update.failed) {
            animation->failed = 1;
            return reject(error, error_size, "Original HSD animation evaluation produced a nonfinite or unsupported channel");
        }
    }
    memcpy(animation->pose, animation->pending, animation->node_count * sizeof(*animation->pose));
    if (error && error_size) error[0] = '\0';
    return 1;
}

const MeleeWebAnimationPose* melee_web_animation_pose(const MeleeWebAnimation* animation)
{
    return animation ? animation->pose : NULL;
}
float melee_web_animation_frame(const MeleeWebAnimation* animation)
{
    if (animation) for (size_t n = 0; n < animation->node_count; ++n)
        if (animation->nodes[n].fobj) return animation->nodes[n].curr_frame;
    return 0;
}
int melee_web_animation_finished(const MeleeWebAnimation* animation)
{
    if (!animation || animation->failed) return 1;
    for (size_t n = 0; n < animation->node_count; ++n)
        if (animation->nodes[n].fobj && !(animation->nodes[n].flags & AOBJ_NO_ANIM)) return 0;
    return 1;
}
