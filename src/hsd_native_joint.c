#include "hsd_native_joint.h"
#include "gameplay_bootstrap.h"
#include <sysdolphin/baselib/class.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/id.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/mtx.h>
#include <sysdolphin/baselib/list.h>
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/tobj.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct NativeJ { HSD_Joint desc; Mtx inverse; uint32_t source_offset; } NativeJ;
typedef struct NativeP { HSD_PObjDesc desc; HSD_VtxDescList* attributes;
    HSD_EnvelopeDesc** envelopes; uint32_t envelope_count; } NativeP;
typedef struct NativeT { HSD_TObjDesc desc; HSD_ImageDesc image;
    HSD_TlutDesc palette; HSD_TexLODDesc lod; } NativeT;
typedef struct NativeM { HSD_MObjDesc desc; HSD_Material material;
    NativeT* textures; uint32_t texture_count; } NativeM;
struct MeleeWebNativeJoint {
    NativeJ* joints; HSD_DObjDesc* dobjs; NativeP* pobjs; NativeM* materials;
    uint32_t joint_count, dobj_count, pobj_count, material_count, root;
    HSD_GObj* owner;
    uint64_t generation;
};
static uint64_t native_generation;
HSD_JObj* melee_web_native_common_load(HSD_Joint* descriptor, const uint8_t diffuse[4]);
static int fail(char* error, size_t size, const char* message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}
static void finish_native_world(void)
{
    /* GObj shutdown has already invoked each real object destructor. Original
     * amnesia clears class allocator/hash caches before their arena disappears. */
    _HSD_IDForgetMemory(NULL, NULL);
    hsdForgetClassLibrary(NULL);
    native_generation = 0;
}
static void owner_removed(void* data)
{
    /* Original GObj deletion calls user-data cleanup before object cleanup.
     * Keep descriptor storage alive until the external handle is released. */
    ((MeleeWebNativeJoint*) data)->owner = NULL;
}
static void free_descriptors(MeleeWebNativeJoint* handle)
{
    if (!handle) return;
    if (handle->pobjs) for (uint32_t i = 0; i < handle->pobj_count; ++i) {
        NativeP* p = &handle->pobjs[i];
        free(p->attributes);
        if (p->envelopes) for (uint32_t j = 0; j < p->envelope_count; ++j) free(p->envelopes[j]);
        free(p->envelopes);
    }
    if (handle->materials) for (uint32_t i = 0; i < handle->material_count; ++i)
        free(handle->materials[i].textures);
    free(handle->joints); free(handle->dobjs); free(handle->pobjs); free(handle->materials); free(handle);
}
int melee_web_native_joint_destroy(MeleeWebNativeJoint* handle, char* error, size_t size)
{
    if (handle && handle->owner) {
        if (handle->generation != melee_web_gameplay_stats().generation)
            return fail(error, size, "Native HSD teardown requires its original owned world");
        if (handle->owner == HSD_GObj_804D781C)
            return fail(error, size, "Cannot destroy a native HSD owner inside its current callback");
        HSD_GObjPLink_80390228(handle->owner);
    }
    free_descriptors(handle);
    if (error && size) error[0] = 0;
    return 1;
}
static int valid_index(uint32_t index, uint32_t count) { return index == UINT32_MAX || index < count; }
static int visit_joint(const MeleeWebNativeGraph* g, uint32_t i, uint8_t* seen, uint32_t* visited)
{
    if (i == UINT32_MAX) return 1;
    if (i >= g->joint_count || seen[i]) return 0;
    seen[i] = 1; ++*visited;
    const MeleeWebNativeJointDesc* j = &g->joints[i];
    return visit_joint(g, j->child, seen, visited) && visit_joint(g, j->next, seen, visited);
}
static int validate(const MeleeWebNativeGraph* g, char* error, size_t size)
{
    if (!g || !g->joints || !g->joint_count || g->joint_count > 256 ||
        g->root >= g->joint_count || g->dobj_count > 4096 || g->pobj_count > 4096 ||
        g->material_count > 4096 || (g->dobj_count && !g->dobjs) ||
        (g->pobj_count && !g->pobjs) || (g->material_count && !g->materials))
        return fail(error, size, "Native HSD graph arrays or counts are invalid");
    uint8_t seen[256] = {0}; uint32_t visited = 0;
    if (!visit_joint(g, g->root, seen, &visited) || visited != g->joint_count)
        return fail(error, size, "Native HSD joints must form one complete acyclic unshared graph");
    for (uint32_t i = 0; i < g->joint_count; ++i) {
        const MeleeWebNativeJointDesc* j = &g->joints[i];
        if ((j->flags & (JOBJ_INSTANCE | JOBJ_PTCL | JOBJ_SPLINE)) || !valid_index(j->dobj, g->dobj_count))
            return fail(error, size, "Native HSD joint union or DObj index is unsupported");
        for (unsigned c = 0; c < 3; ++c)
            if (!isfinite(j->rotation[c]) || !isfinite(j->scale[c]) || !isfinite(j->translation[c]))
                return fail(error, size, "Native HSD joint SRT must be finite");
        if (j->has_inverse_bind) for (unsigned r = 0; r < 3; ++r) for (unsigned c = 0; c < 4; ++c)
            if (!isfinite(j->inverse_bind[r][c])) return fail(error, size, "Native inverse bind must be finite");
    }
    for (uint32_t i = 0; i < g->dobj_count; ++i) {
        const MeleeWebNativeDObjDesc* d = &g->dobjs[i];
        if (d->material >= g->material_count || !valid_index(d->next, g->dobj_count) || !valid_index(d->pobj, g->pobj_count))
            return fail(error, size, "Native DObj references an invalid descriptor");
        uint32_t cursor = i;
        for (unsigned depth = 0; cursor != UINT32_MAX; ++depth) {
            if (depth >= 256 || cursor >= g->dobj_count)
                return fail(error, size, "Native DObj chain is cyclic or exceeds recursion budget");
            cursor = g->dobjs[cursor].next;
        }
    }
    for (uint32_t i = 0; i < g->pobj_count; ++i) {
        const MeleeWebNativePObjDesc* p = &g->pobjs[i];
        const MeleeWebPObjView* v = &p->geometry;
        if (!valid_index(p->next, g->pobj_count) || !v->attributes || !v->attribute_count ||
            v->attribute_count > 21 || !v->display || !v->display_byte_size ||
            v->display_byte_size % 32 || v->display_byte_size / 32 > UINT16_MAX ||
            (v->flags & ~0xe001U) || p->envelope_count > 10 ||
            ((v->flags & 0x3000U) == 0x2000U) != (p->envelope_count != 0) ||
            (p->envelope_count && !p->envelopes))
            return fail(error, size, "Native PObj requires checked rigid/envelope geometry");
        uint32_t cursor = i;
        for (unsigned depth = 0; cursor != UINT32_MAX; ++depth) {
            if (depth >= 256 || cursor >= g->pobj_count)
                return fail(error, size, "Native PObj chain is cyclic or exceeds recursion budget");
            cursor = g->pobjs[cursor].next;
        }
        for (uint32_t e = 0; e < p->envelope_count; ++e) {
            const MeleeWebSkinEnvelope* envelope = &p->envelopes[e];
            if (!envelope->influences || !envelope->influence_count || envelope->influence_count > 31)
                return fail(error, size, "Native envelope influence count is invalid");
            float total = 0;
            for (uint32_t k = 0; k < envelope->influence_count; ++k) {
                const MeleeWebSkinInfluence* inf = &envelope->influences[k];
                if (inf->joint >= g->joint_count || !isfinite(inf->weight) || inf->weight < 0 || inf->weight > 1)
                    return fail(error, size, "Native envelope joint or weight is invalid");
                total += inf->weight;
            }
            if (!(total > 0)) return fail(error, size, "Native envelope has zero aggregate weight");
        }
    }
    for (uint32_t i = 0; i < g->material_count; ++i) {
        const MeleeWebNativeMaterialDesc* m = &g->materials[i];
        if (!isfinite(m->material.alpha) || m->material.alpha < 0 || m->material.alpha > 1 ||
            !isfinite(m->material.shininess) || m->material.shininess < 0 ||
            (m->material.rendermode & ~0xfffU) || m->material.texture_count > 8 ||
            (m->material.texture_count && !m->textures))
            return fail(error, size, "Native material requires checked opaque material metadata");
        for (uint32_t j = 0; j < m->material.texture_count; ++j) {
            const MeleeWebHsdTextureDesc* t = &m->textures[j].texture;
            if (!t->image_data || !t->image_bytes || !t->width || !t->height)
                return fail(error, size, "Native texture requires a checked image span");
        }
    }
    return 1;
}

static MeleeWebNativeJoint* create_joint(const MeleeWebNativeGraph* g, const uint8_t* diffuse, char* error, size_t size)
{
    if (!validate(g, error, size)) return NULL;
    MeleeWebGameplayStats world = melee_web_gameplay_stats();
    /* This is an explicit resource gate, not a prediction of allocator success.
     * Fragmentation or a genuine source heap OOM retains original HSD failure. */
    if (!world.generation || world.heap_free_bytes < 1024 * 1024)
        { fail(error, size, "Native HSD loading requires an owned world with at least 1 MiB free"); return NULL; }
    if (native_generation != world.generation && HSD_IDGetAllocData()->used)
        { fail(error, size, "Native HSD cannot replace an existing descriptor ID context"); return NULL; }
    if (diffuse && g->joints[g->root].dobj == UINT32_MAX)
        { fail(error, size, "Original common material consumer requires a root DObj"); return NULL; }
    MeleeWebNativeJoint* h = calloc(1, sizeof(*h));
    if (!h) { fail(error, size, "Unable to allocate native HSD descriptor owner"); return NULL; }
    h->joint_count = g->joint_count; h->dobj_count = g->dobj_count;
    h->pobj_count = g->pobj_count; h->material_count = g->material_count; h->root = g->root;
    h->joints = calloc(g->joint_count, sizeof(NativeJ));
    h->dobjs = calloc(g->dobj_count, sizeof(HSD_DObjDesc));
    h->pobjs = calloc(g->pobj_count, sizeof(NativeP));
    h->materials = calloc(g->material_count, sizeof(NativeM));
    if (!h->joints || (g->dobj_count && !h->dobjs) || (g->pobj_count && !h->pobjs) || (g->material_count && !h->materials)) goto oom;
    for (uint32_t i = 0; i < g->joint_count; ++i) {
        const MeleeWebNativeJointDesc* s = &g->joints[i]; HSD_Joint* d = &h->joints[i].desc;
        h->joints[i].source_offset = s->source_offset;
        d->flags = s->flags;
        d->child = s->child == UINT32_MAX ? NULL : &h->joints[s->child].desc;
        d->next = s->next == UINT32_MAX ? NULL : &h->joints[s->next].desc;
        d->u.dobjdesc = s->dobj == UINT32_MAX ? NULL : &h->dobjs[s->dobj];
        memcpy(&d->rotation, s->rotation, sizeof(Vec3)); memcpy(&d->scale, s->scale, sizeof(Vec3));
        memcpy(&d->position, s->translation, sizeof(Vec3));
        if (s->has_inverse_bind) { memcpy(h->joints[i].inverse, s->inverse_bind, sizeof(Mtx)); d->mtx = h->joints[i].inverse; }
    }
    for (uint32_t i = 0; i < g->dobj_count; ++i) {
        const MeleeWebNativeDObjDesc* s = &g->dobjs[i]; HSD_DObjDesc* d = &h->dobjs[i];
        d->next = s->next == UINT32_MAX ? NULL : &h->dobjs[s->next];
        d->mobjdesc = &h->materials[s->material].desc;
        d->pobjdesc = s->pobj == UINT32_MAX ? NULL : &h->pobjs[s->pobj].desc;
    }
    for (uint32_t i = 0; i < g->pobj_count; ++i) {
        const MeleeWebNativePObjDesc* s = &g->pobjs[i]; NativeP* p = &h->pobjs[i];
        p->desc.next = s->next == UINT32_MAX ? NULL : &h->pobjs[s->next].desc;
        p->desc.flags = s->geometry.flags; p->desc.n_display = s->geometry.display_byte_size / 32;
        p->desc.display = (u8*) s->geometry.display;
        p->attributes = calloc(s->geometry.attribute_count + 1, sizeof(HSD_VtxDescList));
        if (!p->attributes) goto oom;
        p->desc.verts = p->attributes;
        for (uint32_t j = 0; j < s->geometry.attribute_count; ++j) {
            const MeleeWebPObjAttribute* a = &s->geometry.attributes[j];
            p->attributes[j] = (HSD_VtxDescList) {a->attr, a->attr_type, a->comp_cnt, a->comp_type, a->frac, a->stride, (void*) a->data};
        }
        p->attributes[s->geometry.attribute_count].attr = GX_VA_NULL;
        if (s->envelope_count) {
            p->envelope_count = s->envelope_count;
            p->envelopes = calloc(s->envelope_count + 1, sizeof(HSD_EnvelopeDesc*));
            if (!p->envelopes) goto oom;
            p->desc.u.envelope_p = p->envelopes;
            for (uint32_t e = 0; e < s->envelope_count; ++e) {
                const MeleeWebSkinEnvelope* in = &s->envelopes[e];
                p->envelopes[e] = calloc(in->influence_count + 1, sizeof(HSD_EnvelopeDesc));
                if (!p->envelopes[e]) goto oom;
                for (uint32_t j = 0; j < in->influence_count; ++j)
                    p->envelopes[e][j] = (HSD_EnvelopeDesc) {&h->joints[in->influences[j].joint].desc, in->influences[j].weight};
            }
        }
    }
    for (uint32_t i = 0; i < g->material_count; ++i) {
        const MeleeWebNativeMaterialDesc* s = &g->materials[i]; NativeM* m = &h->materials[i];
        m->desc.rendermode = s->material.rendermode; m->desc.mat = &m->material;
        memcpy(&m->material.ambient, s->material.ambient, sizeof(GXColor));
        memcpy(&m->material.diffuse, s->material.diffuse, sizeof(GXColor));
        memcpy(&m->material.specular, s->material.specular, sizeof(GXColor));
        m->material.alpha = s->material.alpha; m->material.shininess = s->material.shininess;
        m->texture_count = s->material.texture_count;
        if (m->texture_count) {
            m->textures = calloc(m->texture_count, sizeof(NativeT)); if (!m->textures) goto oom;
            m->desc.texdesc = &m->textures[0].desc;
        }
        for (uint32_t j = 0; j < m->texture_count; ++j) {
            const MeleeWebNativeTextureDesc* n = &s->textures[j]; const MeleeWebHsdTextureDesc* in = &n->texture;
            NativeT* t = &m->textures[j]; HSD_TObjDesc* d = &t->desc;
            d->next = j + 1 < m->texture_count ? &m->textures[j + 1].desc : NULL;
            d->id = n->source_id; d->src = in->source;
            memcpy(&d->rotate, in->rotation, sizeof(Vec3)); memcpy(&d->scale, in->scale, sizeof(Vec3));
            memcpy(&d->translate, in->translation, sizeof(Vec3));
            d->wrap_s = in->wrap_s; d->wrap_t = in->wrap_t; d->repeat_s = in->repeat_s; d->repeat_t = in->repeat_t;
            d->blend_flags = in->flags; d->blending = in->blending; d->magFilt = in->mag_filter;
            t->image = (HSD_ImageDesc) {(void*) in->image_data, in->width, in->height, in->format, in->mipmap, in->min_lod, in->max_lod};
            d->imagedesc = &t->image;
            if (in->palette_data) {
                t->palette = (HSD_TlutDesc) {(void*) in->palette_data, in->palette_format, n->palette_name, in->palette_entries};
                d->tlutdesc = &t->palette;
            }
            if (n->has_lod) {
                t->lod = (HSD_TexLODDesc) {in->min_filter, in->lod_bias, in->bias_clamp, in->edge_lod, in->anisotropy};
                d->lod = &t->lod;
            }
        }
    }
    if (!melee_web_gameplay_enable_hsd_objects(finish_native_world, error, size)) { free_descriptors(h); return NULL; }
    if (native_generation != world.generation) {
        HSD_IDInitAllocData(); HSD_IDSetup();
        HSD_ListInitAllocData(); HSD_MtxInitAllocData(); HSD_VecInitAllocData();
        native_generation = world.generation;
    }
    HSD_JObj* loaded = diffuse ? melee_web_native_common_load(&h->joints[g->root].desc, diffuse) :
                                HSD_JObjLoadJoint(&h->joints[g->root].desc);
    if (!loaded) { fail(error, size, "Original HSD_JObjLoadJoint returned no object"); free_descriptors(h); return NULL; }
    h->owner = GObj_Create(HSD_GOBJ_CLASS_UI, 0, 0);
    if (!h->owner) { HSD_JObjRemoveAll(loaded); fail(error, size, "Original GObj allocation failed"); free_descriptors(h); return NULL; }
    h->generation = world.generation;
    HSD_GObjObject_80390A70(h->owner, HSD_GObj_JObjKind, loaded);
    GObj_InitUserData(h->owner, 0, owner_removed, h);
    if (error && size) error[0] = 0;
    return h;
oom:
    fail(error, size, "Unable to allocate native HSD descriptor storage"); free_descriptors(h); return NULL;
}

MeleeWebNativeJoint* melee_web_native_joint_create(const MeleeWebNativeGraph* g, char* error, size_t size)
{
    return create_joint(g, NULL, error, size);
}
MeleeWebNativeJoint* melee_web_native_common_joint_create(const MeleeWebNativeGraph* g,
    const uint8_t diffuse[4], char* error, size_t size)
{
    if (!diffuse) { fail(error, size, "Common material consumer requires root0.x7D8 color"); return NULL; }
    return create_joint(g, diffuse, error, size);
}

int melee_web_native_joint_stats(const MeleeWebNativeJoint* h, MeleeWebNativeJointStats* out, char* error, size_t size)
{
    if (!h || !out || !h->owner || h->generation != melee_web_gameplay_stats().generation)
        return fail(error, size, "Native HSD handle is no longer in a live owned world");
    MeleeWebNativeJointStats result = {0}; result.generation = h->generation;
    for (uint32_t i = 0; i < h->joint_count; ++i) {
        HSD_JObj* j = HSD_IDGetData((u32) &h->joints[i].desc, NULL);
        if (!j || j->id != (u32) &h->joints[i].desc)
            return fail(error, size, "Original HSD joint descriptor identity is missing");
        ++result.joints;
        for (HSD_DObj* d = j->u.dobj; d; d = d->next) {
            ++result.dobjs;
            if (d->mobj) {
                if (!result.materials) memcpy(result.first_diffuse, &d->mobj->mat->diffuse, 4);
                ++result.materials;
                for (HSD_TObj* t = d->mobj->tobj; t; t = t->next) ++result.textures;
            }
            for (HSD_PObj* p = d->pobj; p; p = p->next) {
                ++result.pobjs;
                if ((p->flags & 0x3000U) == 0x2000U) {
                    for (HSD_SList* list = p->u.envelope_list; list; list = list->next)
                        for (HSD_Envelope* e = list->data; e; e = e->next) {
                            if (!e->jobj) return fail(error, size, "Original HSD envelope reference was not resolved");
                            ++result.resolved_envelopes;
                        }
                }
            }
        }
    }
    *out = result;
    if (error && size) error[0] = 0;
    return 1;
}
