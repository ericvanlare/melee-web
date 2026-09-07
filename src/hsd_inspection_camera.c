#include "hsd_probe_compat.h"
#include "hsd_inspection.h"
#include <sysdolphin/baselib/mtx.h>
#include <sysdolphin/baselib/class.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Preserve the original camera getters while supplying an owned inspection
 * camera. The general scene camera scheduler and class loader are not retained.
 * The lazy inverse-matrix allocation uses the same checked host allocator as
 * material expressions if a later supported path marks the camera dirty. */
static void* inspection_matrix_alloc(void);
#define HSD_MtxAlloc inspection_matrix_alloc
#include <sysdolphin/baselib/cobj.c>
#undef HSD_MtxAlloc

static HSD_CObj inspection_camera;
static Mtx inspection_inverse;

static void* inspection_matrix_alloc(void) { return hsdAllocMemPiece(sizeof(Mtx)); }

int melee_web_inspection_camera(const float view[3][4], char* error, size_t size)
{
    Mtx inverse;
    if (!view) {
        if (error && size) snprintf(error, size, "Inspection camera is missing");
        return 0;
    }
    for (size_t r = 0; r < 3; ++r) for (size_t c = 0; c < 4; ++c) {
        if (!isfinite(view[r][c])) {
            if (error && size) snprintf(error, size, "Inspection camera is nonfinite");
            return 0;
        }
    }
    if (!PSMTXInverse(view, inverse)) {
        if (error && size) snprintf(error, size, "Inspection camera is singular");
        return 0;
    }
    for (size_t r = 0; r < 3; ++r) for (size_t c = 0; c < 4; ++c) {
        if (!isfinite(inverse[r][c])) {
            if (error && size) snprintf(error, size, "Inspection camera inverse is nonfinite");
            return 0;
        }
    }
    memcpy(inspection_camera.view_mtx, view, sizeof(Mtx));
    memcpy(inspection_inverse, inverse, sizeof(Mtx));
    inspection_camera.proj_mtx = &inspection_inverse;
    inspection_camera.flags = 0;
    current = &inspection_camera;
    if (error && size) error[0] = 0;
    return 1;
}
