#include <sysdolphin/baselib/debug.h>
/* Original SDK GXTev.c unconditionally asserts for this hardware operation. */
void GXSetTevClampMode(int stage,int mode)
{
    (void)stage; (void)mode;
    HSD_Panic(__FILE__,__LINE__,"GXSetTevClampMode: not available on this hardware");
}
