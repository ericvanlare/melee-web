#ifndef MELEE_WEB_HSD_MATERIAL_BRIDGE_H
#define MELEE_WEB_HSD_MATERIAL_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebHsdTextureDesc {
    uint32_t flags, source;
    float rotation[3], scale[3], translation[3];
    uint32_t wrap_s, wrap_t;
    uint8_t repeat_s, repeat_t;
    float blending;
    uint32_t min_filter, mag_filter, anisotropy;
    float lod_bias;
    uint8_t bias_clamp, edge_lod;
    const void* image_data;
    uint32_t image_bytes;
    uint16_t width, height;
    uint32_t format;
    uint8_t mipmap;
    float min_lod, max_lod;
    const void* palette_data;
    uint32_t palette_bytes, palette_format;
    uint16_t palette_entries;
} MeleeWebHsdTextureDesc;

typedef struct MeleeWebHsdMaterialDesc {
    uint32_t rendermode;
    uint8_t ambient[4], diffuse[4], specular[4];
    float alpha, shininess;
    const MeleeWebHsdTextureDesc* textures;
    uint32_t texture_count;
} MeleeWebHsdMaterialDesc;

typedef struct MeleeWebHsdMaterial MeleeWebHsdMaterial;

/* Metadata is copied into owned HSD runtime objects; image/palette bytes stay
 * owned by the archive and must outlive the material. Importer validates full
 * typed payload spans. min_filter is the original LOD value before HSD's
 * CI/non-mipmap adjustment. Custom classes, animation, TEV and PE are omitted.
 */
MeleeWebHsdMaterial* melee_web_hsd_material_create(const MeleeWebHsdMaterialDesc* desc,
                                                  char* error, size_t error_size);
void melee_web_hsd_material_destroy(MeleeWebHsdMaterial* material);
void melee_web_hsd_material_setup(MeleeWebHsdMaterial* material);
void melee_web_hsd_material_unset(MeleeWebHsdMaterial* material);

#ifdef __cplusplus
}
#endif
#endif
