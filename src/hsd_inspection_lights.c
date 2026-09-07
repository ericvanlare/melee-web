#include "hsd_probe_compat.h"
#include "hsd_inspection.h"
#include <sysdolphin/baselib/wobj.h>
#include <math.h>
#include <stdio.h>

static void static_light_position(HSD_WObj* object, Vec3* output);
/* Only authored static WObjs are supplied by this inspection environment.
 * Reject dynamic position dependencies explicitly instead of retaining the
 * entire animated JObj constraint system through WObjGetPosition's branch. */
#define HSD_WObjGetPosition static_light_position
#include <sysdolphin/baselib/lobj.c>
#undef HSD_WObjGetPosition

static HSD_WObj light_position = {.pos = {0.4f, 0.7f, 1.f}};
static HSD_WObj light_interest = {.pos = {0.f, 0.f, 0.f}};
static HSD_LObj ambient = {.flags = LOBJ_AMBIENT | LOBJ_DIFFUSE, .color = {96, 96, 96, 255}};
static HSD_LObj key = {.flags = LOBJ_INFINITE | LOBJ_DIFFUSE | LOBJ_SPECULAR,
    .color = {224, 224, 224, 255}, .position = &light_position, .interest = &light_interest};
static HSD_SList ambient_link = {.next = NULL, .data = &ambient};
static HSD_SList key_link = {.next = &ambient_link, .data = &key};

static void static_light_position(HSD_WObj* object, Vec3* output)
{
    if (!object || !output || object->aobj || object->robj || (object->flags & 1))
        HSD_Panic(__FILE__, __LINE__, "Dynamic inspection light positions are unsupported");
    *output = object->pos;
}

int melee_web_inspection_begin(const float view[3][4], char* error, size_t size)
{
    if (!melee_web_inspection_camera(view, error, size)) return 0;
    // A fixed, owned list avoids class/scene ownership callbacks. Actual HSD
    // light selection, light masks, GX light objects and material setup execute.
    current_lights = &key_link;
    HSD_LObjSetupInit(HSD_CObjGetCurrent());
    return 1;
}

int melee_web_inspection_specular(const float model_view[3][4], char* error, size_t size)
{
    if (!model_view) {
        if (error && size) snprintf(error, size, "Specular model-view matrix is missing");
        return 0;
    }
    float length = 0;
    for (size_t i = 0; i < 3; ++i) length += model_view[i][3] * model_view[i][3];
    if (!isfinite(length) || length <= 0) {
        if (error && size) snprintf(error, size, "Specular inspection requires a finite nonzero eye-to-joint vector");
        return 0;
    }
    Mtx copy;
    PSMTXCopy(model_view, copy);
    HSD_LObjSetupSpecularInit(copy);
    if (error && size) error[0] = 0;
    return 1;
}
