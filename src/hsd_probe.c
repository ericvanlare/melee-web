#include "hsd_probe.h"

#include <sysdolphin/baselib/state.h>

void melee_web_hsd_apply_render_state(void)
{
    /* Aurora and the harness can change GX state independently. Reset only
     * the cache groups this probe uses so HSD cannot suppress needed writes.
     * HSD_StateInvalidate() would retain unrelated HSD systems in the link.
     */
    _HSD_StateInvalidatePrimitive();
    _HSD_StateInvalidateRenderMode();
    HSD_StateSetCullMode(GX_CULL_NONE);
    HSD_StateSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    HSD_StateSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    HSD_StateSetColorUpdate(GX_TRUE);
    HSD_StateSetAlphaUpdate(GX_FALSE);
    HSD_StateSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    HSD_StateSetDstAlpha(GX_FALSE, 0);
    HSD_StateSetZCompLoc(GX_TRUE);
    HSD_StateSetDither(GX_FALSE);
}
