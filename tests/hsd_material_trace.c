/* GX recorders exist only in this test; actual original HSD material, TEV,
 * texture and state code is linked below them. No GPU or proprietary data. */
#include "hsd_material_bridge.h"
#include "hsd_host_support.h"
#include <dolphin/gx.h>
#include <assert.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned gx_calls;
static unsigned texture_initializations, palette_initializations, texture_loads, lod_calls;
static unsigned texture_destroys, palette_destroys;
static unsigned assigned_texgens, assigned_stages, uv_generators, reflection_generators;
static unsigned ids[16], next_id;
static unsigned palette_ids[4], palette_loads, active_texture_count = 2;
static unsigned expected_uv_coord = 1;
static int saw_blend_constant, saw_diffuse_color, saw_specular_color;

typedef struct TestTexture { unsigned id, tlut; const void* data; } TestTexture;
typedef struct TestPalette { unsigned id; const void* data; u16 entries; } TestPalette;
_Static_assert(sizeof(TestTexture) <= sizeof(GXTexObj), "test opaque texture storage");
_Static_assert(sizeof(TestPalette) <= sizeof(GXTlutObj), "test opaque palette storage");

static void record_color(GXColor color)
{
    if (color.r == 51 || color.g == 51 || color.b == 51 || color.a == 51) saw_blend_constant = 1;
    if (color.r == 200 && color.g == 100 && color.b == 50) saw_diffuse_color = 1;
    if (color.r == 55 && color.g == 66 && color.b == 77) saw_specular_color = 1;
}

void GXInitTexObj(GXTexObj* obj, const void* data, u16 width, u16 height,
                  GXTexFmt format, GXTexWrapMode wrapS, GXTexWrapMode wrapT, GXBool mipmap)
{
    assert(data && width == 8 && height == 8 && format == GX_TF_CMPR && !mipmap);
    assert(wrapS <= GX_MIRROR && wrapT <= GX_MIRROR);
    memset(obj, 0, sizeof(*obj));
    const TestTexture texture = {++next_id, 0, data};
    memcpy(obj, &texture, sizeof texture);
    ++texture_initializations;
}
void GXInitTexObjCI(GXTexObj* obj, const void* data, u16 width, u16 height,
                    GXCITexFmt format, GXTexWrapMode wrapS, GXTexWrapMode wrapT,
                    GXBool mipmap, u32 tlut)
{
    assert(data && width == 8 && height == 8 && format == GX_TF_C8 && !mipmap);
    assert(wrapS <= GX_MIRROR && wrapT <= GX_MIRROR);
    memset(obj, 0, sizeof(*obj));
    const TestTexture texture = {++next_id, tlut, data};
    memcpy(obj, &texture, sizeof texture);
    ++texture_initializations;
}
void GXInitTexObjTlut(GXTexObj* obj, u32 tlut)
{
    TestTexture texture;
    memcpy(&texture, obj, sizeof texture);
    texture.tlut = tlut;
    memcpy(obj, &texture, sizeof texture);
}
void GXDestroyTexObj(GXTexObj* obj)
{
    TestTexture texture;
    memcpy(&texture, obj, sizeof texture);
    assert(texture.id && texture.data);
    texture.id = 0;
    memcpy(obj, &texture, sizeof texture);
    ++texture_destroys;
}
void GXDestroyTlutObj(GXTlutObj* obj)
{
    TestPalette palette;
    memcpy(&palette, obj, sizeof palette);
    assert(palette.id && palette.data);
    palette.id = 0;
    memcpy(obj, &palette, sizeof palette);
    ++palette_destroys;
}
void GXInitTlutObj(GXTlutObj* obj, const void* data, GXTlutFmt format, u16 entries)
{
    assert(data && entries == 256 && format == GX_TL_RGB565);
    memset(obj, 0, sizeof(*obj));
    const TestPalette palette = {++next_id, data, entries};
    memcpy(obj, &palette, sizeof palette);
    ++palette_initializations;
}
void GXInitTexObjLOD(GXTexObj* obj, GXTexFilter min_filt, GXTexFilter mag_filt,
                     f32 min_lod, f32 max_lod, f32 lod_bias, GXBool bias_clamp,
                     GXBool do_edge_lod, GXAnisotropy max_aniso)
{
    (void)obj;
    assert(min_filt == GX_LINEAR && mag_filt == GX_LINEAR);
    assert(min_lod == 0 && max_lod == 0 && lod_bias == 0);
    assert(!bias_clamp && !do_edge_lod && max_aniso == GX_ANISO_1);
    ++lod_calls;
}
void GXLoadTexObj(GXTexObj* obj, GXTexMapID id)
{
    TestTexture texture;
    memcpy(&texture, obj, sizeof texture);
    assert(texture.id && texture.data && id == texture_loads % active_texture_count);
    if (active_texture_count == 1) assert(texture.tlut == GX_BIGTLUT0);
    assert(texture_loads < sizeof ids / sizeof ids[0]);
    ids[texture_loads++] = texture.id;
}
void GXLoadTlut(const GXTlutObj* obj, u32 idx)
{
    TestPalette palette;
    memcpy(&palette, obj, sizeof palette);
    assert(palette.id && palette.data && palette.entries == 256 && idx == GX_BIGTLUT0);
    assert(palette_loads < sizeof palette_ids / sizeof palette_ids[0]);
    palette_ids[palette_loads++] = palette.id;
}
void GXSetNumTevStages(u8 count) { assigned_stages = count; }
void GXSetNumTexGens(u8 count) { assigned_texgens = count; }
void GXSetTevKColor(GXTevKColorID id, GXColor color) { (void)id; record_color(color); }
void GXSetTevColor(GXTevRegID id, GXColor color) { (void)id; record_color(color); }
void GXSetTevOrder(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map, GXChannelID color)
{
    assert(stage < GX_MAX_TEVSTAGE);
    (void)coord; (void)map; (void)color;
    ++gx_calls;
}
void GXSetTexCoordGen2(GXTexCoordID coord, GXTexGenType function, GXTexGenSrc source,
                       u32 matrix, GXBool normalize, u32 post_matrix)
{
    (void)post_matrix;
    if (source == GX_TG_NRM) {
        assert(coord == GX_TEXCOORD0 && function == GX_TG_MTX3x4 && matrix == GX_TEXMTX0 && normalize);
        ++reflection_generators;
    } else {
        assert(coord == expected_uv_coord && source == GX_TG_TEX0 && function == GX_TG_MTX2x4 && !normalize);
        ++uv_generators;
    }
}

void GXInitLightAttn(GXLightObj* lt_obj, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2) { ++gx_calls; }
void GXInitLightColor(GXLightObj* lt_obj, GXColor color) { ++gx_calls; }
void GXLoadLightObjImm(GXLightObj* lt_obj, GXLightID light) { ++gx_calls; }
void GXLoadTexMtxImm(const void* mtx, u32 id, GXTexMtxType type) { ++gx_calls; }
void GXPixModeSync(void) { ++gx_calls; }
void GXSetAlphaCompare(GXCompare comp0, u8 ref0, GXAlphaOp op, GXCompare comp1, u8 ref1) { ++gx_calls; }
void GXSetAlphaUpdate(GXBool update_enable) { ++gx_calls; }
void GXSetBlendMode(GXBlendMode type, GXBlendFactor src_factor, GXBlendFactor dst_factor, GXLogicOp op) { ++gx_calls; }
void GXSetChanAmbColor(GXChannelID chan, GXColor amb_color) { ++gx_calls; }
void GXSetChanCtrl(GXChannelID chan, GXBool enable, GXColorSrc amb_src, GXColorSrc mat_src, u32 light_mask, GXDiffuseFn diff_fn, GXAttnFn attn_fn) { ++gx_calls; }
void GXSetChanMatColor(GXChannelID chan, GXColor mat_color) { ++gx_calls; }
void GXSetColorUpdate(GXBool update_enable) { ++gx_calls; }
void GXSetDither(GXBool dither) { ++gx_calls; }
void GXSetDstAlpha(GXBool enable, u8 alpha) { ++gx_calls; }
void GXSetNumChans(u8 nChans) { ++gx_calls; }
void GXSetTevAlphaIn(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c, GXTevAlphaArg d) { ++gx_calls; }
void GXSetTevAlphaOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out_reg) { ++gx_calls; }
void GXSetTevColorIn(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c, GXTevColorArg d) { ++gx_calls; }
void GXSetTevColorOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out_reg) { ++gx_calls; }
void GXSetTevColorS10(GXTevRegID id, GXColorS10 color) { ++gx_calls; }
void GXSetTevKAlphaSel(GXTevStageID stage, GXTevKAlphaSel sel) { ++gx_calls; }
void GXSetTevKColorSel(GXTevStageID stage, GXTevKColorSel sel) { ++gx_calls; }
void GXSetTevOp(GXTevStageID id, GXTevMode mode) { ++gx_calls; }
void GXSetTevSwapMode(GXTevStageID stage, GXTevSwapSel ras_sel, GXTevSwapSel tex_sel) { ++gx_calls; }
void GXSetZCompLoc(GXBool before_tex) { ++gx_calls; }
void GXSetZMode(GXBool compare_enable, GXCompare func, GXBool update_enable) { ++gx_calls; }
void GXClearVtxDesc(void) { ++gx_calls; }

int main(void)
{
    unsigned char pixels[2][32] = {{0}};
    MeleeWebHsdTextureDesc textures[2] = {{0}};
    for (unsigned i = 0; i < 2; ++i) {
        textures[i].flags = i ? 0x30081 : 0x40010;
        // Reflection ignores the stored source; preserve the full known enum.
        textures[i].source = i ? GX_TG_COLOR1 : GX_TG_TEX0;
        textures[i].scale[0] = textures[i].scale[1] = textures[i].scale[2] = 1;
        textures[i].wrap_s = textures[i].wrap_t = i ? 2 : 1;
        textures[i].repeat_s = textures[i].repeat_t = i ? 2 : 1;
        textures[i].blending = i ? 0.2f : 1;
        textures[i].min_filter = 5;
        textures[i].mag_filter = 1;
        textures[i].image_data = pixels[i];
        textures[i].image_bytes = sizeof pixels[i];
        textures[i].width = textures[i].height = 8;
        textures[i].format = GX_TF_CMPR;
    }
    MeleeWebHsdMaterialDesc descriptor = {.rendermode = 0x3c,
        .ambient = {255,255,255,255}, .diffuse = {200,100,50,255},
        .specular = {55,66,77,255}, .alpha = 1, .shininess = 20,
        .textures = textures, .texture_count = 2};
    char error[256];
    assert(melee_web_hsd_allocation_bytes() == 0);
    MeleeWebHsdMaterial* material = melee_web_hsd_material_create(&descriptor, error, sizeof error);
    assert(material && !error[0] && melee_web_hsd_allocation_bytes() > 0);
    assert(texture_initializations == 2 && palette_initializations == 0);
    for (unsigned frame = 0; frame < 2; ++frame) {
        melee_web_hsd_material_setup(material);
        assert(assigned_texgens == 2 && assigned_stages > 1 && assigned_stages <= 16);
        assert(saw_blend_constant && saw_diffuse_color && saw_specular_color);
        melee_web_hsd_material_unset(material);
    }
    assert(texture_loads == 4 && lod_calls == 4 && texture_initializations == 2);
    assert(ids[0] == ids[2] && ids[1] == ids[3] && ids[0] != ids[1]);
    assert(uv_generators == 2 && reflection_generators == 2 && gx_calls > 0);
    melee_web_hsd_material_destroy(material);
    assert(melee_web_hsd_allocation_bytes() == 0);
    assert(texture_destroys == 2 && palette_destroys == 0);
    descriptor.rendermode |= 0x40000000;
    assert(!melee_web_hsd_material_create(&descriptor, error, sizeof error) && error[0]);
    assert(melee_web_hsd_allocation_bytes() == 0);

    /* A palette with 256 entries uses HSD's big-TLUT pool, and both cached
     * identities must survive source setup's stack-object copies. */
    unsigned char indices[64] = {0}, palette[512] = {0};
    textures[0].format = GX_TF_C8;
    textures[0].image_data = indices;
    textures[0].image_bytes = sizeof indices;
    textures[0].palette_data = palette;
    textures[0].palette_bytes = sizeof palette;
    textures[0].palette_entries = 256;
    textures[0].palette_format = GX_TL_RGB565;
    descriptor.rendermode = 0x14;
    descriptor.texture_count = 1;
    active_texture_count = 1;
    expected_uv_coord = 0;
    texture_loads = 0;
    material = melee_web_hsd_material_create(&descriptor, error, sizeof error);
    assert(material && texture_initializations == 3 && palette_initializations == 1);
    for (unsigned frame = 0; frame < 2; ++frame) {
        melee_web_hsd_material_setup(material);
        assert(assigned_texgens == 1);
        melee_web_hsd_material_unset(material);
    }
    assert(texture_loads == 2 && ids[0] == ids[1]);
    assert(palette_loads == 2 && palette_ids[0] == palette_ids[1]);
    assert(texture_initializations == 3 && palette_initializations == 1);
    melee_web_hsd_material_destroy(material);
    assert(melee_web_hsd_allocation_bytes() == 0);
    assert(texture_destroys == 3 && palette_destroys == 1);

    // An unlit constant material has an actual original HSD setup path.
    descriptor.texture_count = 0;
    descriptor.rendermode = 1;
    material = melee_web_hsd_material_create(&descriptor, error, sizeof error);
    assert(material);
    melee_web_hsd_material_setup(material);
    assert(assigned_texgens == 0 && assigned_stages >= 1);
    melee_web_hsd_material_unset(material);
    melee_web_hsd_material_destroy(material);
    assert(melee_web_hsd_allocation_bytes() == 0);

    descriptor.texture_count = 1;
    descriptor.rendermode = 0x14;
    textures[0].source = GX_TG_POS;
    assert(!melee_web_hsd_material_create(&descriptor, error, sizeof error));
    textures[0].source = GX_TG_TEX0;
    textures[0].translation[0] = FLT_MAX;
    textures[0].scale[0] = 0.00001f;
    assert(!melee_web_hsd_material_create(&descriptor, error, sizeof error));
    assert(strstr(error, "matrix") && melee_web_hsd_allocation_bytes() == 0);
    assert(texture_destroys == texture_initializations && palette_destroys == palette_initializations);
    puts("HSD original material compile/setup/cache/free trace: passed");
    return 0;
}
