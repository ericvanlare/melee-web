/* Synthetic fixture with GX test doubles. Link the real HSD bridge and state
 * code, then verify the source-to-SDK call boundary without a GPU or game data.
 * These doubles are confined to this test executable. */
#include "hsd_pobj_bridge.h"
#include <dolphin/gx.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const MeleeWebPObjView* expected;
static unsigned arrays, descriptors, formats, clears, lists, culls;
static unsigned total_calls;

void GXClearVtxDesc(void) { ++clears; ++total_calls; }
void GXSetVtxDesc(GXAttr attr, GXAttrType type) {
    assert(attr == expected->attributes[descriptors % 2].attr);
    assert(type == expected->attributes[descriptors % 2].attr_type);
    ++descriptors; ++total_calls;
}
void GXSetVtxAttrFmt(GXVtxFmt fmt, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac) {
    const MeleeWebPObjAttribute* a = &expected->attributes[formats % 2];
    assert(fmt == GX_VTXFMT0 && attr == a->attr && cnt == a->comp_cnt && type == a->comp_type && frac == a->frac);
    ++formats; ++total_calls;
}
void GXSetArray(GXAttr attr, const void* data, u32 size, u8 stride, bool le) {
    const MeleeWebPObjAttribute* a = &expected->attributes[arrays % 2];
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
    puts("HSD original PObj bridge trace: passed");
    return 0;
}
