#ifndef MELEE_WEB_HSD_TRANSFORM_BRIDGE_H
#define MELEE_WEB_HSD_TRANSFORM_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebJointTransform {
    float matrix[3][4];
    float accumulated_scale[3];
    int has_accumulated_scale;
} MeleeWebJointTransform;

/* Ordinary static Euler joint transforms through original HSD_MtxSRT and
 * Aurora's SDK matrix concatenation. Angles are radians. parent is NULL at
 * a root; otherwise pass the state returned for that joint's actual parent.
 *
 * Preserves HSD_JObjMakeMatrix's scale state: nonclassical joints accumulate
 * their scale, while classical joints inherit their parent's scale state.
 * HSD_MtxSRT uses the parent's accumulated scale for compensation before
 * parent-matrix concatenation. The state is not simply the world matrix's
 * column lengths, so keep it with the joint while traversing the tree.
 *
 * Billboard, quaternion, instance, skeletal/IK, user matrix, independent
 * matrix/SRT and unknown modes are unsupported. Animation/constraint refs
 * and graph validity must be checked by the importer. Visibility/material
 * flags are accepted here but must still be honored by the rendering caller.
 * Returns 1 on success or 0 with a diagnostic; output is unchanged on failure.
 * output may alias parent. No GameCube bit-exact math claim is made by this
 * source-path bridge; original-game numerical comparison remains required.
 */
int melee_web_joint_transform(uint32_t flags, const float scale[3],
                              const float rotation[3], const float translation[3],
                              const MeleeWebJointTransform* parent,
                              MeleeWebJointTransform* output,
                              char* error, size_t error_size);

/* Compose the camera and world matrix through the SDK. Rejects nonfinite
 * inputs/results before changing output; output may alias either input. */
int melee_web_joint_view_matrix(const float camera[3][4],
                                const MeleeWebJointTransform* joint,
                                float output[3][4], char* error,
                                size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
