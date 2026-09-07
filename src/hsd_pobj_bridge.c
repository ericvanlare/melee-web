#include "hsd_probe_compat.h"
#include "hsd_pobj_bridge.h"

#include <dolphin/gx.h>
#include <stdio.h>
#include <stdlib.h>

static void set_checked_array(GXAttr attr, const void* data, u16 stride);

/* Compile the pinned original implementation instead of copying its private
 * geometry path. Only the SDK array call changes: Aurora needs a byte length
 * and byte order that the GameCube SDK did not accept. Unused HSD loaders and
 * animation functions are removed by the linker's section garbage collection.
 * This file must not be linked alongside a second compilation of pobj.c.
 */
#define GXSetArray(attr, data, stride) set_checked_array((attr), (data), (stride))
#include <sysdolphin/baselib/pobj.c>
#undef GXSetArray

static const MeleeWebPObjView* active_view;

static int reject(char* error, size_t error_size, const char* reason)
{
    if (error != NULL && error_size != 0) {
        snprintf(error, error_size, "%s", reason);
    }
    return 0;
}

static void set_checked_array(GXAttr attr, const void* data, u16 stride)
{
    if (active_view != NULL) {
        for (uint32_t i = 0; i < active_view->attribute_count; ++i) {
            const MeleeWebPObjAttribute* a = &active_view->attributes[i];
            if (a->attr == (uint32_t) attr && a->data == data &&
                a->stride == stride && a->attr_type != GX_DIRECT) {
                GXSetArray(attr, data, a->byte_size, (u8) stride, false);
                return;
            }
        }
    }
    /* This indicates an integration error after validation, not a supported
     * missing service. Never guess an array size or silently omit the array. */
    fputs("HSD PObj bridge: unexpected GXSetArray request\n", stderr);
    abort();
}

static int valid_format(const MeleeWebPObjAttribute* a)
{
    if (a->frac > 31) {
        return 0;
    }
    if (a->attr == GX_VA_CLR0 || a->attr == GX_VA_CLR1) {
        return a->comp_cnt <= GX_CLR_RGBA && a->comp_type <= GX_RGBA8;
    }
    if (a->comp_type > GX_F32) {
        return 0;
    }
    if (a->attr == GX_VA_NRM) {
        /* NBT and NBT3 require a distinct index/component interpretation. */
        return a->comp_cnt == GX_NRM_XYZ &&
               (a->comp_type == GX_S8 || a->comp_type == GX_S16 ||
                a->comp_type == GX_F32);
    }
    return a->comp_cnt <= 1;
}

int melee_web_pobj_draw(const MeleeWebPObjView* view, char* error,
                        size_t error_size)
{
    if (error != NULL && error_size != 0) {
        error[0] = '\0';
    }
    if (active_view != NULL) {
        return reject(error, error_size, "HSD PObj drawing is not reentrant");
    }
    if (view == NULL || view->attributes == NULL ||
        view->attribute_count == 0 ||
        view->attribute_count > MELEE_WEB_POBJ_MAX_ATTRIBUTES) {
        return reject(error, error_size, "Invalid PObj attribute count");
    }
    if ((view->flags & ~(POBJ_CULLFRONT | POBJ_CULLBACK)) != 0) {
        return reject(error, error_size, "Only rigid static PObj cull flags are supported");
    }
    if (view->display == NULL || view->display_byte_size == 0 ||
        (view->display_byte_size & 31) != 0 ||
        view->display_byte_size / 32 > UINT16_MAX) {
        return reject(error, error_size, "Invalid PObj display-list size");
    }
    if (view->attributes[0].attr != GX_VA_POS) {
        return reject(error, error_size, "PObj requires a position attribute");
    }

    HSD_VtxDescList descriptors[MELEE_WEB_POBJ_MAX_ATTRIBUTES + 1] = {0};
    for (uint32_t i = 0; i < view->attribute_count; ++i) {
        const MeleeWebPObjAttribute* a = &view->attributes[i];
        if (a->attr < GX_VA_POS || a->attr > GX_VA_TEX7 ||
            (i != 0 && a->attr <= view->attributes[i - 1].attr)) {
            return reject(error, error_size, "Unsupported or unordered PObj attribute");
        }
        if (a->attr_type < GX_DIRECT || a->attr_type > GX_INDEX16 ||
            !valid_format(a)) {
            return reject(error, error_size, "Unsupported PObj attribute format");
        }
        if (a->stride > UINT8_MAX ||
            (a->attr_type != GX_DIRECT &&
             (a->data == NULL || a->stride == 0 || a->byte_size < a->stride))) {
            return reject(error, error_size, "Invalid PObj indexed array span or stride");
        }
        descriptors[i] = (HSD_VtxDescList){
            .attr = (GXAttr) a->attr,
            .attr_type = (GXAttrType) a->attr_type,
            .comp_cnt = (GXCompCnt) a->comp_cnt,
            .comp_type = (GXCompType) a->comp_type,
            .frac = a->frac,
            .stride = a->stride,
            .vertex = (void*) a->data,
        };
    }
    descriptors[view->attribute_count].attr = GX_VA_NULL;

    HSD_PObj pobj = {
        .verts = descriptors,
        .flags = view->flags,
        .n_display = (u16) (view->display_byte_size / 32),
        .display = (u8*) view->display,
    };
    /* HSD_PObjDisp normally performs this cull selection before its class
     * matrix callback. This narrow bridge uses caller-supplied matrices and
     * reaches the original simple-primitive function directly. */
    if (view->flags == (POBJ_CULLFRONT | POBJ_CULLBACK)) {
        return 1;
    }
    _HSD_StateInvalidatePrimitive();
    HSD_StateSetCullMode((view->flags & POBJ_CULLFRONT) ? GX_CULL_FRONT :
                        (view->flags & POBJ_CULLBACK) ? GX_CULL_BACK : GX_CULL_NONE);

    /* Stack addresses can repeat on the next mesh. Invalidate on both sides
     * so original HSD caches never mistake recycled descriptors for old data. */
    HSD_ClearVtxDesc();
    active_view = view;
    PObjDispSimplePrimitive(&pobj, 0);
    active_view = NULL;
    HSD_ClearVtxDesc();
    return 1;
}
