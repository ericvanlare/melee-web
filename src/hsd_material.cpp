#include "hsd_material.hpp"
#include <algorithm>
#include <vector>

namespace melee_web {
HsdMaterialHandle make_hsd_material(const DatMaterial& source) {
    std::vector<MeleeWebHsdTextureDesc> textures;
    textures.reserve(source.textures.size());
    for (const auto& t : source.textures) {
        MeleeWebHsdTextureDesc out{};
        out.flags = t.source_flags; out.source = t.source;
        std::copy(t.rotation.begin(), t.rotation.end(), out.rotation);
        std::copy(t.scale.begin(), t.scale.end(), out.scale);
        std::copy(t.translation.begin(), t.translation.end(), out.translation);
        out.wrap_s = t.sampler.wrap_s; out.wrap_t = t.sampler.wrap_t;
        out.repeat_s = t.repeat_s; out.repeat_t = t.repeat_t; out.blending = t.blending;
        out.min_filter = t.sampler.min_filter; out.mag_filter = t.sampler.mag_filter;
        out.anisotropy = t.sampler.anisotropy; out.lod_bias = t.sampler.lod_bias;
        out.bias_clamp = t.sampler.bias_clamp; out.edge_lod = t.sampler.edge_lod;
        out.image_data = t.image.bytes.data(); out.image_bytes = uint32_t(t.image.bytes.size());
        out.width = t.image.width; out.height = t.image.height; out.format = t.image.format;
        out.mipmap = t.image.mipmap; out.min_lod = t.image.min_lod; out.max_lod = t.image.max_lod;
        if (t.palette) {
            out.palette_data = t.palette->bytes.data(); out.palette_bytes = uint32_t(t.palette->bytes.size());
            out.palette_format = t.palette->format; out.palette_entries = t.palette->entries;
        }
        textures.push_back(out);
    }
    MeleeWebHsdMaterialDesc desc{};
    desc.rendermode = source.render_mode;
    std::copy(source.ambient.begin(), source.ambient.end(), desc.ambient);
    std::copy(source.diffuse.begin(), source.diffuse.end(), desc.diffuse);
    std::copy(source.specular.begin(), source.specular.end(), desc.specular);
    desc.alpha = source.alpha; desc.shininess = source.shininess;
    desc.textures = textures.data(); desc.texture_count = uint32_t(textures.size());
    char error[256];
    HsdMaterialHandle result(melee_web_hsd_material_create(&desc, error, sizeof(error)),
                              &melee_web_hsd_material_destroy);
    if (!result) throw DatError(error);
    return result;
}
}
