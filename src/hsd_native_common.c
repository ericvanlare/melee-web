#include <melee/ft/fighter.h>
#include <melee/ft/ft_0C8C.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/jobj.h>
#include <stdint.h>
#include <string.h>

static HSD_Joint* expected_descriptor;
static HSD_JObj* captured_root;
static void (*persistent_capture)(void*, void*);
static void* persistent_context;
typedef struct RetainedRoot {
    void* context;
    HSD_JObj* root;
} RetainedRoot;
static RetainedRoot retained_roots[2];
static unsigned retained_root_count;
static HSD_JObj* capture_joint_load(HSD_Joint* descriptor)
{
    if (!expected_descriptor || descriptor != expected_descriptor || (captured_root && !persistent_capture))
        HSD_Panic(__FILE__, __LINE__, "Unexpected native common joint load");
    HSD_JObj* root = HSD_JObjLoadJoint(descriptor);
    if (persistent_capture) persistent_capture(persistent_context, root);
    else captured_root = root;
    return root;
}
/* The original consumer discards its returned root after retaining its MObj.
 * Capture that real loader return so the common owner can retain and release
 * the JObj explicitly; no constructor or common-material behavior is
 * substituted. */
#define HSD_JObjLoadJoint capture_joint_load
#include <melee/ft/ft_0C8C.c>
#include <melee/ft/ftCo_800C7CA0.c>
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

int melee_web_native_common_capture_begin(void* descriptor,
    void (*capture)(void*, void*), void* context)
{
    if (expected_descriptor || persistent_capture || retained_root_count ||
        !descriptor || !capture || !context) return 0;
    expected_descriptor = descriptor;
    persistent_capture = capture;
    persistent_context = context;
    return 1;
}

int melee_web_native_common_retain_root(void* context, HSD_JObj* root)
{
    if (!persistent_capture || persistent_context != context || !root ||
        retained_root_count >= sizeof(retained_roots) / sizeof(retained_roots[0]))
        return 0;
    /* HSD_JObjLoadJoint returns an unowned root. The individual reference is
     * the explicit owner that survives the consumer's discarded return value
     * until the common context or native world releases it. */
    HSD_JObjRefThis(root);
    retained_roots[retained_root_count++] = (RetainedRoot) {context, root};
    return 1;
}

static void release_root(RetainedRoot* retained)
{
    if (!retained->root) return;
    HSD_JObjRemoveAll(retained->root);
    HSD_JObjUnrefThis(retained->root);
    retained->context = NULL;
    retained->root = NULL;
}

int melee_web_native_common_release_roots(void* context)
{
    if (!retained_root_count) return 1;
    if (!context || persistent_context != context) return 0;
    while (retained_root_count) release_root(&retained_roots[--retained_root_count]);
    return 1;
}

void melee_web_native_common_release_all(void)
{
    while (retained_root_count) release_root(&retained_roots[--retained_root_count]);
}

int melee_web_native_common_capture_end(void* context)
{
    if (!persistent_capture || persistent_context != context || retained_root_count) return 0;
    expected_descriptor = NULL;
    persistent_capture = NULL;
    persistent_context = NULL;
    return 1;
}
