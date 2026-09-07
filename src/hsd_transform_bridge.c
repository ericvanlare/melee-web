#include "hsd_probe_compat.h"
#include "hsd_transform_bridge.h"

#include <sysdolphin/baselib/displayfunc.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mtx.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int melee_web_joint_normal_matrix(const float view[3][4], float output[3][4],
                                  char* error, size_t error_size)
{
    if (!view || !output) {
        if (error && error_size) snprintf(error, error_size, "Missing normal-matrix input or output");
        return 0;
    }
    Mtx input, result;
    for (size_t r = 0; r < 3; ++r) for (size_t c = 0; c < 4; ++c) {
        if (!isfinite(view[r][c])) {
            if (error && error_size) snprintf(error, error_size, "Normal-matrix input is nonfinite");
            return 0;
        }
        input[r][c] = view[r][c];
    }
    HSD_MtxInverseTranspose(input, result);
    for (size_t r = 0; r < 3; ++r) for (size_t c = 0; c < 4; ++c) {
        if (!isfinite(result[r][c])) {
            if (error && error_size) snprintf(error, error_size, "Normal-matrix output is nonfinite");
            return 0;
        }
    }
    memcpy(output, result, sizeof(result));
    if (error && error_size) error[0] = '\0';
    return 1;
}

static int reject(char* error, size_t error_size, const char* reason)
{
    if (error != NULL && error_size != 0) {
        snprintf(error, error_size, "%s", reason);
    }
    return 0;
}

int melee_web_joint_transform(uint32_t flags, const float scale[3],
                              const float rotation[3], const float translation[3],
                              const MeleeWebJointTransform* parent,
                              MeleeWebJointTransform* output,
                              char* error, size_t error_size)
{
    const uint32_t allowed_flags = JOBJ_CLASSICAL_SCALE | JOBJ_HIDDEN |
        JOBJ_MTX_DIRTY | JOBJ_LIGHTING | JOBJ_TEXGEN | JOBJ_SPECULAR |
        JOBJ_OPA | JOBJ_XLU | JOBJ_TEXEDGE | JOBJ_ROOT_MASK;
    if (error != NULL && error_size != 0) {
        error[0] = '\0';
    }
    if (scale == NULL || rotation == NULL || translation == NULL || output == NULL) {
        return reject(error, error_size, "Missing joint transform input or output");
    }
    if ((flags & ~allowed_flags) != 0) {
        return reject(error, error_size, "Joint flags require unsupported matrix behavior");
    }
    for (size_t i = 0; i < 3; ++i) {
        if (!isfinite(scale[i]) || !isfinite(rotation[i]) || !isfinite(translation[i])) {
            return reject(error, error_size, "Joint SRT values must be finite");
        }
    }
    if (parent != NULL) {
        if (parent->has_accumulated_scale != 0 && parent->has_accumulated_scale != 1) {
            return reject(error, error_size, "Invalid parent scale state");
        }
        for (size_t row = 0; row < 3; ++row) {
            for (size_t column = 0; column < 4; ++column) {
                if (!isfinite(parent->matrix[row][column])) {
                    return reject(error, error_size, "Parent joint matrix must be finite");
                }
            }
            if (parent->has_accumulated_scale &&
                (!isfinite(parent->accumulated_scale[row]) ||
                 parent->accumulated_scale[row] == 0.0f)) {
                return reject(error, error_size, "Parent scale compensation requires finite nonzero scale");
            }
        }
    }

    MeleeWebJointTransform result = {0};
    const int parent_has_scale = parent != NULL && parent->has_accumulated_scale;
    /* These metadata operations follow HSD_JObjMakeMatrix's scl branch.
     * Allocation and JObj callbacks are unnecessary for a validated static
     * tree: the caller owns this scale state and visits the parent first. */
    if (flags & JOBJ_CLASSICAL_SCALE) {
        result.has_accumulated_scale = parent_has_scale;
        if (parent_has_scale) {
            memcpy(result.accumulated_scale, parent->accumulated_scale,
                   sizeof result.accumulated_scale);
        }
    } else {
        result.has_accumulated_scale = 1;
        for (size_t i = 0; i < 3; ++i) {
            result.accumulated_scale[i] = parent_has_scale ?
                scale[i] * parent->accumulated_scale[i] : scale[i];
        }
    }
    Vec3 local_scale = {scale[0], scale[1], scale[2]};
    Vec3 local_rotation = {rotation[0], rotation[1], rotation[2]};
    Vec3 local_translation = {translation[0], translation[1], translation[2]};
    Vec3 parent_scale;
    if (parent_has_scale) {
        parent_scale = (Vec3){parent->accumulated_scale[0],
                             parent->accumulated_scale[1],
                             parent->accumulated_scale[2]};
    }
    HSD_MtxSRT(result.matrix, &local_scale, &local_rotation, &local_translation,
               parent_has_scale ? &parent_scale : NULL);
    if (parent != NULL) {
        PSMTXConcat(parent->matrix, result.matrix, result.matrix);
    }
    for (size_t row = 0; row < 3; ++row) {
        if (result.has_accumulated_scale && !isfinite(result.accumulated_scale[row])) {
            return reject(error, error_size, "Accumulated joint scale overflow");
        }
        for (size_t column = 0; column < 4; ++column) {
            if (!isfinite(result.matrix[row][column])) {
                return reject(error, error_size, "Joint matrix computation produced a nonfinite value");
            }
        }
    }
    *output = result;
    return 1;
}

int melee_web_joint_view_matrix(const float camera[3][4],
                                const MeleeWebJointTransform* joint,
                                float output[3][4], char* error,
                                size_t error_size)
{
    if (error != NULL && error_size != 0) {
        error[0] = '\0';
    }
    if (camera == NULL || joint == NULL || output == NULL) {
        return reject(error, error_size, "Missing joint view-matrix input or output");
    }
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            if (!isfinite(camera[row][column]) || !isfinite(joint->matrix[row][column])) {
                return reject(error, error_size, "Joint view-matrix inputs must be finite");
            }
        }
    }
    Mtx result;
    PSMTXConcat(camera, joint->matrix, result);
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            if (!isfinite(result[row][column])) {
                return reject(error, error_size, "Joint view-matrix computation produced a nonfinite value");
            }
        }
    }
    memcpy(output, result, sizeof result);
    return 1;
}
