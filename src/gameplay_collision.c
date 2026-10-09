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

/* These bounded owner tables match the fixed arrays declared in the included
 * retail mplib.c: groundCollLine_count=1536 and groundCollJoint_count=256. */
#define COLLISION_SOURCE_MAX_LINES 1536
#define COLLISION_SOURCE_MAX_JOINTS 256

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
    const MapCollData* source_map;
    const Vec2* source_vertices;
    const MapLine* source_lines;
    const MapJoint* source_joints;
    GrKind source_stage_kind;
    GrTouchLineCallback source_touch_line;
    uint16_t source_dynamic_line_joint[COLLISION_SOURCE_MAX_LINES];
    uint8_t source_dynamic_line_is_dynamic[COLLISION_SOURCE_MAX_LINES];
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
static int collision_input(const MeleeWebCollisionInput* in, int source_loaded,
                           char* error, size_t size)
{
    if (groundCollLine_count != COLLISION_SOURCE_MAX_LINES ||
        groundCollJoint_count != COLLISION_SOURCE_MAX_JOINTS)
        return collision_fail(error, size, "Original collision capacities exceed the bounded source owner tables");
    if (!in || !in->vertices || !in->lines || !in->joints ||
        !in->vertex_count || in->vertex_count > groundCollVtx_count ||
        !in->line_count || in->line_count > groundCollLine_count ||
        !in->joint_count || in->joint_count > groundCollJoint_count)
        return collision_fail(error, size, "Collision arrays exceed original storage capacities or are missing");
    if (in->stage_kind < 0 || in->stage_kind >= Gr_Kind_Count ||
        !isfinite(in->stage_scale) || in->stage_scale <= 0.0F)
        return collision_fail(error, size, "Collision requires a source stage kind and positive finite stage scale");
    if (in->ranges[4].count && !source_loaded)
        return collision_fail(error, size, "Dynamic collision lines require pending stage bindings and callbacks");
    unsigned char categories[COLLISION_SOURCE_MAX_LINES] = {0};
    for (unsigned category = 0; category < 5; ++category) {
        MeleeWebCollisionRange range = in->ranges[category];
        if (!collision_range(range, in->line_count))
            return collision_fail(error, size, "Collision category range is invalid");
        for (int i = range.start; i < range.start + range.count; ++i) {
            if (categories[i] ||
                (category < 4 && (in->lines[i].hi_flags & LINE_FLAG_KIND) != (1U << category)))
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
        /* Dynamic effective kinds come from original current-endpoint
         * classification; an authored floor hint is not a static floor. */
        const MeleeWebCollisionRange static_floor = in->ranges[0];
        if ((int) i >= static_floor.start && (int) i < static_floor.start + static_floor.count &&
            (dx != 0 || dy != 0) && dx <= 0)
            return collision_fail(error, size, "Static floor queries require source left-to-right nonvertical lines");
    }
    /* mpIsland seeds only the static floor/ceiling ranges. It follows raw
     * authored links by kind without stopping at a category boundary, so a
     * static seed that enters another range still needs cycle protection. */
    for (unsigned category = 0; category < 2; ++category) {
        const MeleeWebCollisionRange range = in->ranges[category];
        const unsigned kind = 1U << category;
        for (int start = range.start; start < range.start + range.count; ++start) {
            int current = start;
            size_t steps = 0;
            while (current != -1 && (in->lines[current].hi_flags & LINE_FLAG_KIND) == kind) {
                if (++steps > in->line_count)
                    return collision_fail(error, size, "Cyclic static island chains are unsupported");
                current = kind == CollLine_Floor ? in->lines[current].next0 : in->lines[current].prev0;
            }
        }
    }
    return 1;
}

static int collision_source_joint_list(size_t joint_count,
                                       unsigned char linked[COLLISION_SOURCE_MAX_JOINTS],
                                       char* error, size_t size)
{
    CollJoint* current = jointListStart;
    CollJoint* last = NULL;
    const uintptr_t base = (uintptr_t) groundCollJoint;
    const uintptr_t end = base + joint_count * sizeof(*groundCollJoint);
    size_t steps = 0;
    if ((!jointListStart) != (!jointListEnd))
        return collision_fail(error, size, "Source dynamic collision active-joint list has mismatched endpoints");
    while (current) {
        const uintptr_t address = (uintptr_t) current;
        if (address < base || address >= end ||
            (address - base) % sizeof(*groundCollJoint) != 0)
            return collision_fail(error, size, "Source dynamic collision active-joint list has a foreign owner");
        const size_t index = (address - base) / sizeof(*groundCollJoint);
        if (linked[index])
            return collision_fail(error, size, "Source dynamic collision active-joint list contains a cycle");
        linked[index] = 1;
        last = current;
        current = current->next;
        if (++steps > joint_count)
            return collision_fail(error, size, "Source dynamic collision active-joint list exceeds its source bounds");
    }
    if (last != jointListEnd)
        return collision_fail(error, size, "Source dynamic collision active-joint list tail changed");
    return 1;
}

static int collision_source_build_line_owners(MeleeWebCollision* owner,
                                              char* error, size_t size)
{
    if (!owner->map.dynamic_count) return 1;
    for (size_t i = 0; i < (size_t) owner->map.joint_count; ++i) {
        const MapJoint* joint = &owner->map.joints[i];
        if (!joint->dynamic_count) continue;
        const int starts[5] = {joint->floor_start, joint->ceiling_start,
            joint->right_wall_start, joint->left_wall_start, joint->dynamic_start};
        const int counts[5] = {joint->floor_count, joint->ceiling_count,
            joint->right_wall_count, joint->left_wall_count, joint->dynamic_count};
        for (unsigned category = 0; category < 5; ++category) {
            if (starts[category] < 0 || counts[category] < 0 ||
                starts[category] > owner->map.line_count ||
                counts[category] > owner->map.line_count - starts[category])
                return collision_fail(error, size, "Source dynamic collision joint line range is outside its loaded map");
            for (int line = starts[category]; line < starts[category] + counts[category]; ++line) {
                const uint16_t joint_owner = (uint16_t) (i + 1);
                if (owner->source_dynamic_line_joint[line] &&
                    owner->source_dynamic_line_joint[line] != joint_owner)
                    return collision_fail(error, size, "Source dynamic collision joints have overlapping owned line ranges");
                owner->source_dynamic_line_joint[line] = joint_owner;
                if (category == 4) owner->source_dynamic_line_is_dynamic[line] = 1;
            }
        }
    }
    return 1;
}

/* Retail Stage_8022524C loads the DAT collision map before StageData::on_init.
 * A dynamic descriptor may be deliberately deferred only in the exact source
 * state produced by removing its joint: correct loaded ownership, disabled and
 * unlinked joint, and every owned category line disabled. */
static int collision_source_dynamic_state(const MeleeWebCollision* owner,
                                         int* all_joints_bound,
                                         char* error, size_t size)
{
    const MapCollData* expected = &owner->map;
    MapCollData* source = stage_info.coll_data;
    if (all_joints_bound) *all_joints_bound = 0;
    if (!expected->dynamic_count) return 1;
    if (!owner->source_touch_line || !stage_info.on_touch_line)
        return collision_fail(error, size, "Source dynamic collision has no authored touch-line callback");
    if (stage_info.on_touch_line != owner->source_touch_line)
        return collision_fail(error, size, "Source dynamic collision touch-line callback changed");
    if (!expected->joints || !source || source != owner->source_map || source != mpLib_804D64B4 ||
        stage_info.grkind != owner->source_stage_kind ||
        !source->verts || !source->lines || !source->joints ||
        source->verts != owner->source_vertices || source->lines != owner->source_lines ||
        source->joints != owner->source_joints ||
        source->vert_count != expected->vert_count ||
        source->line_count != expected->line_count ||
        source->joint_count != expected->joint_count ||
        source->floor_start != expected->floor_start ||
        source->floor_count != expected->floor_count ||
        source->ceiling_start != expected->ceiling_start ||
        source->ceiling_count != expected->ceiling_count ||
        source->right_wall_start != expected->right_wall_start ||
        source->right_wall_count != expected->right_wall_count ||
        source->left_wall_start != expected->left_wall_start ||
        source->left_wall_count != expected->left_wall_count ||
        source->dynamic_start != expected->dynamic_start ||
        source->dynamic_count != expected->dynamic_count ||
        source->x2C != expected->x2C ||
        !groundCollVtx || !groundCollLine || !groundCollJoint)
        return collision_fail(error, size, "Source dynamic collision lost its original loaded map descriptor");

    if (source->line_count > COLLISION_SOURCE_MAX_LINES ||
        source->joint_count > COLLISION_SOURCE_MAX_JOINTS)
        return collision_fail(error, size, "Source dynamic collision dimensions exceed the original fixed capacities");
    unsigned char linked[COLLISION_SOURCE_MAX_JOINTS] = {0};
    if (!collision_source_joint_list((size_t) source->joint_count, linked, error, size)) return 0;
    unsigned char dynamic_line_owners[COLLISION_SOURCE_MAX_LINES] = {0};
    unsigned char joint_must_disable[COLLISION_SOURCE_MAX_JOINTS] = {0};
    int every_dynamic_joint_bound = 1;
    for (size_t i = 0; i < (size_t) source->joint_count; ++i) {
        const MapJoint* loaded = &source->joints[i];
        const MapJoint* authored = &expected->joints[i];
        if (memcmp(loaded, authored, sizeof(*loaded)) != 0)
            return collision_fail(error, size, "Source dynamic collision joint descriptor or range changed");
        if (!loaded->dynamic_count) continue;
        const CollJoint* bound = &groundCollJoint[i];
        if (bound->inner != loaded)
            return collision_fail(error, size, "Source dynamic collision joint lost its loaded MapJoint owner");
        if (!!(bound->flags & CollJoint_Enabled) != !!linked[i])
            return collision_fail(error, size, "Source dynamic collision joint enabled flag disagrees with active-list membership");
        if (!bound->x20) {
            every_dynamic_joint_bound = 0;
            if (bound->flags & CollJoint_Enabled || linked[i])
                return collision_fail(error, size, "Source dynamic collision joint is active without its authored stage JObj");
            joint_must_disable[i] = 1;
        } else if (!(bound->flags & CollJoint_Enabled)) {
            joint_must_disable[i] = 1;
        }
        for (int line = loaded->dynamic_start;
             line < loaded->dynamic_start + loaded->dynamic_count; ++line) {
            if (line < expected->dynamic_start ||
                line >= expected->dynamic_start + expected->dynamic_count ||
                owner->source_dynamic_line_joint[line] != i + 1 ||
                !owner->source_dynamic_line_is_dynamic[line] ||
                groundCollLine[line].x0 != &source->lines[line])
                return collision_fail(error, size, "Source dynamic collision line has no original joint-owned descriptor");
            if (dynamic_line_owners[line]++)
                return collision_fail(error, size, "Source dynamic collision line is owned by overlapping stage joints");
        }
    }
    for (int line = expected->dynamic_start;
         line < expected->dynamic_start + expected->dynamic_count; ++line)
        if (dynamic_line_owners[line] != 1)
            return collision_fail(error, size, "Source dynamic collision ranges do not cover every authored dynamic line");
    for (int line = 0; line < source->line_count; ++line) {
        const uint16_t encoded_joint = owner->source_dynamic_line_joint[line];
        if (!encoded_joint) continue;
        if (groundCollLine[line].x0 != &source->lines[line])
            return collision_fail(error, size, "Source dynamic collision line has no original joint-owned descriptor");
        const size_t joint = encoded_joint - 1;
        const int enabled = (groundCollLine[line].flags & LINE_FLAG_ENABLED) != 0;
        if (joint_must_disable[joint] && enabled)
            return collision_fail(error, size, "Removed source dynamic collision joint retains an enabled owned line");
    }
    if (all_joints_bound) *all_joints_bound = every_dynamic_joint_bound;
    return 1;
}

static int collision_source_line_descriptor(const MeleeWebCollision* owner,
                                            int line, char* error, size_t size)
{
    const MapLine* descriptors = owner->source_lines ? owner->source_lines : owner->map.lines;
    if (line < 0 || line >= owner->map.line_count || !descriptors ||
        !groundCollLine || !groundCollVtx ||
        groundCollLine[line].x0 != &descriptors[line])
        return collision_fail(error, size, "Collision line descriptor escaped its loaded source map");
    const MapLine* descriptor = &descriptors[line];
    if (descriptor->v0_idx < 0 || descriptor->v0_idx >= owner->map.vert_count ||
        descriptor->v1_idx < 0 || descriptor->v1_idx >= owner->map.vert_count)
        return collision_fail(error, size, "Collision line descriptor has invalid source vertices");
    return 1;
}

/* Retail mpLineGetNext/Prev inspect the alternate id1 line's flags and, when
 * enabled, its descriptor and endpoint vertices before returning either id1
 * or the raw id0 fallback. Validate every raw alternative and its descriptor
 * first so the unchanged retail selector never dereferences outside this map. */
static int collision_source_line_selection(const MeleeWebCollision* owner,
                                           int line, char* error, size_t size)
{
    if (!collision_source_line_descriptor(owner, line, error, size)) return 0;
    const MapLine* descriptors = owner->source_lines ? owner->source_lines : owner->map.lines;
    const MapLine* descriptor = &descriptors[line];
    const int alternatives[4] = {
        descriptor->prev_id0, descriptor->prev_id1,
        descriptor->next_id0, descriptor->next_id1,
    };
    for (size_t i = 0; i < sizeof(alternatives) / sizeof(alternatives[0]); ++i) {
        if (!collision_adjacency(alternatives[i], (size_t) owner->map.line_count))
            return collision_fail(error, size, "Retail line adjacency escaped the loaded source map");
        if (alternatives[i] != -1 &&
            !collision_source_line_descriptor(owner, alternatives[i], error, size))
            return 0;
    }
    return 1;
}

static int collision_resolved_floor_chains(MeleeWebCollision* owner,
                                           char* error, size_t size)
{
    for (int start = 0; start < owner->map.line_count; ++start) {
        if (!(groundCollLine[start].flags & LINE_FLAG_ENABLED) ||
            mpLineGetKind(start) != CollLine_Floor) continue;
        for (int direction = 0; direction < 2; ++direction) {
            int current = start;
            size_t steps = 0;
            while (current != -1) {
                if (current < 0 || current >= owner->map.line_count)
                    return collision_fail(error, size, "Resolved floor adjacency escaped its loaded source map");
                if (!collision_source_line_selection(owner, current, error, size)) return 0;
                if (mpLineGetKind(current) != CollLine_Floor) break;
                if (++steps > (size_t) owner->map.line_count)
                    return collision_fail(error, size, "Cyclic resolved floor-query chains are unsupported");
                current = direction ? mpLineGetPrev(current) : mpLineGetNext(current);
            }
        }
    }
    return 1;
}

static void collision_release_storage(void)
{
    /* B334 attaches dynamic x10/x14 chains to active next/x4; x8/xC
     * are borrowed tails. AE1C partitions static nodes into active and
     * disabled x18/x1C chains; B004 retains unused dynamic nodes in x20.
     * Only these five roots own disjoint allocations. */
    mp_UnkStruct0* lists[] = {
        mpIsland_80458E88.next, mpIsland_80458E88.x4,
        mpIsland_80458E88.x18, mpIsland_80458E88.x1C,
        mpIsland_80458E88.x20,
    };
    for (unsigned i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i)
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
    if (!collision_input(in, 0, error, size)) return NULL;
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
    /* Only enabled floor lines can seed an executable query. A walk still
     * follows original adjacency across disabled boundaries; the query itself
     * rejects any disabled node before calling retail floor arithmetic. */
    if (!collision_resolved_floor_chains(owner, error, size)) {
        melee_web_collision_destroy(owner, NULL, 0);
        return NULL;
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
    if (!collision_input(in, 1, error, size)) return NULL;
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
    if (!collision_source_build_line_owners(owner, error, size)) {
        collision_map_clear(owner); free(owner);
        return NULL;
    }
    owner->source_map = mpLib_804D64B4;
    owner->source_vertices = owner->source_map->verts;
    owner->source_lines = owner->source_map->lines;
    owner->source_joints = owner->source_map->joints;
    owner->source_stage_kind = stage_info.grkind;
    owner->source_touch_line = stage_info.on_touch_line;
    for (int i = 0; i < owner->map.line_count; ++i) {
        if (!groundCollLine[i].x0) {
            collision_map_clear(owner); free(owner);
            collision_fail(error, size, "Original collision line lost its loaded source descriptor");
            return NULL;
        }
        owner->map.lines[i] = *groundCollLine[i].x0;
    }
    if (owner->map.dynamic_count &&
        !collision_source_dynamic_state(owner, NULL, error, size)) {
        collision_map_clear(owner); free(owner);
        return NULL;
    }
    owner->generation = generation; owner->object = object; collision_owner = owner;
    GObj_InitUserData(object, 0, collision_release, owner);
    if (!collision_resolved_floor_chains(owner, error, size)) {
        melee_web_collision_destroy(owner, NULL, 0);
        return NULL;
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
    if (owner->map.dynamic_count) {
        if (!collision_source_dynamic_state(owner, &result.stage_joint_bindings_ready,
                                            error, size)) return 0;
        result.stage_callbacks_ready = 1;
    }
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
    if (owner->map.dynamic_count &&
        !collision_source_dynamic_state(owner, NULL, error, size)) return 0;
    if (!collision_source_line_selection(owner, index, error, size)) return 0;
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
    if (!out || index < 0 || index >= owner->map.line_count || !isfinite(x) || !isfinite(y))
        return collision_fail(error, size, "Floor query requires a valid line and finite point");
    if (owner->map.dynamic_count &&
        !collision_source_dynamic_state(owner, NULL, error, size)) return 0;
    if (!collision_source_line_selection(owner, index, error, size)) return 0;
    if (mpLineGetKind(index) != CollLine_Floor ||
        (groundCollLine[index].flags & LINE_FLAG_EMPTY))
        return collision_fail(error, size, "Floor query requires a nonempty source floor line");
    for (int direction = 0; direction < 2; ++direction) {
        int current = index;
        size_t steps = 0;
        while (current != -1) {
            if (current < 0 || current >= owner->map.line_count)
                return collision_fail(error, size, "Floor query adjacency escaped its loaded source map");
            if (!collision_source_line_selection(owner, current, error, size)) return 0;
            if (mpLineGetKind(current) != CollLine_Floor) break;
            if (++steps > (size_t) owner->map.line_count)
                return collision_fail(error, size, "Floor query resolved floor chain is cyclic");
            if (!(groundCollLine[current].flags & LINE_FLAG_ENABLED))
                return collision_fail(error, size, "Floor query crosses a disabled source floor line");
            if (groundCollLine[current].flags & LINE_FLAG_EMPTY)
                return collision_fail(error, size, "Floor query crosses an empty source floor line");
            const MapLine* line = groundCollLine[current].x0;
            if (!line || line->v0_idx >= owner->map.vert_count ||
                line->v1_idx >= owner->map.vert_count)
                return collision_fail(error, size, "Floor query line lost its loaded source vertices");
            const Vec2 v0 = groundCollVtx[line->v0_idx].pos;
            const Vec2 v1 = groundCollVtx[line->v1_idx].pos;
            if (!isfinite(v0.x) || !isfinite(v0.y) ||
                !isfinite(v1.x) || !isfinite(v1.y))
                return collision_fail(error, size, "Floor query source endpoints are nonfinite");
            if (v1.x <= v0.x)
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
