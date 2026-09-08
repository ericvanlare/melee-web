#include "hsd_probe_compat.h"
#include <dolphin/gx.h>
#include <sysdolphin/baselib/debug.h>

/* Native class loading keeps every original PObj method. The only adaptation
 * is the SDK array signature: Aurora requires a proven byte bound that the
 * original descriptor does not contain. This load/lifetime target has no
 * registered drawing arrays, so drawing is explicitly unavailable. */
static void native_checked_array(GXAttr attr, const void* data, u16 stride)
{
    (void) attr;
    (void) data;
    (void) stride;
    HSD_Panic(__FILE__, __LINE__,
              "Native HSD drawing requires a registered bounded vertex array");
}

#define GXSetArray(attr, data, stride) native_checked_array((attr), (data), (stride))
#include <sysdolphin/baselib/pobj.c>
#undef GXSetArray
