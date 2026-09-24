#include "gameplay_bootstrap.h"
#include "hsd_native_joint.h"
#include <dolphin/os.h>
#include <sysdolphin/baselib/class.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/id.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/fobj.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "CHECK failed: %s at %d\n", #c, __LINE__); abort(); } } while (0)
#include <stdio.h>
#include <string.h>

/* Native NODE affects one joint; BRANCH affects its subtree only. */
static void original_visibility_channels(void)
{
    HSD_JObj branch={0},child={0},sibling={0},outside={0};
    branch.child=&child;child.next=&sibling;branch.next=&outside;
    HSD_ObjData visibility={0};visibility.fv=0.5f;
    JObjUpdateFunc(&branch,HSD_A_J_BRANCH,&visibility);
    CHECK((branch.flags&JOBJ_HIDDEN)&&(child.flags&JOBJ_HIDDEN)&&
          (sibling.flags&JOBJ_HIDDEN)&&!(outside.flags&JOBJ_HIDDEN));
    visibility.fv=1.0f;JObjUpdateFunc(&branch,HSD_A_J_BRANCH,&visibility);
    CHECK(!(branch.flags&JOBJ_HIDDEN)&&!(child.flags&JOBJ_HIDDEN)&&
          !(sibling.flags&JOBJ_HIDDEN));
    visibility.fv=0.0f;JObjUpdateFunc(&branch,HSD_A_J_NODE,&visibility);
    CHECK((branch.flags&JOBJ_HIDDEN)&&!(child.flags&JOBJ_HIDDEN)&&
          !(sibling.flags&JOBJ_HIDDEN));
    HSD_JObjClearFlagsAll(&branch,JOBJ_HIDDEN);
    // Exact Donkey FallAerialF branch stream. Its start bits represent -34
    // in the original FObj; the 16-frame clip ends before its first callback.
    u8 bytes[]={1,0,0};
    HSD_FObjDesc track={0};track.ad=bytes;track.length=sizeof(bytes);
    track.startframe=-34;track.type=HSD_A_J_BRANCH;
    track.frac_value=track.frac_slope=0x88;
    HSD_AObjDesc desc={0};desc.end_frame=16;desc.fobjdesc=&track;
    HSD_AObj* animation=HSD_AObjLoadDesc(&desc);
    CHECK(animation);
    for(unsigned cycle=0;cycle<3;cycle++) {
        HSD_AObjReqAnim(animation,0);
        for(unsigned frame=0;frame<17;frame++)
            HSD_AObjInterpretAnim(animation,&branch,JObjUpdateFunc);
        CHECK(!(branch.flags&JOBJ_HIDDEN)&&!(child.flags&JOBJ_HIDDEN));
    }
    HSD_AObjRemove(animation);
}

static unsigned rejected_callback_deletions;
static void callback_deletion(HSD_GObj* object)
{
    char error[256];
    MeleeWebNativeJoint* native = object->user_data;
    CHECK(!melee_web_native_joint_destroy(native, error, sizeof(error)));
    MeleeWebNativeJointStats stats;
    CHECK(melee_web_native_joint_stats(native, &stats, error, sizeof(error)));
    ++rejected_callback_deletions;
}

static void managed_native_lifetimes(int replace_heap)
{
    char error[256];
    MeleeWebNativeJointDesc joints[2] = {0};
    for (unsigned i = 0; i < 2; ++i) {
        joints[i].source_offset = 64 * i;
        joints[i].next = joints[i].child = joints[i].dobj = UINT32_MAX;
        joints[i].scale[0] = joints[i].scale[1] = joints[i].scale[2] = 1;
    }
    joints[0].child = 1; joints[0].dobj = 0;
    joints[0].flags = JOBJ_SKELETON | JOBJ_SKELETON_ROOT | JOBJ_LIGHTING;
    joints[1].flags = JOBJ_SKELETON;
    const uint8_t vertices[36] = {0};
    const uint8_t display[32] = {0x90, 0, 3, 0, 1, 2};
    const MeleeWebPObjAttribute attribute = {9, 2, 1, 4, 0, 12, vertices, sizeof(vertices)};
    MeleeWebSkinInfluence influence = {1, 1};
    const MeleeWebSkinEnvelope envelope = {&influence, 1};
    const MeleeWebNativePObjDesc polygon = {160, UINT32_MAX,
        {&attribute, 1, display, sizeof(display), 0xa000}, &envelope, 1};
    MeleeWebNativeDObjDesc dobj = {128, UINT32_MAX, 0, 0};
    MeleeWebNativeMaterialDesc material = {0};
    material.source_offset = 192; material.material.rendermode = 4;
    material.material.alpha = 1; material.material.diffuse[0] = 123;
    MeleeWebNativeGraph graph = {joints, &dobj, &polygon, &material, 2, 1, 1, 1, 0};
    MeleeWebNativeJoint* stale = NULL;
    for (unsigned pass = 0; pass < 2; ++pass) {
        CHECK(melee_web_gameplay_startup(4U * 1024U * 1024U, error, sizeof(error)));
        if (stale) {
            MeleeWebNativeJointStats stats;
            CHECK(!melee_web_native_joint_stats(stale, &stats, error, sizeof(error)));
            CHECK(melee_web_native_joint_destroy(stale, error, sizeof(error)));
        }
        // Reject graph cycles and invalid envelope references before any
        // original recursive constructor or reference resolver is entered.
        dobj.next = 0;
        CHECK(!melee_web_native_joint_create(&graph, error, sizeof(error)));
        dobj.next = UINT32_MAX;
        influence.joint = 2;
        CHECK(!melee_web_native_joint_create(&graph, error, sizeof(error)));
        influence.joint = 1;
        const uint8_t diffuse[4] = {7, 11, 13, 17};
        MeleeWebNativeJoint* handle = melee_web_native_common_joint_create(&graph, diffuse, error, sizeof(error));
        CHECK(handle);
        MeleeWebNativeJointStats stats;
        CHECK(melee_web_native_joint_stats(handle, &stats, error, sizeof(error)));
        CHECK(stats.joints == 2 && stats.dobjs == 1 && stats.pobjs == 1 && stats.materials == 1);
        CHECK(stats.resolved_envelopes == 1 && memcmp(stats.first_diffuse, diffuse, 4) == 0);
        HSD_Joint* descriptor = melee_web_native_joint_descriptor(handle,error,sizeof(error));
        HSD_PObjDesc* native_polygon = descriptor->u.dobjdesc->pobjdesc;
        CHECK(native_polygon->display != display && !((uintptr_t)native_polygon->display & 31));
        CHECK(!memcmp(native_polygon->display,display,sizeof(display)));
        native_polygon->display[31] = 0xa5;
        CHECK(display[31] == 0); /* Source-loader writes cannot poison archive input. */
        HSD_GObj* owner = ((HSD_GObj**) HSD_GObj_Entities)[0];
        while (owner && owner->user_data != handle) owner = owner->next;
        CHECK(owner && HSD_GObj_SetupProc(owner, callback_deletion, 0));
        const unsigned previous_rejections = rejected_callback_deletions;
        CHECK(melee_web_gameplay_step(error, sizeof(error)));
        CHECK(rejected_callback_deletions == previous_rejections + 1);
        HSD_GObjProc_8038FED4(owner);
        if (replace_heap) {
            /* This isolated process deliberately invalidates SDK identity.
             * Original owned storage is retained, never freed through the
             * replacement heap. The process exits after proving rejection. */
            static _Alignas(32) unsigned char replacement[65536];
            void* start = OSInitAlloc(replacement, replacement + sizeof(replacement), 1);
            CHECK(start && OSCreateHeap(start, replacement + sizeof(replacement)) == 0);
            const int available = OSCheckHeap(0);
            CHECK(!melee_web_native_joint_destroy(handle, error, sizeof(error)));
            CHECK(!melee_web_gameplay_shutdown(error, sizeof(error)));
            CHECK(OSCheckHeap(0) == available);
            puts("native teardown safely rejected replacement heap");
            return;
        }
        // A second independently hydrated graph must have separate source
        // descriptor addresses/ID entries even with identical archive offsets.
        MeleeWebNativeJoint* other = melee_web_native_joint_create(&graph, error, sizeof(error));
        CHECK(other);
        HSD_Joint* other_descriptor = melee_web_native_joint_descriptor(other,error,sizeof(error));
        CHECK(other_descriptor->u.dobjdesc->pobjdesc->display[31] == 0);
        CHECK(native_polygon->display[31] == 0xa5);
        CHECK(melee_web_native_joint_stats(other, &stats, error, sizeof(error)) && stats.first_diffuse[0] == 123);
        CHECK(melee_web_native_joint_destroy(other, error, sizeof(error)));
        CHECK(melee_web_native_joint_stats(handle, &stats, error, sizeof(error)));
        CHECK(melee_web_gameplay_shutdown(error, sizeof(error)));
        CHECK(!melee_web_native_joint_stats(handle, &stats, error, sizeof(error)));
        stale = handle;
    }
    CHECK(melee_web_native_joint_destroy(stale, error, sizeof(error)));
}

static void descriptor_only_source_lifetime(void)
{
    char error[256];
    MeleeWebNativeJointDesc joint = {0};
    joint.child = joint.next = joint.dobj = UINT32_MAX;
    joint.scale[0] = joint.scale[1] = joint.scale[2] = 1;
    MeleeWebNativeGraph graph = {&joint, NULL, NULL, NULL, 1, 0, 0, 0, 0};
    MeleeWebNativeJoint* descriptors = melee_web_native_joint_hydrate(&graph, error, sizeof(error));
    CHECK(descriptors);
    HSD_Joint* descriptor = melee_web_native_joint_descriptor(descriptors, error, sizeof(error));
    CHECK(descriptor && !melee_web_native_joint_object(descriptors, error, sizeof(error)));
    for (unsigned pass = 0; pass < 2; ++pass) {
        CHECK(melee_web_gameplay_startup(4U * 1024U * 1024U, error, sizeof(error)));
        CHECK(melee_web_native_world_enable(error, sizeof(error)));
        CHECK(melee_web_native_world_enable(error, sizeof(error)));
        HSD_JObj* object = HSD_JObjLoadJoint(descriptor);
        CHECK(object && HSD_IDGetData((u32)descriptor, NULL) == object);
        /* Original metal initialization registers descriptor aliases whose
         * identity differs from jobj->id. JObjRelease leaves those entries. */
        HSD_Joint aliases[3] = {0};
        for (unsigned i = 0; i < 3; ++i)
            HSD_IDInsertToTable(NULL, (u32)&aliases[i], object);
        CHECK(HSD_IDGetAllocData()->used == 4);
        HSD_JObjRemoveAll(object);
        CHECK(!HSD_IDGetData((u32)descriptor, NULL));
        CHECK(HSD_IDGetAllocData()->used == 3);
        for (unsigned i = 0; i < 3; ++i)
            CHECK(HSD_IDGetData((u32)&aliases[i], NULL) == object);
        CHECK(melee_web_gameplay_shutdown(error, sizeof(error)));
        CHECK(HSD_IDGetAllocData()->used == 0);
        for (unsigned i = 0; i < 3; ++i)
            CHECK(!HSD_IDGetData((u32)&aliases[i], NULL));
        CHECK(melee_web_native_joint_descriptor(descriptors,error,sizeof(error)) == descriptor);
    }
    CHECK(melee_web_native_joint_destroy(descriptors,error,sizeof(error)));
}

/* Authored data only. Tests original class allocation, descriptor identity,
 * parent links and destruction before each arena is released. */
int main(int argc, char** argv)
{
    if(argc==2 && (!strcmp(argv[1],"--terminal-branch-linear") ||
                   !strcmp(argv[1],"--terminal-branch-spline") ||
                   !strcmp(argv[1],"--terminal-pass-endpoint") ||
                   !strcmp(argv[1],"--terminal-stop-ceil-endpoint"))) {
        char error[256];
        CHECK(melee_web_gameplay_startup(4U*1024U*1024U,error,sizeof(error)));
        CHECK(melee_web_native_world_enable(error,sizeof(error)));
        u8 bytes[]={!strcmp(argv[1],"--terminal-branch-linear")?2:3,0,0};
        const unsigned delay=!strcmp(argv[1],"--terminal-pass-endpoint")?25:
            !strcmp(argv[1],"--terminal-stop-ceil-endpoint")?8:0;
        HSD_FObjDesc track={0};track.ad=bytes;track.length=sizeof(bytes);
        track.startframe=-(s16)delay;
        track.type=HSD_A_J_BRANCH;track.frac_value=track.frac_slope=0x88;
        HSD_AObjDesc desc={0};desc.end_frame=delay?delay:1;desc.fobjdesc=&track;
        HSD_AObj* animation=HSD_AObjLoadDesc(&desc);
        HSD_JObj joint={0};
        HSD_AObjReqAnim(animation,0);
        // Pass/StopCeil have delay == end: the source interprets the terminal
        // frame before stopping. Only earlier frames are dormant.
        for(unsigned frame=0;frame<delay;frame++) {
            HSD_AObjInterpretAnim(animation,&joint,JObjUpdateFunc);
            CHECK(!(joint.flags&JOBJ_HIDDEN));
        }
        HSD_AObjInterpretAnim(animation,&joint,JObjUpdateFunc);
        // The terminal single-datum update is defined as the authored last
        // value: each interpolation type's own zero-duration path emits p1.
        // Zero hides the branch without changing the parser state.
        CHECK(joint.flags&JOBJ_HIDDEN);
        puts("original terminal branch visibility constant passed");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--replace-heap") == 0) {
        managed_native_lifetimes(1);
        return 0;
    }
    CHECK(argc == 1);
    char error[256];
    for (unsigned iteration = 0; iteration < 2; ++iteration) {
        CHECK(melee_web_gameplay_startup(4U * 1024U * 1024U, error, sizeof(error)));
        HSD_IDInitAllocData();
        HSD_IDSetup();
        CHECK(melee_web_native_world_enable(error,sizeof(error)));
        original_visibility_channels();
        HSD_Joint child = {0}, root = {0};
        root.scale = child.scale = (Vec3) {1, 1, 1};
        root.child = &child;
        child.position.x = 3;
        HSD_JObj* object = HSD_JObjLoadJoint(&root);
        CHECK(object && object->child && object->child->parent == object);
        CHECK(object->child->translate.x == 3);
        CHECK(HSD_IDGetData((u32) &root, NULL) == object);
        CHECK(HSD_IDGetData((u32) &child, NULL) == object->child);
        HSD_JObjRemoveAll(object);
        CHECK(HSD_IDGetData((u32) &root, NULL) == NULL);
        _HSD_IDForgetMemory(NULL, NULL);
        hsdForgetClassLibrary(NULL);
        CHECK(melee_web_gameplay_shutdown(error, sizeof(error)));
    }
    managed_native_lifetimes(0);
    descriptor_only_source_lifetime();
    puts("original native joint allocation/reference/destruction/restart passed");
    return 0;
}
