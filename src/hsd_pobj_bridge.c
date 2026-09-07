#include "hsd_probe_compat.h"
#include "hsd_pobj_bridge.h"

#include <dolphin/gx.h>
#include <sysdolphin/baselib/forward.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_checked_array(GXAttr attr, const void* data, u16 stride);
static HSD_JObj* skin_current_joint(void);
static void reject_dynamic_skin_joint(HSD_JObj* joint);
static void capture_position_matrix(const void* matrix, u32 id);
static void capture_normal_matrix(const void* matrix, u32 id);
static void capture_texture_matrix(const void* matrix, u32 id, GXTexMtxType type);

/* Compile the pinned original geometry and envelope implementations. The
 * scoped SDK adapters bound vertex arrays and capture matrix loads before GX
 * submission. A host-owned current joint supplies already-evaluated original
 * SRT matrices; requests for dynamic HSD scene callbacks fail explicitly.
 * Unused loaders are removed by the linker's section garbage collection.
 * This file must not be linked alongside a second compilation of pobj.c.
 */
#define GXSetArray(attr, data, stride) set_checked_array((attr), (data), (stride))
#define HSD_JObjGetCurrent skin_current_joint
#define HSD_JObjSetupMatrixSub reject_dynamic_skin_joint
#define GXLoadPosMtxImm capture_position_matrix
#define GXLoadNrmMtxImm capture_normal_matrix
#define GXLoadTexMtxImm capture_texture_matrix
#include <sysdolphin/baselib/pobj.c>
#undef GXSetArray
#undef HSD_JObjGetCurrent
#undef HSD_JObjSetupMatrixSub
#undef GXLoadPosMtxImm
#undef GXLoadNrmMtxImm
#undef GXLoadTexMtxImm

static const MeleeWebPObjView* active_view;
static HSD_JObj* active_skin_joint;
static MeleeWebPObjPalette* active_palette;
static uint32_t captured_position_mask;
static int invalid_capture;

typedef struct OwnedSkinJoint {
    HSD_JObj object;
    Mtx inverse_bind;
} OwnedSkinJoint;
struct MeleeWebSkinSkeleton {
    OwnedSkinJoint* joints;
    uint32_t count;
};

static HSD_JObj* skin_current_joint(void)
{
    HSD_ASSERT(__LINE__, active_skin_joint != NULL);
    return active_skin_joint;
}

static void reject_dynamic_skin_joint(HSD_JObj* joint)
{
    (void) joint;
    HSD_Panic(__FILE__, __LINE__, "Skin joints must be updated through the original SRT bridge before palette setup");
}

static void capture_matrix(const void* matrix, u32 slot, float output[10][3][4],
                           uint32_t* mask)
{
    if (!active_palette || !matrix || slot >= active_palette->count) {
        invalid_capture = 1;
        return;
    }
    const float (*input)[4] = matrix;
    for (uint32_t row = 0; row < 3; ++row) {
        for (uint32_t column = 0; column < 4; ++column) {
            if (!isfinite(input[row][column])) {
                invalid_capture = 1;
                return;
            }
        }
    }
    memcpy(output[slot], matrix, sizeof(Mtx));
    *mask |= 1U << slot;
}

static void capture_position_matrix(const void* matrix, u32 id)
{
    if (!active_palette || id % 3) { invalid_capture = 1; return; }
    capture_matrix(matrix, id / 3, active_palette->position, &captured_position_mask);
}
static void capture_normal_matrix(const void* matrix, u32 id)
{
    if (!active_palette || id % 3) { invalid_capture = 1; return; }
    capture_matrix(matrix, id / 3, active_palette->normal, &active_palette->normal_mask);
}
static void capture_texture_matrix(const void* matrix, u32 id, GXTexMtxType type)
{
    if (!active_palette || id < GX_TEXMTX0 || (id - GX_TEXMTX0) % 3 || type != GX_MTX3x4) {
        invalid_capture = 1;
        return;
    }
    capture_matrix(matrix, (id - GX_TEXMTX0) / 3, active_palette->texture,
                   &active_palette->texture_mask);
}

static int reject(char* error, size_t error_size, const char* reason)
{
    if (error != NULL && error_size != 0) {
        snprintf(error, error_size, "%s", reason);
    }
    return 0;
}

static void set_checked_array(GXAttr attr, const void* data, u16 stride)
{
    if (active_view != NULL) {
        for (uint32_t i = 0; i < active_view->attribute_count; ++i) {
            const MeleeWebPObjAttribute* a = &active_view->attributes[i];
            if (a->attr == (uint32_t) attr && a->data == data &&
                a->stride == stride && a->attr_type != GX_DIRECT) {
                GXSetArray(attr, data, a->byte_size, (u8) stride, false);
                return;
            }
        }
    }
    /* This indicates an integration error after validation, not a supported
     * missing service. Never guess an array size or silently omit the array. */
    fputs("HSD PObj bridge: unexpected GXSetArray request\n", stderr);
    abort();
}

static int valid_format(const MeleeWebPObjAttribute* a)
{
    if (a->frac > 31) {
        return 0;
    }
    if (a->attr == GX_VA_CLR0 || a->attr == GX_VA_CLR1) {
        return a->comp_cnt <= GX_CLR_RGBA && a->comp_type <= GX_RGBA8;
    }
    if (a->comp_type > GX_F32) {
        return 0;
    }
    if (a->attr == GX_VA_NRM) {
        /* NBT and NBT3 require a distinct index/component interpretation. */
        return a->comp_cnt == GX_NRM_XYZ &&
               (a->comp_type == GX_S8 || a->comp_type == GX_S16 ||
                a->comp_type == GX_F32);
    }
    return a->comp_cnt <= 1;
}

static uint32_t component_bytes(const MeleeWebPObjAttribute* a)
{
    if (a->attr == GX_VA_CLR0 || a->attr == GX_VA_CLR1) {
        static const uint8_t color_bytes[] = {2, 3, 4, 2, 3, 4};
        return color_bytes[a->comp_type];
    }
    const uint32_t scalar = a->comp_type <= GX_S8 ? 1 : a->comp_type <= GX_S16 ? 2 : 4;
    const uint32_t count = a->attr == GX_VA_POS ? a->comp_cnt + 2 :
                           a->attr == GX_VA_NRM ? 3 : a->comp_cnt + 1;
    return scalar * count;
}

static int draw_pobj(const MeleeWebPObjView* view, const MeleeWebPObjPalette* palette,
                     char* error, size_t error_size)
{
    if (error != NULL && error_size != 0) {
        error[0] = '\0';
    }
    if (active_view != NULL) {
        return reject(error, error_size, "HSD PObj drawing is not reentrant");
    }
    if (view == NULL || view->attributes == NULL ||
        view->attribute_count == 0 ||
        view->attribute_count > MELEE_WEB_POBJ_MAX_ATTRIBUTES) {
        return reject(error, error_size, "Invalid PObj attribute count");
    }
    if ((view->flags & ~(POBJ_CULLFRONT | POBJ_CULLBACK | 1U | (palette ? POBJ_ENVELOPE : 0))) != 0 ||
        (palette && (view->flags & 0x3000) != POBJ_ENVELOPE)) {
        return reject(error, error_size, "Only rigid or envelope PObj geometry with supported flags is accepted");
    }
    if (view->display == NULL || view->display_byte_size == 0 ||
        (view->display_byte_size & 31) != 0 ||
        view->display_byte_size / 32 > UINT16_MAX) {
        return reject(error, error_size, "Invalid PObj display-list size");
    }
    HSD_VtxDescList descriptors[MELEE_WEB_POBJ_MAX_ATTRIBUTES + 1] = {0};
    int has_position = 0, has_matrix_index = 0;
    for (uint32_t i = 0; i < view->attribute_count; ++i) {
        const MeleeWebPObjAttribute* a = &view->attributes[i];
        const int matrix_index = a->attr <= GX_VA_TEX7MTXIDX;
        if ((!palette && a->attr < GX_VA_POS) || a->attr > GX_VA_TEX7 ||
            (i != 0 && a->attr <= view->attributes[i - 1].attr)) {
            return reject(error, error_size, "Unsupported or unordered PObj attribute");
        }
        if (a->attr_type < GX_DIRECT || a->attr_type > GX_INDEX16 ||
            (!matrix_index && !valid_format(a)) ||
            (matrix_index && (a->attr_type != GX_DIRECT || a->data || a->byte_size || a->stride))) {
            return reject(error, error_size, "Unsupported PObj attribute format");
        }
        if (a->stride > UINT8_MAX ||
            (a->attr_type != GX_DIRECT &&
             (a->data == NULL || a->stride == 0 || a->byte_size < component_bytes(a)))) {
            return reject(error, error_size, "Invalid PObj indexed array span or stride");
        }
        descriptors[i] = (HSD_VtxDescList){
            .attr = (GXAttr) a->attr,
            .attr_type = (GXAttrType) a->attr_type,
            .comp_cnt = (GXCompCnt) a->comp_cnt,
            .comp_type = (GXCompType) a->comp_type,
            .frac = a->frac,
            .stride = a->stride,
            .vertex = (void*) a->data,
        };
        has_position |= a->attr == GX_VA_POS;
        has_matrix_index |= a->attr == GX_VA_PNMTXIDX;
    }
    if (!has_position || (palette && !has_matrix_index))
        return reject(error, error_size, "PObj requires position and envelope matrix-index attributes");
    descriptors[view->attribute_count].attr = GX_VA_NULL;

    HSD_PObj pobj = {
        .verts = descriptors,
        .flags = view->flags,
        .n_display = (u16) (view->display_byte_size / 32),
        .display = (u8*) view->display,
    };
    /* HSD_PObjDisp normally performs this cull selection before its class
     * matrix callback. This narrow bridge uses caller-supplied matrices and
     * reaches the original simple-primitive function directly. */
    if ((view->flags & (POBJ_CULLFRONT | POBJ_CULLBACK)) == (POBJ_CULLFRONT | POBJ_CULLBACK)) {
        return 1;
    }
    _HSD_StateInvalidatePrimitive();
    HSD_StateSetCullMode((view->flags & POBJ_CULLFRONT) ? GX_CULL_FRONT :
                        (view->flags & POBJ_CULLBACK) ? GX_CULL_BACK : GX_CULL_NONE);
    if (palette) {
        GXSetCurrentMtx(GX_PNMTX0);
        for (uint32_t slot = 0; slot < palette->count; ++slot) {
            GXLoadPosMtxImm(palette->position[slot], slot * 3);
            if (palette->normal_mask & (1U << slot)) GXLoadNrmMtxImm(palette->normal[slot], slot * 3);
            if (palette->texture_mask & (1U << slot))
                GXLoadTexMtxImm(palette->texture[slot], GX_TEXMTX0 + slot * 3, GX_MTX3x4);
        }
    }

    /* Stack addresses can repeat on the next mesh. Invalidate on both sides
     * so original HSD caches never mistake recycled descriptors for old data. */
    HSD_ClearVtxDesc();
    active_view = view;
    PObjDispSimplePrimitive(&pobj, 0);
    active_view = NULL;
    HSD_ClearVtxDesc();
    return 1;
}

int melee_web_pobj_draw(const MeleeWebPObjView* view, char* error, size_t error_size)
{
    return draw_pobj(view, NULL, error, error_size);
}

int melee_web_pobj_draw_palette(const MeleeWebPObjView* view,
    const MeleeWebPObjPalette* palette, char* error, size_t error_size)
{
    if (!palette || !palette->count || palette->count > MELEE_WEB_POBJ_MAX_PALETTE ||
        ((palette->normal_mask | palette->texture_mask) >> palette->count))
        return reject(error, error_size, "Invalid captured matrix palette");
    for (uint32_t slot = 0; slot < palette->count; ++slot) {
        for (uint32_t row = 0; row < 3; ++row) for (uint32_t col = 0; col < 4; ++col) {
            if (!isfinite(palette->position[slot][row][col]) ||
                ((palette->normal_mask & (1U << slot)) && !isfinite(palette->normal[slot][row][col])) ||
                ((palette->texture_mask & (1U << slot)) && !isfinite(palette->texture[slot][row][col])))
                return reject(error, error_size, "Captured matrix palette is nonfinite");
        }
    }
    return draw_pobj(view, palette, error, error_size);
}

int melee_web_skin_update(MeleeWebSkinSkeleton* skeleton,
    const MeleeWebSkinJoint* joints, uint32_t count, char* error, size_t error_size)
{
    if (error && error_size) error[0] = 0;
    if (!skeleton || !joints || !count || count > MELEE_WEB_SKIN_MAX_JOINTS)
        return reject(error, error_size, "Invalid skin skeleton or joint budget");
    const uint32_t supported = JOBJ_SKELETON | JOBJ_SKELETON_ROOT | JOBJ_ENVELOPE_MODEL |
        JOBJ_CLASSICAL_SCALE | JOBJ_HIDDEN | JOBJ_MTX_DIRTY | JOBJ_LIGHTING | JOBJ_TEXGEN |
        JOBJ_SPECULAR | JOBJ_OPA | JOBJ_XLU | JOBJ_TEXEDGE | JOBJ_ROOT_MASK;
    for (uint32_t i = 0; i < count; ++i) {
        if ((joints[i].parent != UINT32_MAX && joints[i].parent >= i) ||
            (joints[i].flags & ~supported) || joints[i].has_inverse_bind > 1)
            return reject(error, error_size, "Unsupported skin joint flags, parent order or inverse bind");
        for (uint32_t row = 0; row < 3; ++row) for (uint32_t col = 0; col < 4; ++col) {
            if (!isfinite(joints[i].world[row][col]) ||
                (joints[i].has_inverse_bind && !isfinite(joints[i].inverse_bind[row][col])))
                return reject(error, error_size, "Skin joint matrices must be finite");
        }
    }
    /* Validation is complete, so refreshing an unchanged topology cannot
     * fail and does not allocate in the animation frame loop. */
    const int reuse = skeleton->joints != NULL && skeleton->count == count;
    OwnedSkinJoint* replacement = reuse ? skeleton->joints : calloc(count, sizeof(*replacement));
    if (!replacement) return reject(error, error_size, "Skin joint allocation failed");
    if (reuse) memset(replacement, 0, count * sizeof(*replacement));
    for (uint32_t i = 0; i < count; ++i) {
        replacement[i].object.flags = joints[i].flags & ~JOBJ_MTX_DIRTY;
        if (joints[i].parent != UINT32_MAX)
            replacement[i].object.parent = &replacement[joints[i].parent].object;
        memcpy(replacement[i].object.mtx, joints[i].world, sizeof(Mtx));
        if (joints[i].has_inverse_bind) {
            memcpy(replacement[i].inverse_bind, joints[i].inverse_bind, sizeof(Mtx));
            replacement[i].object.envelopemtx = replacement[i].inverse_bind;
        }
    }
    if (!reuse) free(skeleton->joints);
    skeleton->joints = replacement;
    skeleton->count = count;
    return 1;
}

MeleeWebSkinSkeleton* melee_web_skin_create(const MeleeWebSkinJoint* joints,
    uint32_t count, char* error, size_t error_size)
{
    MeleeWebSkinSkeleton* skeleton = calloc(1, sizeof(*skeleton));
    if (!skeleton) { reject(error, error_size, "Skin skeleton allocation failed"); return NULL; }
    if (!melee_web_skin_update(skeleton, joints, count, error, error_size)) {
        free(skeleton);
        return NULL;
    }
    return skeleton;
}

void melee_web_skin_destroy(MeleeWebSkinSkeleton* skeleton)
{
    if (skeleton) { free(skeleton->joints); free(skeleton); }
}

int melee_web_pobj_prepare_palette(const MeleeWebSkinSkeleton* skeleton,
    uint32_t mesh_joint, const MeleeWebSkinEnvelope* envelopes, uint32_t count,
    const float camera[3][4], uint32_t rendermode,
    MeleeWebPObjPalette* output, char* error, size_t error_size)
{
    if (error && error_size) error[0] = 0;
    if (active_skin_joint || active_palette)
        return reject(error, error_size, "Skin palette preparation is not reentrant");
    if (!skeleton || mesh_joint >= skeleton->count || !envelopes || !count ||
        count > MELEE_WEB_POBJ_MAX_PALETTE || !camera || !output)
        return reject(error, error_size, "Invalid skin palette request");
    for (uint32_t row = 0; row < 3; ++row) for (uint32_t col = 0; col < 4; ++col)
        if (!isfinite(camera[row][col])) return reject(error, error_size, "Skin camera must be finite");
    HSD_JObj* mesh = &skeleton->joints[mesh_joint].object;
    HSD_JObj* ancestor = mesh;
    while (ancestor && !(ancestor->flags & (JOBJ_SKELETON | JOBJ_SKELETON_ROOT))) ancestor = ancestor->parent;
    if (!ancestor || (!(ancestor->flags & JOBJ_SKELETON_ROOT) && !ancestor->envelopemtx))
        return reject(error, error_size, "Envelope model requires a skeleton ancestor and its inverse bind");
    if (ancestor == mesh && !(mesh->flags & JOBJ_SKELETON_ROOT)) {
        Mtx inverse;
        if (!PSMTXInverse(mesh->envelopemtx, inverse))
            return reject(error, error_size, "Envelope model inverse bind is singular");
        for (uint32_t row = 0; row < 3; ++row) for (uint32_t col = 0; col < 4; ++col)
            if (!isfinite(inverse[row][col]))
                return reject(error, error_size, "Envelope model inverse bind cannot be inverted finitely");
    }
    const int needs_right = !(mesh->flags & JOBJ_SKELETON_ROOT);
    HSD_Envelope influences[MELEE_WEB_POBJ_MAX_PALETTE][MELEE_WEB_POBJ_MAX_INFLUENCES] = {0};
    HSD_SList list[MELEE_WEB_POBJ_MAX_PALETTE] = {0};
    for (uint32_t i = 0; i < count; ++i) {
        const MeleeWebSkinEnvelope* envelope = &envelopes[i];
        if (!envelope->influences || !envelope->influence_count ||
            envelope->influence_count > MELEE_WEB_POBJ_MAX_INFLUENCES)
            return reject(error, error_size, "Invalid envelope influence count");
        const int needs_blend = envelope->influences[0].weight < 1.0f - FLT_EPSILON;
        float total = 0;
        for (uint32_t k = 0; k < envelope->influence_count; ++k) {
            const MeleeWebSkinInfluence* in = &envelope->influences[k];
            if (in->joint >= skeleton->count || !isfinite(in->weight) || in->weight < 0 || in->weight > 1)
                return reject(error, error_size, "Invalid envelope joint or weight");
            HSD_JObj* joint = &skeleton->joints[in->joint].object;
            if ((needs_right || needs_blend) && !joint->envelopemtx)
                return reject(error, error_size, "Envelope influence requires an inverse bind matrix");
            influences[i][k].jobj = joint;
            influences[i][k].weight = in->weight;
            if (k + 1 < envelope->influence_count) influences[i][k].next = &influences[i][k + 1];
            total += in->weight;
        }
        if (total <= 0)
            return reject(error, error_size, "Envelope weights must have a nonzero total");
        list[i].data = &influences[i][0];
        if (i + 1 < count) list[i].next = &list[i + 1];
    }
    HSD_PObj pobj = {.flags = POBJ_ENVELOPE, .u.envelope_list = list};
    MeleeWebPObjPalette result = {.count = count};
    Mtx view, unused = {0};
    memcpy(view, camera, sizeof(view));
    active_skin_joint = mesh;
    active_palette = &result;
    captured_position_mask = 0;
    invalid_capture = 0;
    SetupEnvelopeModelMtx(&pobj, view, unused, rendermode);
    active_palette = NULL;
    active_skin_joint = NULL;
    if (invalid_capture || captured_position_mask != (1U << count) - 1)
        return reject(error, error_size, "Original skin matrix computation produced invalid or nonfinite output");
    *output = result;
    return 1;
}
