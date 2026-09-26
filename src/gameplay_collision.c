#include "gameplay_collision.h"
#include "gameplay_bootstrap.h"
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjuserdata.h>

/* Retain the actual loader/query functions and access their private storage
 * solely to implement bounded host ownership. Other mpLib functions are dead
 * stripped; none of their stage/physics services are replaced by success stubs. */
#include <melee/mp/mplib.c>

_Static_assert(sizeof(MapLine) == 16 && sizeof(MapJoint) == 40 && sizeof(MapCollData) == 48,
               "Original collision descriptor ABI");
/* mpIsland_8005A728 allocates only the 0x2c prefix; the source struct's final
 * ptr member belongs to other island paths and is never accessed here. */
_Static_assert(offsetof(mp_UnkStruct0, ptr) == 0x2c, "Original island allocation prefix");
_Static_assert(offsetof(GroundParam, y) == 0 && sizeof(((GroundParam*) 0)->y) == 4,
               "Original source stage scale field");

struct MeleeWebCollision {
    HSD_GObj* object;
    MapCollData map;
    uint64_t generation;
};
static MeleeWebCollision* collision_owner;

static int collision_fail(char* error, size_t size, const char* message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}
static int collision_success(char* error, size_t size)
{
    if (error && size) error[0] = '\0';
    return 1;
}
static int collision_live(MeleeWebCollision* owner, char* error, size_t size)
{
    if (!owner || owner != collision_owner || !owner->object ||
        owner->generation != melee_web_gameplay_generation())
        return collision_fail(error, size, "Collision storage has no live owned gameplay world");
    return 1;
}
static int collision_range(MeleeWebCollisionRange range, size_t limit)
{
    return range.start >= 0 && range.count >= 0 && (size_t) range.start + range.count <= limit;
}
static int collision_adjacency(int index, size_t count)
{
    return index == -1 || (index >= 0 && (size_t) index < count);
}
static int collision_input(const MeleeWebCollisionInput* in, char* error, size_t size)
{
    if (!in || !in->vertices || !in->lines || !in->joints ||
        !in->vertex_count || in->vertex_count > groundCollVtx_count ||
        !in->line_count || in->line_count > groundCollLine_count ||
        !in->joint_count || in->joint_count > groundCollJoint_count)
        return collision_fail(error, size, "Collision arrays exceed original storage capacities or are missing");
    if (in->stage_kind < 0 || in->stage_kind >= Gr_Kind_Count ||
        !isfinite(in->stage_scale) || in->stage_scale <= 0.0F)
        return collision_fail(error, size, "Collision requires a source stage kind and positive finite stage scale");
    if (in->ranges[4].count)
        return collision_fail(error, size, "Dynamic collision lines require pending stage bindings and callbacks");
    unsigned char categories[1536] = {0};
    for (unsigned category = 0; category < 5; ++category) {
        MeleeWebCollisionRange range = in->ranges[category];
        if (!collision_range(range, in->line_count))
            return collision_fail(error, size, "Collision category range is invalid");
        for (int i = range.start; i < range.start + range.count; ++i) {
            if (categories[i] || (in->lines[i].hi_flags & LINE_FLAG_KIND) != (1U << category))
                return collision_fail(error, size, "Collision category partition or original line kind is inconsistent");
            categories[i] = 1;
        }
    }
    for (size_t i = 0; i < in->vertex_count; ++i)
        if (!isfinite(in->vertices[i].x) || !isfinite(in->vertices[i].y) ||
            !isfinite(in->vertices[i].x * in->stage_scale) || !isfinite(in->vertices[i].y * in->stage_scale))
            return collision_fail(error, size, "Collision stage scaling produces nonfinite coordinates");
    for (size_t i = 0; i < in->joint_count; ++i) {
        const MeleeWebCollisionJoint* joint = &in->joints[i];
        if (!collision_range(joint->vertices, in->vertex_count) ||
            !isfinite(joint->left * in->stage_scale) || !isfinite(joint->right * in->stage_scale) ||
            !isfinite(joint->bottom * in->stage_scale) || !isfinite(joint->top * in->stage_scale) ||
            joint->left > joint->right || joint->bottom > joint->top)
            return collision_fail(error, size, "Collision joint bounds or vertex range is invalid");
        for (unsigned category = 0; category < 5; ++category) {
            MeleeWebCollisionRange local = joint->ranges[category], global = in->ranges[category];
            if (!collision_range(local, in->line_count) ||
                (local.count && (local.start < global.start || local.start + local.count > global.start + global.count)))
                return collision_fail(error, size, "Collision joint category range is invalid");
        }
    }
    for (size_t i = 0; i < in->line_count; ++i) {
        const MeleeWebCollisionLine* line = &in->lines[i];
        if (!categories[i] || line->v0 >= in->vertex_count || line->v1 >= in->vertex_count ||
            !collision_adjacency(line->prev0, in->line_count) || !collision_adjacency(line->next0, in->line_count) ||
            !collision_adjacency(line->prev1, in->line_count) || !collision_adjacency(line->next1, in->line_count))
            return collision_fail(error, size, "Collision line has an invalid vertex, adjacency or category");
        int joint_found = 0;
        for (size_t j = 0; j < in->joint_count; ++j) {
            MeleeWebCollisionRange range = in->joints[j].vertices;
            if (line->v0 >= range.start && line->v0 < range.start + range.count) joint_found = 1;
        }
        if (!joint_found) return collision_fail(error, size, "Collision line has no original vertex-owning joint");
        const float dx = in->vertices[line->v1].x * in->stage_scale - in->vertices[line->v0].x * in->stage_scale;
        const float dy = in->vertices[line->v1].y * in->stage_scale - in->vertices[line->v0].y * in->stage_scale;
        const float magnitude = dx * dx + dy * dy;
        if (!isfinite(magnitude) || ((dx != 0 || dy != 0) && magnitude < FLT_MIN))
            return collision_fail(error, size, "Collision line exceeds the original normal arithmetic range");
        if ((line->hi_flags & LINE_FLAG_KIND) == CollLine_Floor && (dx != 0 || dy != 0) && dx <= 0)
            return collision_fail(error, size, "Static floor queries require source left-to-right nonvertical lines");
    }
    /* mpIsland walks raw next0 floor chains and prev0 ceiling chains. Reject
     * cycles before invoking it, including loops entered after the first line. */
    for (size_t start = 0; start < in->line_count; ++start) {
        unsigned kind = in->lines[start].hi_flags & LINE_FLAG_KIND;
        if (kind != CollLine_Floor && kind != CollLine_Ceiling) continue;
        int current = (int) start;
        size_t steps = 0;
        while (current != -1 && (in->lines[current].hi_flags & LINE_FLAG_KIND) == kind) {
            if (++steps > in->line_count)
                return collision_fail(error, size, "Cyclic static island chains are unsupported");
            current = kind == CollLine_Floor ? in->lines[current].next0 : in->lines[current].prev0;
        }
    }
    return 1;
}

static void collision_release_storage(void)
{
    mp_UnkStruct0* lists[] = {mpIsland_80458E88.next, mpIsland_80458E88.x4};
    for (unsigned i = 0; i < 2; ++i)
        while (lists[i]) { mp_UnkStruct0* next = lists[i]->next; HSD_Free(lists[i]); lists[i] = next; }
    mpIsland_8005A6F8();
    HSD_Free(groundCollVtx); HSD_Free(groundCollLine); HSD_Free(groundCollJoint);
    groundCollVtx = NULL; groundCollLine = NULL; groundCollJoint = NULL;
    jointListStart = jointListEnd = NULL; mpLib_804D64B4 = NULL; didCheckBounding = false;
    grDynamicAttr_801CA0B4();
}

static void collision_release(void* data)
{
    MeleeWebCollision* owner = data;
    if (collision_owner != owner) { fputs("Collision global ownership changed during destruction\n", stderr); abort(); }
    collision_release_storage();
    free(owner->map.verts); free(owner->map.lines); free(owner->map.joints);
    owner->map = (MapCollData) {0}; owner->object = NULL; collision_owner = NULL;
}

int melee_web_collision_source_available(void)
{
    return !collision_owner && !mpLib_804D64B4 && !groundCollVtx &&
        !groundCollLine && !groundCollJoint && !mpIsland_80458E88.next &&
        !mpIsland_80458E88.x4 && !HSD_GObj_804D781C;
}

int melee_web_collision_retire_unadopted(const void* source_map,
                                         char* error, size_t size)
{
    if (!source_map || !melee_web_gameplay_generation() ||
        HSD_GObj_804D781C || HSD_GObj_804D7814)
        return collision_fail(error, size, "Source collision rollback requires an idle live stage owner");
    if (melee_web_collision_source_available()) return collision_success(error, size);
    if (mpLib_804D64B4 != source_map || stage_info.coll_data != source_map ||
        !groundCollVtx || !groundCollLine || !groundCollJoint)
        return collision_fail(error, size, "Source collision rollback lost its loaded map owner");
    /* Successful adoption transfers retirement to the collision owner. */
    if (collision_owner) return collision_live(collision_owner, error, size);
    HSD_GObj* updater = NULL;
    for (HSD_GObj* object = ((HSD_GObj**) HSD_GObj_Entities)[6];
         object; object = object->next) {
        if (!object->proc || object->proc->on_invoke != mpLib_800587FC) continue;
        if (updater || object->classifier != 1 || object->user_data ||
            object->proc->child || object->proc->s_link != 4)
            return collision_fail(error, size, "Source collision rollback found a foreign updater");
        updater = object;
    }
    if (updater) HSD_GObjPLink_80390228(updater);
    collision_release_storage();
    return collision_success(error, size);
}

MeleeWebCollision* melee_web_collision_adopt_dummy(char* error,size_t size)
{
    const uint64_t generation=melee_web_gameplay_generation();
    HSD_GObj* object=NULL;
    if(!generation||collision_owner||mpLib_804D64B4!=&mpLib_803BF760||
       !groundCollVtx||!groundCollLine||!groundCollJoint||
       HSD_GObj_804D781C||stage_info.grkind!=Gr_Kind_Unk00){
        collision_fail(error,size,"Source dummy collision requires the initialized dummy stage");return NULL;
    }
    for(HSD_GObj* candidate=((HSD_GObj**)HSD_GObj_Entities)[6];candidate;candidate=candidate->next){
        if(candidate->classifier!=1)continue;
        if(object||candidate->user_data||!candidate->proc||
           candidate->proc->child||candidate->proc->s_link!=4||
           candidate->proc->on_invoke!=mpLib_800587FC){
            collision_fail(error,size,"Source dummy collision process ownership is ambiguous");return NULL;
        }
        object=candidate;
    }
    if(!object){collision_fail(error,size,"Source dummy collision process is missing");return NULL;}
    MeleeWebCollision* owner=calloc(1,sizeof(*owner));
    if(!owner){collision_fail(error,size,"Cannot own source dummy collision storage");return NULL;}
    owner->generation=generation;owner->object=object;collision_owner=owner;
    GObj_InitUserData(object,0,collision_release,owner);
    collision_success(error,size);return owner;
}

#define COPY_RANGES(to, from) do { \
    (to).floor_start = (from)[0].start; (to).floor_count = (from)[0].count; \
    (to).ceiling_start = (from)[1].start; (to).ceiling_count = (from)[1].count; \
    (to).right_wall_start = (from)[2].start; (to).right_wall_count = (from)[2].count; \
    (to).left_wall_start = (from)[3].start; (to).left_wall_count = (from)[3].count; \
    (to).dynamic_start = (from)[4].start; (to).dynamic_count = (from)[4].count; \
} while (0)

static void collision_map_clear(MeleeWebCollision* owner)
{
    if (!owner) return;
    free(owner->map.verts); free(owner->map.lines); free(owner->map.joints);
    owner->map = (MapCollData) {0};
}

static int collision_map_copy(MeleeWebCollision* owner,
                              const MeleeWebCollisionInput* in,
                              char* error, size_t size)
{
    owner->map.verts = malloc(in->vertex_count * sizeof(Vec2));
    owner->map.lines = malloc(in->line_count * sizeof(MapLine));
    owner->map.joints = malloc(in->joint_count * sizeof(MapJoint));
    if (!owner->map.verts || !owner->map.lines || !owner->map.joints) {
        collision_map_clear(owner);
        return collision_fail(error, size, "Unable to allocate owned collision descriptors");
    }
    owner->map.vert_count = (int) in->vertex_count;
    owner->map.line_count = (int) in->line_count;
    owner->map.joint_count = (int) in->joint_count;
    memcpy(&owner->map.x2C, &in->source_reserved_2c, sizeof(owner->map.x2C));
    COPY_RANGES(owner->map, in->ranges);
    for (size_t i = 0; i < in->vertex_count; ++i)
        owner->map.verts[i] = (Vec2) {in->vertices[i].x, in->vertices[i].y};
    for (size_t i = 0; i < in->line_count; ++i) {
        const MeleeWebCollisionLine* line = &in->lines[i];
        owner->map.lines[i] = (MapLine) {line->v0, line->v1, line->prev0,
            line->next0, line->prev1, line->next1, line->hi_flags, line->lo_flags};
    }
    for (size_t i = 0; i < in->joint_count; ++i) {
        const MeleeWebCollisionJoint* src = &in->joints[i];
        MapJoint* dst = &owner->map.joints[i];
        COPY_RANGES(*dst, src->ranges);
        dst->left_bound = src->left; dst->bottom_bound = src->bottom;
        dst->right_bound = src->right; dst->top_bound = src->top;
        dst->vtx_start = src->vertices.start; dst->vtx_count = src->vertices.count;
    }
    return 1;
}

MeleeWebCollision* melee_web_collision_create(const MeleeWebCollisionInput* in, char* error, size_t size)
{
    uint64_t generation = melee_web_gameplay_generation();
    if (!generation || collision_owner || mpLib_804D64B4 || groundCollVtx || groundCollLine || groundCollJoint ||
        mpIsland_80458E88.next || mpIsland_80458E88.x4 || HSD_GObj_804D781C) {
        collision_fail(error, size, "Collision creation requires an idle gameplay world and exclusive original storage"); return NULL;
    }
    if (!collision_input(in, error, size)) return NULL;
    /* Original mpLibLoad allocates fixed-capacity arrays even for a tiny map.
     * Include conservative SDK cell headers/rounding, one GObj and its update process, and at most
     * one 0x2c island per floor/ceiling line. This catches undersized worlds;
     * genuine allocation failure/fragmentation retains HSD's explicit panic. */
    const size_t heap_budget = sizeof(CollVtx) * groundCollVtx_count +
        sizeof(CollLine) * groundCollLine_count + sizeof(CollJoint) * groundCollJoint_count +
        3 * 64 + 256 + (size_t) (in->ranges[0].count + in->ranges[1].count) * 128;
    const int32_t heap_free = melee_web_gameplay_stats().heap_free_bytes;
    if (heap_free < 0 || (size_t) heap_free < heap_budget) {
        collision_fail(error, size, "Gameplay heap has insufficient free space for original collision capacities"); return NULL;
    }
    MeleeWebCollision* owner = calloc(1, sizeof(*owner));
    if (!owner || !collision_map_copy(owner, in, error, size)) {
        free(owner); return NULL;
    }
    owner->generation = generation; collision_owner = owner;
    GroundParam param = {0}; param.y = in->stage_scale;
    GroundParam* saved_param = stage_info.param;
    GrKind saved_kind = stage_info.grkind;
    stage_info.param = &param; stage_info.grkind = (GrKind) in->stage_kind;
    mpLibLoad(&owner->map);
    stage_info.param = saved_param; stage_info.grkind = saved_kind;
    /* Original class-1/link-6 owner and priority-4 update process. Sharing this
     * object's userdata lifetime prevents updates after collision storage dies. */
    owner->object = mpLib_80058820_owned();
    GObj_InitUserData(owner->object, 0, collision_release, owner);
    /* Static adjacency selection can use alternate links. Check those actual
     * original selections before allowing an unbounded floor traversal. */
    for (int start = 0; start < owner->map.line_count; ++start)
        for (int direction = 0; direction < 2; ++direction) {
            int current = start, steps = 0;
            while (current != -1 && mpLineGetKind(current) == CollLine_Floor) {
                if (++steps > owner->map.line_count) {
                    melee_web_collision_destroy(owner, NULL, 0);
                    collision_fail(error, size, "Cyclic resolved floor-query chains are unsupported"); return NULL;
                }
                current = direction ? mpLineGetPrev(current) : mpLineGetNext(current);
            }
        }
    collision_success(error, size);
    return owner;
}

MeleeWebCollision* melee_web_collision_adopt_loaded(
    const MeleeWebCollisionInput* in, char* error, size_t size)
{
    const uint64_t generation = melee_web_gameplay_generation();
    HSD_GObj* object = NULL;
    if (!generation || collision_owner || !mpLib_804D64B4 ||
        mpLib_804D64B4 != stage_info.coll_data || !groundCollVtx ||
        !groundCollLine || !groundCollJoint || HSD_GObj_804D781C ||
        stage_info.grkind != (GrKind) (in ? in->stage_kind : -1)) {
        collision_fail(error, size, "Loaded source collision has no exclusive active stage context");
        return NULL;
    }
    if (!collision_input(in, error, size)) return NULL;
    for (HSD_GObj* candidate = ((HSD_GObj**) HSD_GObj_Entities)[6];
         candidate; candidate = candidate->next) {
        if (candidate->classifier != 1 || !candidate->proc ||
            candidate->proc->on_invoke != mpLib_800587FC)
            continue;
        if (object || candidate->proc->child || candidate->proc->s_link != 4 ||
            candidate->user_data) {
            collision_fail(error, size, "Original collision updater ownership is ambiguous");
            return NULL;
        }
        object = candidate;
    }
    if (!object) {
        collision_fail(error, size, "Original source collision updater is missing");
        return NULL;
    }
    MeleeWebCollision* owner = calloc(1, sizeof(*owner));
    if (!owner || !collision_map_copy(owner, in, error, size)) {
        free(owner); return NULL;
    }
    for (int i = 0; i < owner->map.line_count; ++i) {
        if (!groundCollLine[i].x0) {
            collision_map_clear(owner); free(owner);
            collision_fail(error, size, "Original collision line lost its loaded source descriptor");
            return NULL;
        }
        owner->map.lines[i] = *groundCollLine[i].x0;
    }
    owner->generation = generation; owner->object = object; collision_owner = owner;
    GObj_InitUserData(object, 0, collision_release, owner);
    for (int start = 0; start < owner->map.line_count; ++start)
        for (int direction = 0; direction < 2; ++direction) {
            int current = start, steps = 0;
            while (current != -1 && mpLineGetKind(current) == CollLine_Floor) {
                if (++steps > owner->map.line_count) {
                    melee_web_collision_destroy(owner, NULL, 0);
                    collision_fail(error, size, "Cyclic resolved source floor-query chains are unsupported");
                    return NULL;
                }
                current = direction ? mpLineGetPrev(current) : mpLineGetNext(current);
            }
        }
    collision_success(error, size);
    return owner;
}
#undef COPY_RANGES

int melee_web_collision_readiness(MeleeWebCollision* owner, MeleeWebCollisionReadiness* out, char* error, size_t size)
{
    if (!collision_live(owner, error, size)) return 0;
    if (!out) return collision_fail(error, size, "Collision readiness output is missing");
    MeleeWebCollisionReadiness result = {0};
    result.vertices = owner->map.vert_count; result.lines = owner->map.line_count; result.joints = owner->map.joint_count;
    result.storage_owned = result.original_indices_initialized = 1;
    for (mp_UnkStruct0* segment = mpIsland_80458E88.next; segment; segment = segment->next) ++result.floor_islands;
    for (mp_UnkStruct0* segment = mpIsland_80458E88.x4; segment; segment = segment->next) ++result.ceiling_islands;
    for (int i = 0; i < owner->map.line_count; ++i) if (owner->map.lines[i].hi_flags & LINE_FLAG_EMPTY) ++result.empty_lines;
    *out = result;
    return collision_success(error, size);
}
int melee_web_collision_line(MeleeWebCollision* owner, int32_t index, MeleeWebCollisionLineResult* out, char* error, size_t size)
{
    if (!collision_live(owner, error, size)) return 0;
    if (!out || index < 0 || index >= owner->map.line_count)
        return collision_fail(error, size, "Collision line query index or output is invalid");
    MeleeWebCollisionLineResult result = {0}; Vec3 v0, v1, normal;
    mpLineGetV0Pos(index, &v0); mpLineGetV1Pos(index, &v1);
    memcpy(result.v0, &v0, sizeof(v0)); memcpy(result.v1, &v1, sizeof(v1));
    result.kind = mpLineGetKind(index); result.material_flags = mpLineGetFlags(index);
    result.runtime_flags = groundCollLine[index].flags;
    result.previous = mpLineGetPrev(index); result.next = mpLineGetNext(index); result.joint = mpJointFromLine(index);
    result.pruned_hi_flags = owner->map.lines[index].hi_flags;
    if (v0.x != v1.x || v0.y != v1.y) {
        mpLineGetNormal(index, &normal); memcpy(result.normal, &normal, sizeof(normal)); result.has_normal = 1;
    }
    *out = result;
    return collision_success(error, size);
}
int melee_web_collision_floor(MeleeWebCollision* owner, int32_t index, float x, float y,
                              MeleeWebCollisionFloorResult* out, char* error, size_t size)
{
    if (!collision_live(owner, error, size)) return 0;
    if (!out || index < 0 || index >= owner->map.line_count || !isfinite(x) || !isfinite(y) ||
        mpLineGetKind(index) != CollLine_Floor || (groundCollLine[index].flags & LINE_FLAG_EMPTY))
        return collision_fail(error, size, "Floor query requires a finite point and a nonempty static floor line");
    for (int direction = 0; direction < 2; ++direction) {
        int current = index;
        while (current != -1 && mpLineGetKind(current) == CollLine_Floor) {
            const MapLine* line = groundCollLine[current].x0;
            if (groundCollVtx[line->v1_idx].pos.x <= groundCollVtx[line->v0_idx].pos.x)
                return collision_fail(error, size, "Floor query chain contains a degenerate line retained by source stage policy");
            current = direction ? mpLineGetPrev(current) : mpLineGetNext(current);
        }
    }
    MeleeWebCollisionFloorResult result = {0}; Vec3 position = {x, y, 0}, normal = {0};
    result.line = mpLib_8004DD90_Floor(index, &position, &result.displacement_y, &result.material_flags, &normal);
    memcpy(result.normal, &normal, sizeof(normal));
    if (!isfinite(result.displacement_y)) return collision_fail(error, size, "Original floor displacement is nonfinite");
    *out = result;
    return collision_success(error, size);
}
int melee_web_collision_destroy(MeleeWebCollision* owner, char* error, size_t size)
{
    if (!owner) return collision_success(error, size);
    if (owner->object) {
        if (HSD_GObj_804D781C) return collision_fail(error, size, "Cannot destroy collision storage during a process callback");
        if (!collision_live(owner, error, size)) return 0;
        HSD_GObjPLink_80390228(owner->object);
    }
    free(owner);
    return collision_success(error, size);
}
