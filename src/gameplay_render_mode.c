#include <dolphin/gx.h>

/* Aurora's browser GX provider omits this original SDK mode descriptor. Keep
 * the pinned Melee/Dolphin source values used by gmMainLib_8015F500 intact. */
GXRenderModeObj GXNtsc480Prog = {
    2,   640, 480, 480, 40, 0, 640, 480, 0, 0, 0,
    { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
      6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
    { 0, 0, 21, 22, 21, 0, 0 },
};
