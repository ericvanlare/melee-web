#include "hsd_probe_compat.h"
#include "hsd_material_bridge.h"
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/state.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cached_texture_init(GXTexObj*, const void*, u16, u16, GXTexFmt,
                                GXTexWrapMode, GXTexWrapMode, GXBool);
static void cached_indexed_texture_init(GXTexObj*, const void*, u16, u16, GXCITexFmt,
                                        GXTexWrapMode, GXTexWrapMode, GXBool, u32);
static void cached_palette_init(GXTlutObj*, const void*, GXTlutFmt, u16);

/* Private original texture expression/matrix callbacks are reused verbatim.
 * Aurora assigns texture identities at GXInit time. Preserve immutable base
 * objects across frames while original HSD still performs LOD, binding, TEV
 * and texture-matrix setup on its local object copies.
 * Do not compile a second copy of tobj.c into this target. */
#define GXInitTexObj cached_texture_init
#define GXInitTexObjCI cached_indexed_texture_init
#define GXInitTlutObj cached_palette_init
#include <sysdolphin/baselib/tobj.c>
#undef GXInitTexObj
#undef GXInitTexObjCI
#undef GXInitTlutObj

enum { material_max_textures = 8 };
typedef struct OwnedTexture {
    HSD_TObj object;
    HSD_ImageDesc image;
    HSD_Tlut palette;
    HSD_TexLODDesc lod;
    GXTexObj sdk_texture;
    GXTlutObj sdk_palette;
} OwnedTexture;

struct MeleeWebHsdMaterial {
    HSD_MObj object;
    HSD_Material colors;
    uint32_t texture_count;
    uint32_t initialized_textures;
    OwnedTexture textures[material_max_textures];
};
static MeleeWebHsdMaterial* active_material;

static void release_sdk_textures(MeleeWebHsdMaterial* material)
{
    for (uint32_t i = 0; i < material->initialized_textures; ++i) {
        OwnedTexture* texture = &material->textures[i];
        GXDestroyTexObj(&texture->sdk_texture);
        if (texture->object.tlut != NULL) GXDestroyTlutObj(&texture->sdk_palette);
    }
    material->initialized_textures = 0;
}

static void copy_cached_texture(GXTexObj* output, const void* data, u16 width,
                                u16 height, u32 format, GXTexWrapMode wrap_s,
                                GXTexWrapMode wrap_t, GXBool mipmap)
{
    if (active_material != NULL) {
        for (uint32_t i = 0; i < active_material->texture_count; ++i) {
            const OwnedTexture* t = &active_material->textures[i];
            if (t->image.image_ptr == data && t->image.width == width &&
                t->image.height == height && (u32) t->image.format == format &&
                t->image.mipmap == mipmap && t->object.wrap_s == wrap_s &&
                t->object.wrap_t == wrap_t) {
                memcpy(output, &t->sdk_texture, sizeof(*output));
                return;
            }
        }
    }
    HSD_Panic(__FILE__, __LINE__, "Unexpected HSD texture initialization request");
}

static void cached_texture_init(GXTexObj* output, const void* data, u16 width,
                                u16 height, GXTexFmt format, GXTexWrapMode wrap_s,
                                GXTexWrapMode wrap_t, GXBool mipmap)
{
    copy_cached_texture(output, data, width, height, format, wrap_s, wrap_t, mipmap);
}

static void cached_indexed_texture_init(GXTexObj* output, const void* data, u16 width,
                                        u16 height, GXCITexFmt format, GXTexWrapMode wrap_s,
                                        GXTexWrapMode wrap_t, GXBool mipmap, u32 tlut)
{
    copy_cached_texture(output, data, width, height, format, wrap_s, wrap_t, mipmap);
    GXInitTexObjTlut(output, tlut);
}

static void cached_palette_init(GXTlutObj* output, const void* data,
                                GXTlutFmt format, u16 entries)
{
    if (active_material != NULL) {
        for (uint32_t i = 0; i < active_material->texture_count; ++i) {
            const OwnedTexture* t = &active_material->textures[i];
            if (t->palette.lut == data && t->palette.fmt == format &&
                t->palette.n_entries == entries) {
                memcpy(output, &t->sdk_palette, sizeof(*output));
                return;
            }
        }
    }
    HSD_Panic(__FILE__, __LINE__, "Unexpected HSD palette initialization request");
}

static HSD_MObjInfo material_methods = {
    .setup = HSD_MObjSetup,
    .make_texp = MObjMakeTExp,
    .setup_tev = MObjSetupTev,
    .unset = HSD_MObjUnset,
};
static HSD_TObjInfo texture_methods = {
    .make_mtx = MakeTextureMtx,
    .make_texp = TObjMakeTExp,
};

static MeleeWebHsdMaterial* reject(char* error, size_t error_size, const char* reason)
{
    if (error != NULL && error_size) snprintf(error, error_size, "%s", reason);
    return NULL;
}

MeleeWebHsdMaterial* melee_web_hsd_material_create(const MeleeWebHsdMaterialDesc* desc,
                                                  char* error, size_t error_size)
{
    if (error != NULL && error_size) error[0] = '\0';
    if (desc == NULL || desc->texture_count > material_max_textures ||
        (desc->texture_count && desc->textures == NULL))
        return reject(error, error_size, "Invalid material texture array");
    if ((desc->rendermode & ~(RENDER_CONSTANT | RENDER_DIFFUSE | RENDER_SPECULAR | RENDER_TEXTURES)) ||
        !isfinite(desc->alpha) || desc->alpha != 1.0f ||
        !isfinite(desc->shininess) || desc->shininess < 0 || desc->shininess > 128)
        return reject(error, error_size, "Unsupported material mode, alpha or shininess");
    for (uint32_t i = 0; i < desc->texture_count; ++i) {
        const MeleeWebHsdTextureDesc* t = &desc->textures[i];
        const uint32_t coord = t->flags & TEX_COORD_MASK;
        if ((coord != TEX_COORD_UV && coord != TEX_COORD_REFLECTION) ||
            (t->flags & ~(TEX_MTX_DIRTY | TEX_COORD_MASK | TEX_COLORMAP_MASK |
                          TEX_ALPHAMAP_MASK | TEX_LIGHTMAP_DIFFUSE | TEX_LIGHTMAP_AMBIENT |
                          TEX_LIGHTMAP_SPECULAR | TEX_LIGHTMAP_EXT)) ||
            ((t->flags & TEX_COLORMAP_MASK) >> 16) > 8 ||
            ((t->flags & TEX_ALPHAMAP_MASK) >> 20) > 7 ||
            !isfinite(t->blending) || t->blending < 0 || t->blending > 1 ||
            !t->repeat_s || !t->repeat_t || t->wrap_s > 2 || t->wrap_t > 2 ||
            (coord == TEX_COORD_UV && (t->source < GX_TG_TEX0 || t->source > GX_TG_TEX7)) ||
            (coord == TEX_COORD_REFLECTION && t->source > GX_TG_COLOR1) ||
            t->min_filter > 5 || t->mag_filter > 1 || t->anisotropy > 2 ||
            t->bias_clamp > 1 || t->edge_lod > 1 || t->mipmap > 1 ||
            t->image_data == NULL || !t->image_bytes || !t->width || !t->height ||
            t->width > 1024 || t->height > 1024 ||
            !isfinite(t->min_lod) || !isfinite(t->max_lod) ||
            t->min_lod < 0 || t->max_lod > 10 || t->min_lod > t->max_lod ||
            !isfinite(t->lod_bias) || t->lod_bias < -4 || t->lod_bias > 3.99f)
            return reject(error, error_size, "Unsupported or invalid static HSD texture metadata");
        const bool indexed = t->format == GX_TF_C4 || t->format == GX_TF_C8 || t->format == GX_TF_C14X2;
        if ((!indexed && t->format > GX_TF_RGBA8 && t->format != GX_TF_CMPR) ||
            (indexed && (!t->palette_data || !t->palette_entries || t->palette_format > 2 ||
                         t->palette_bytes < (uint32_t) t->palette_entries * 2)) ||
            (!indexed && t->palette_data))
            return reject(error, error_size, "Unsupported or invalid texture/palette format");
        for (uint32_t k = 0; k < 3; ++k)
            if (!isfinite(t->rotation[k]) || !isfinite(t->scale[k]) || !isfinite(t->translation[k]))
                return reject(error, error_size, "Texture SRT must be finite");
    }
    MeleeWebHsdMaterial* material = calloc(1, sizeof(*material));
    if (material == NULL) return reject(error, error_size, "Material allocation failed");
    HSD_MObj* m = &material->object;
    material->texture_count = desc->texture_count;
    m->parent.class_info = &material_methods.parent;
    m->rendermode = desc->rendermode;
    m->mat = &material->colors;
    memcpy(&m->mat->ambient, desc->ambient, 4);
    memcpy(&m->mat->diffuse, desc->diffuse, 4);
    memcpy(&m->mat->specular, desc->specular, 4);
    m->mat->alpha = desc->alpha;
    m->mat->shininess = desc->shininess;
    for (uint32_t i = 0; i < desc->texture_count; ++i) {
        const MeleeWebHsdTextureDesc* d = &desc->textures[i];
        OwnedTexture* owned = &material->textures[i];
        HSD_TObj* t = &owned->object;
        t->parent.parent.class_info = &texture_methods.parent;
        if (i == 0) m->tobj = t;
        else material->textures[i - 1].object.next = t;
        t->id = (GXTexMapID) i;
        t->src = (GXTexGenSrc) d->source;
        t->flags = d->flags | TEX_MTX_DIRTY;
        t->rotate.x = d->rotation[0]; t->rotate.y = d->rotation[1]; t->rotate.z = d->rotation[2];
        t->scale = (Vec3){d->scale[0], d->scale[1], d->scale[2]};
        t->translate = (Vec3){d->translation[0], d->translation[1], d->translation[2]};
        t->wrap_s = (GXTexWrapMode) d->wrap_s; t->wrap_t = (GXTexWrapMode) d->wrap_t;
        t->repeat_s = d->repeat_s; t->repeat_t = d->repeat_t;
        t->blending = d->blending; t->magFilt = (GXTexFilter) d->mag_filter;
        t->imagedesc = &owned->image;
        owned->image = (HSD_ImageDesc){(void*) d->image_data, d->width, d->height,
            (GXTexFmt) d->format, d->mipmap, d->min_lod, d->max_lod};
        if (d->palette_data) {
            t->tlut = &owned->palette;
            owned->palette = (HSD_Tlut){(void*) d->palette_data,
                (GXTlutFmt) d->palette_format, 0, d->palette_entries};
        }
        t->tlut_no = (u8) -1;
        t->lod = &owned->lod;
        owned->lod = (HSD_TexLODDesc){(GXTexFilter) d->min_filter, d->lod_bias,
            d->bias_clamp, d->edge_lod, (GXAnisotropy) d->anisotropy};
        /* Compute the immutable matrix through the original implementation
         * before acceptance: finite SRT inputs can still overflow its products.
         * Reflection's final post-matrix translation must remain finite too. */
        MakeTextureMtx(t);
        for (uint32_t row = 0; row < 3; ++row) {
            for (uint32_t column = 0; column < 4; ++column) {
                if (!isfinite(t->mtx[row][column])) {
                    release_sdk_textures(material);
                    free(material);
                    return reject(error, error_size, "Texture matrix computation produced a nonfinite value");
                }
            }
            if ((t->flags & TEX_COORD_MASK) == TEX_COORD_REFLECTION &&
                !isfinite(0.5f * t->mtx[row][0] + 0.5f * t->mtx[row][1] +
                          t->mtx[row][2] + t->mtx[row][3])) {
                release_sdk_textures(material);
                free(material);
                return reject(error, error_size, "Reflection texture matrix computation produced a nonfinite value");
            }
        }
        t->flags &= ~TEX_MTX_DIRTY;
        if (d->palette_data) {
            GXInitTlutObj(&owned->sdk_palette, d->palette_data,
                          (GXTlutFmt) d->palette_format, d->palette_entries);
            GXInitTexObjCI(&owned->sdk_texture, d->image_data, d->width, d->height,
                           (GXCITexFmt) d->format, t->wrap_s, t->wrap_t,
                           d->mipmap, GX_TLUT0);
        } else {
            GXInitTexObj(&owned->sdk_texture, d->image_data, d->width, d->height,
                         (GXTexFmt) d->format, t->wrap_s, t->wrap_t, d->mipmap);
        }
        ++material->initialized_textures;
    }
    HSD_MObjCompileTev(m);
    return material;
}

void melee_web_hsd_material_destroy(MeleeWebHsdMaterial* material)
{
    if (material == NULL) return;
    HSD_MObjUnset(&material->object, 0);
    HSD_MObjSetCurrent(NULL);
    HSD_TExpFreeTevDesc(material->object.tevdesc);
    HSD_TExpFreeList(material->object.texp, HSD_TE_ALL, 1);
    release_sdk_textures(material);
    free(material);
}

void melee_web_hsd_material_setup(MeleeWebHsdMaterial* material)
{
    HSD_ASSERT(__LINE__, material);
    HSD_ASSERT(__LINE__, active_material == NULL);
    active_material = material;
    HSD_StateInvalidate(HSD_STATE_ALL);
    HSD_MObjSetCurrent(&material->object);
    HSD_MObjSetup(&material->object, 0);
    active_material = NULL;
}

void melee_web_hsd_material_unset(MeleeWebHsdMaterial* material)
{
    if (material == NULL) return;
    HSD_MObjUnset(&material->object, 0);
    HSD_MObjSetCurrent(NULL);
}
