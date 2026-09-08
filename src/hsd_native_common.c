#include <melee/ft/fighter.h>
#include <melee/ft/ft_0C8C.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/jobj.h>
#include <stdint.h>
#include <string.h>

static HSD_Joint* expected_descriptor;
static HSD_JObj* captured_root;
static HSD_JObj* capture_joint_load(HSD_Joint* descriptor)
{
    if (!expected_descriptor || descriptor != expected_descriptor || captured_root)
        HSD_Panic(__FILE__, __LINE__, "Unexpected native common joint load");
    captured_root = HSD_JObjLoadJoint(descriptor);
    return captured_root;
}
/* The original consumer discards its returned root after retaining its MObj.
 * Capture that real loader return solely to give the native GObj ownership;
 * no constructor or common-material behavior is substituted. */
#define HSD_JObjLoadJoint capture_joint_load
#include <melee/ft/ft_0C8C.c>
#undef HSD_JObjLoadJoint

HSD_JObj* melee_web_native_common_load(HSD_Joint* descriptor, const uint8_t diffuse[4])
{
    if (expected_descriptor)
        HSD_Panic(__FILE__, __LINE__, "Native common material initialization cannot be reentered");
    HSD_Joint* previous_joint = Fighter_804D6504;
    ftCommonData* previous_common = p_ftCommonData;
    HSD_MObj* previous_material = ft_804D6588;
    ftCommonData common = {0};
    /* Exact complete read set of retained ftCo_800C8F6C, not a partial full
     * common-data publication or a promise that the other22 roots are ready. */
    memcpy(&common.x7D8, diffuse, sizeof(GXColor));
    expected_descriptor = descriptor;
    captured_root = NULL;
    Fighter_804D6504 = descriptor;
    p_ftCommonData = &common;
    ftCo_800C8F6C();
    HSD_JObj* result = captured_root;
    Fighter_804D6504 = previous_joint;
    p_ftCommonData = previous_common;
    ft_804D6588 = previous_material;
    captured_root = NULL;
    expected_descriptor = NULL;
    return result;
}
