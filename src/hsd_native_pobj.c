#include "hsd_probe_compat.h"
#include "hsd_native_arrays.h"
#include <dolphin/gx.h>
#include <sysdolphin/baselib/debug.h>

/* Execute every original geometry/matrix method; adapt only Aurora's extra
 * array byte bound using metadata registered by live native descriptor owners. */
static void native_checked_array(GXAttr attr, const void* data, u16 stride)
{
    uint32_t bytes;
    if (!melee_web_native_array_bound(attr,data,stride,&bytes))
        HSD_Panic(__FILE__, __LINE__, "Native HSD drawing requires a registered bounded vertex array");
    GXSetArray(attr,data,bytes,(u8)stride,false);
}

#define GXSetArray(attr, data, stride) native_checked_array((attr), (data), (stride))
#include <sysdolphin/baselib/pobj.c>
#undef GXSetArray
