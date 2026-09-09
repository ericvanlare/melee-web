/* Synthetic fixture with GX test doubles. Link the real HSD bridge and state
 * code, then verify the source-to-SDK call boundary without a GPU or game data.
 * These doubles are confined to this test executable. */
#include "hsd_pobj_bridge.h"
#include <dolphin/gx.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/tobj.h>
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const MeleeWebPObjView* expected;
static unsigned arrays, descriptors, formats, clears, lists, culls;
static unsigned total_calls;
static unsigned position_loads, normal_loads, texture_loads;
static const MeleeWebPObjPalette* expected_palette;
// Original TObjSetup owns this source context; only the fixture is hydrated here.
extern HSD_TObj* tobj_head;

static const MeleeWebPObjAttribute* expected_attribute(GXAttr attr) {
    for (unsigned i = 0; i < expected->attribute_count; ++i)
        if (expected->attributes[i].attr == attr) return &expected->attributes[i];
    assert(0);
    return NULL;
}

void GXClearVtxDesc(void) { ++clears; ++total_calls; }
void GXSetVtxDesc(GXAttr attr, GXAttrType type) {
    assert(type == expected_attribute(attr)->attr_type);
    ++descriptors; ++total_calls;
}
void GXSetVtxAttrFmt(GXVtxFmt fmt, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac) {
    const MeleeWebPObjAttribute* a = expected_attribute(attr);
    assert(fmt == GX_VTXFMT0 && attr == a->attr && cnt == a->comp_cnt && type == a->comp_type && frac == a->frac);
    ++formats; ++total_calls;
}
void GXSetArray(GXAttr attr, const void* data, u32 size, u8 stride, bool le) {
    const MeleeWebPObjAttribute* a = expected_attribute(attr);
    assert(attr == a->attr && data == a->data && size == a->byte_size && stride == a->stride && !le);
    ++arrays; ++total_calls;
}
void GXSetCullMode(GXCullMode mode) {
    assert(mode == GX_CULL_BACK); ++culls; ++total_calls;
}
void GXCallDisplayList(const void* data, u32 size) {
    assert(data == expected->display && size == expected->display_byte_size);
    ++lists; ++total_calls;
}
void GXSetCurrentMtx(u32 id) { assert(id == GX_PNMTX0); ++total_calls; }
void GXLoadPosMtxImm(const void* matrix, u32 id) {
    assert(expected_palette && id / 3 < expected_palette->count && id % 3 == 0);
    assert(memcmp(matrix, expected_palette->position[id / 3], sizeof(Mtx)) == 0);
    ++position_loads; ++total_calls;
}
void GXLoadNrmMtxImm(const void* matrix, u32 id) {
    assert(expected_palette && id / 3 < expected_palette->count && id % 3 == 0);
    assert(memcmp(matrix, expected_palette->normal[id / 3], sizeof(Mtx)) == 0);
    ++normal_loads; ++total_calls;
}
void GXLoadTexMtxImm(const void* matrix, u32 id, GXTexMtxType type) {
    assert(expected_palette && id >= GX_TEXMTX0 && type == GX_MTX3x4);
    assert((id - GX_TEXMTX0) / 3 < expected_palette->count);
    assert(memcmp(matrix, expected_palette->texture[(id - GX_TEXMTX0) / 3], sizeof(Mtx)) == 0);
    ++texture_loads; ++total_calls;
}

int main(void) {
    unsigned char positions[12] = {0}, normals[6] = {0}, display[32] = {0};
    MeleeWebPObjAttribute attributes[] = {
        {GX_VA_POS, GX_INDEX16, GX_POS_XYZ, GX_S16, 10, 6, positions, sizeof positions},
        {GX_VA_NRM, GX_INDEX8, GX_NRM_XYZ, GX_S16, 14, 6, normals, sizeof normals},
    };
    MeleeWebPObjView view = {attributes, 2, display, sizeof display, 0x8000};
    expected = &view;
    char error[128];
    assert(melee_web_pobj_draw(&view, error, sizeof error));
    assert(error[0] == 0 && arrays == 2 && formats == 2 && descriptors == 2 && lists == 1 && culls == 1 && clears == 3);
    /* Same descriptor address on the second call, changed buffer and frac. */
    unsigned char replacement[18] = {0};
    attributes[0].data = replacement;
    attributes[0].byte_size = sizeof replacement;
    attributes[0].frac = 4;
    assert(melee_web_pobj_draw(&view, error, sizeof error));
    assert(arrays == 4 && formats == 4 && descriptors == 4 && lists == 2 && culls == 2 && clears == 6);
    unsigned before = total_calls;
    view.flags = 0x3000;
    assert(!melee_web_pobj_draw(&view, error, sizeof error) && strstr(error, "rigid"));
    view.flags = 0x8000;
    view.display_byte_size = 31;
    assert(!melee_web_pobj_draw(&view, error, sizeof error) && strstr(error, "size"));
    view.display_byte_size = 65536u * 32;
    assert(!melee_web_pobj_draw(&view, error, sizeof error));
    view.display_byte_size = 32;
    attributes[0].stride = 256;
    assert(!melee_web_pobj_draw(&view, error, sizeof error));
    attributes[0].stride = 6;
    attributes[0].byte_size = 5;
    assert(!melee_web_pobj_draw(&view, error, sizeof error));
    attributes[0].byte_size = 18;
    attributes[1].attr = GX_VA_POS;
    assert(!melee_web_pobj_draw(&view, error, sizeof error));
    attributes[1].attr = GX_VA_NRM;
    attributes[1].comp_cnt = GX_NRM_NBT3;
    assert(!melee_web_pobj_draw(&view, error, sizeof error));
    attributes[1].comp_cnt = GX_NRM_XYZ;
    assert(!melee_web_pobj_draw(NULL, error, sizeof error));
    assert(!melee_web_pobj_draw(NULL, NULL, 0));
    view.flags = 0xc000;
    assert(melee_web_pobj_draw(&view, error, sizeof error));
    assert(before == total_calls);

    // The source's single-influence root path uses world directly; weighted
    // envelopes use world times inverse bind before blending and the camera.
    MeleeWebSkinJoint joints[3] = {0};
    const float identity[3][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0}};
    for (unsigned i = 0; i < 3; ++i) {
        joints[i].parent = i ? 0 : UINT32_MAX;
        joints[i].flags = JOBJ_LIGHTING | (i ? JOBJ_SKELETON : JOBJ_SKELETON_ROOT);
        joints[i].has_inverse_bind = 1;
        memcpy(joints[i].world, identity, sizeof identity);
        memcpy(joints[i].inverse_bind, identity, sizeof identity);
    }
    joints[1].world[0][3] = 10;
    joints[1].inverse_bind[0][3] = -8;
    joints[2].world[0][3] = 20;
    joints[2].inverse_bind[0][3] = -12;
    MeleeWebSkinSkeleton* skeleton = melee_web_skin_create(joints, 3, error, sizeof error);
    assert(skeleton && !error[0]);
    const MeleeWebSkinInfluence single[] = {{1, 1}};
    const MeleeWebSkinInfluence blended[] = {{1, .25f}, {2, .75f}};
    const MeleeWebSkinEnvelope envelopes[] = {{single, 1}, {blended, 2}};
    MeleeWebPObjPalette palette;
    HSD_TObj reflection = {.flags = TEX_COORD_REFLECTION};
    tobj_head = &reflection;
    assert(melee_web_pobj_prepare_palette(skeleton, 0, envelopes, 2, identity, 4,
                                          &palette, error, sizeof error));
    assert(palette.count == 2 && palette.normal_mask == 3 && palette.texture_mask == 3);
    assert(palette.position[0][0][3] == 10 && palette.position[1][0][3] == 6.5f);
    assert(palette.normal[0][0][0] == 1 && palette.normal[1][0][3] == 0);
    assert(before == total_calls); // preparation does not mutate GX.
    // Non-root skeleton ownership applies the original model-node right matrix.
    assert(melee_web_pobj_prepare_palette(skeleton, 1, envelopes, 2, identity, 4,
                                          &palette, error, sizeof error));
    assert(palette.position[0][0][3] == 10 && palette.position[1][0][3] == 14.5f);
    assert(melee_web_pobj_prepare_palette(skeleton, 0, envelopes, 2, identity, 4,
                                          &palette, error, sizeof error));

    MeleeWebPObjAttribute skin_attributes[] = {
        {GX_VA_PNMTXIDX, GX_DIRECT, 0, GX_F32, 0, 0, NULL, 0},
        attributes[0], attributes[1],
    };
    view = (MeleeWebPObjView){skin_attributes, 3, display, sizeof display, 0xa000};
    expected_palette = &palette;
    assert(melee_web_pobj_draw_palette(&view, &palette, error, sizeof error));
    assert(position_loads == 2 && normal_loads == 2 && texture_loads == 2);

    joints[1].world[0][3] = 12;
    assert(melee_web_skin_update(skeleton, joints, 3, error, sizeof error));
    assert(melee_web_pobj_prepare_palette(skeleton, 0, envelopes, 2, identity, 4,
                                          &palette, error, sizeof error));
    assert(palette.position[0][0][3] == 12 && palette.position[1][0][3] == 7);
    joints[1].parent = 2; // bad replacement must leave the previous pose intact.
    assert(!melee_web_skin_update(skeleton, joints, 3, error, sizeof error));
    assert(melee_web_pobj_prepare_palette(skeleton, 0, envelopes, 2, identity, 4,
                                          &palette, error, sizeof error));
    assert(palette.position[0][0][3] == 12);
    joints[1].parent = 0;
    joints[1].world[0][0] = FLT_MAX;
    joints[1].inverse_bind[0][0] = 2;
    assert(melee_web_skin_update(skeleton, joints, 3, error, sizeof error));
    MeleeWebPObjPalette unchanged = palette;
    assert(!melee_web_pobj_prepare_palette(skeleton, 0, envelopes, 2, identity, 4,
                                           &palette, error, sizeof error));
    assert(memcmp(&unchanged, &palette, sizeof palette) == 0 && strstr(error, "nonfinite"));
    tobj_head = NULL;
    melee_web_skin_destroy(skeleton);
    // Referencing only index0 needs its components, not unused stride padding.
    attributes[0].stride = 12;
    attributes[0].byte_size = 6;
    view = (MeleeWebPObjView){attributes, 2, display, sizeof display, 0x8000};
    assert(melee_web_pobj_draw(&view, error, sizeof error));
    // Original PObj descriptor setup forwards direct RGBA8 without a GX array.
    MeleeWebPObjAttribute color_attributes[] = {
        attributes[0], {GX_VA_CLR0, GX_DIRECT, GX_CLR_RGBA, GX_RGBA8, 0, 4, NULL, 0},
    };
    view = (MeleeWebPObjView){color_attributes, 2, display, sizeof display, 0x8000};
    const unsigned old_arrays = arrays, old_formats = formats, old_descriptors = descriptors;
    assert(melee_web_pobj_draw(&view, error, sizeof error));
    assert(arrays == old_arrays + 1 && formats == old_formats + 2 && descriptors == old_descriptors + 2);

    // GX_VA_NBT is an alias descriptor after TEX7, but HSD uses one indexed
    // packet value for the nine interleaved normal/binormal/tangent scalars.
    unsigned char nbt_data[18] = {0};
    MeleeWebPObjAttribute nbt_attributes[] = {
        {GX_VA_POS, GX_INDEX8, GX_POS_XYZ, GX_S16, 10, 6, replacement, sizeof replacement},
        {GX_VA_NBT, GX_INDEX8, GX_NRM_NBT, GX_S16, 0, 18, nbt_data, sizeof nbt_data},
    };
    MeleeWebPObjView nbt_view = {nbt_attributes, 2, display, sizeof display, 0x8000};
    expected = &nbt_view;
    assert(melee_web_pobj_draw(&nbt_view, error, sizeof error));
    nbt_attributes[1].byte_size = 17;
    assert(!melee_web_pobj_draw(&nbt_view, error, sizeof error) && strstr(error, "span"));
    nbt_attributes[1].byte_size = sizeof nbt_data;
    nbt_attributes[1].comp_cnt = GX_NRM_NBT3;
    assert(!melee_web_pobj_draw(&nbt_view, error, sizeof error) && strstr(error, "format"));
    nbt_attributes[1].comp_cnt = GX_NRM_NBT;
    nbt_attributes[1].attr_type = GX_DIRECT;
    assert(!melee_web_pobj_draw(&nbt_view, error, sizeof error) && strstr(error, "format"));
    puts("HSD original PObj bridge trace: passed");
    return 0;
}
