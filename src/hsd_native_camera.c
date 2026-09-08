#include "gameplay_compat.h"
/* Keep original camera loading, projection, matrix and destruction methods.
 * The host frame boundary restores this source-local pointer explicitly;
 * CObjRelease itself does not clear it until whole-library amnesia. */
#include <sysdolphin/baselib/cobj.c>
int melee_web_native_camera_restore_current(void* expected,void* previous)
{
    if(current!=expected)return 0;
    current=previous;return 1;
}
