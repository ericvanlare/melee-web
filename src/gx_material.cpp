#include "gx_material.hpp"

namespace melee_web {
GxTexture::GxTexture(const DatTexture& source) : descriptor(source) {
    const auto& image = source.image;
    const auto& sampler = source.sampler;
    if (source.palette) {
        const auto& lut = *source.palette;
        palette_slot = lut.entries >= 256 ? GX_BIGTLUT0 : GX_TLUT0;
        GXInitTlutObj(&palette, lut.bytes.data(), GXTlutFmt(lut.format), lut.entries);
        GXInitTexObjCI(&texture, image.bytes.data(), image.width, image.height,
                      GXCITexFmt(image.format), GXTexWrapMode(sampler.wrap_s),
                      GXTexWrapMode(sampler.wrap_t), image.mipmap, palette_slot);
    } else {
        GXInitTexObj(&texture, image.bytes.data(), image.width, image.height,
                     GXTexFmt(image.format), GXTexWrapMode(sampler.wrap_s),
                     GXTexWrapMode(sampler.wrap_t), image.mipmap);
    }
    GXInitTexObjLOD(&texture, GXTexFilter(sampler.min_filter), GXTexFilter(sampler.mag_filter),
                    image.min_lod, image.max_lod, sampler.lod_bias, sampler.bias_clamp,
                    sampler.edge_lod, GXAnisotropy(sampler.anisotropy));
}

void apply_diffuse_material(GXColor color, GxTexture* resource) {
    GXSetChanMatColor(GX_COLOR0A0, color);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG,
                  GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTevStages(1);
    if (!resource) {
        GXSetNumTexGens(0);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
        return;
    }
    if (resource->descriptor.palette) GXLoadTlut(&resource->palette, resource->palette_slot);
    GXLoadTexObj(&resource->texture, GX_TEXMAP0);
    GXSetNumTexGens(1);
    GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    // Equivalent single-stage expressions from original TObjMakeTExp. BLEND
    // endpoints have already been validated and lowered by read_dat_texture.
    const auto& desc = resource->descriptor;
    if (desc.color_operation == TextureOperation::modulate)
        GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_RASC, GX_CC_TEXC, GX_CC_ZERO);
    else
        GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO,
                        desc.color_operation == TextureOperation::replace ? GX_CC_TEXC : GX_CC_RASC);
    if (desc.alpha_operation == TextureOperation::modulate)
        GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_RASA, GX_CA_TEXA, GX_CA_ZERO);
    else
        GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO,
                        desc.alpha_operation == TextureOperation::replace ? GX_CA_TEXA : GX_CA_RASA);
}
}
