/* Semantic companion to tools/retail_cpu_observation.py. No source writes. */
#include "gameplay_cpu_observation.h"
#include "gameplay_match_rules.h"
#include <stddef.h>
#include <melee/cm/camera.h>
#include <melee/cm/types.h>
#include <melee/ft/types.h>
#include <melee/it/it_3F14.h>
#include <melee/it/types.h>
#include <melee/ft/ftparts.h>
#include <melee/if/ifmagnify.h>
#include <melee/if/ifstatus.h>
#include <melee/if/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/jobj.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

extern uint32_t gm_GetFrameCount(void);
extern uint32_t gm_8016AEEC(void);
extern uint16_t gm_8016AEFC(void);
extern int melee_web_match_source_result(void);
extern int melee_web_match_end_state(void);

/* These records are consumed through their retail ABI offsets below. Keep
 * the assertions unconditional so a source-layout drift fails at compile
 * time instead of silently changing the audit schema. */
_Static_assert(sizeof(struct CpuFighter) == 0x57C, "CpuFighter size drift");
_Static_assert(offsetof(struct CpuFighter, xC) == 0x0C, "CpuFighter kind drift");
_Static_assert(offsetof(struct CpuFighter, x44) == 0x44, "CpuFighter target drift");
_Static_assert(offsetof(struct CpuFighter, xA8_array) == 0xA8, "CpuFighter defend queue drift");
_Static_assert(offsetof(struct CpuFighter, xC8) == 0xC8, "CpuFighter defend count drift");
_Static_assert(offsetof(struct CpuFighter, xCC_array) == 0xCC, "CpuFighter attack queue drift");
_Static_assert(offsetof(struct CpuFighter, xEC) == 0xEC, "CpuFighter attack count drift");
_Static_assert(offsetof(struct CpuFighter, xFC) == 0xFC, "CpuFighter decision flags drift");
_Static_assert(offsetof(struct CpuFighter, command_duration) == 0x44C, "CpuFighter duration drift");
_Static_assert(offsetof(struct CpuFighter, csP) == 0x450, "CpuFighter cursor drift");
_Static_assert(offsetof(struct CpuFighter, buffer) == 0x454, "CpuFighter buffer drift");
_Static_assert(offsetof(struct CpuFighter, write_pos) == 0x554, "CpuFighter write pointer drift");
_Static_assert(offsetof(struct Fighter, x890_cameraBox) == 0x890, "Fighter camera subject drift");
_Static_assert(offsetof(struct Fighter, cpu) == 0x1A88, "Fighter CPU block drift");
_Static_assert(offsetof(struct Fighter, dmg) == 0x182C, "Fighter damage block drift");
_Static_assert(offsetof(struct Fighter, dmg.x1838_percentTemp) == 0x1838,
               "Fighter damage temp drift");
_Static_assert(offsetof(struct Fighter, dmg.x183C_applied) == 0x183C,
               "Fighter damage applied drift");
_Static_assert(offsetof(struct Fighter, dmg.x1840) == 0x1840,
               "Fighter damage force flag drift");
_Static_assert(offsetof(struct Fighter, dmg.kb_applied) == 0x1850,
               "Fighter knockback drift");
_Static_assert(offsetof(struct Fighter, dmg.x189C_unk_num_frames) == 0x189C,
               "Fighter damage frame drift");
_Static_assert(offsetof(struct Fighter, dmg.x18a0) == 0x18A0,
               "Fighter damage source drift");
_Static_assert(offsetof(struct Fighter, dmg.x195c_hitlag_frames) == 0x195C,
               "Fighter hitlag counter drift");
_Static_assert(offsetof(struct CmSubject, state) == 0x08, "camera subject state drift");
_Static_assert(offsetof(struct CmSubject, state_timer) == 0x0E, "camera subject timer drift");
_Static_assert(offsetof(struct CmSubject, pos) == 0x10, "camera subject position drift");
_Static_assert(offsetof(struct CmSubject, bone_pos) == 0x1C, "camera subject bone drift");
_Static_assert(offsetof(struct IfDamageState, damage_percent) == 0x0A, "HUD damage drift");
_Static_assert(offsetof(struct IfDamageState, old_damage) == 0x0C, "HUD old damage drift");
_Static_assert(offsetof(struct IfDamageState, damage_from_last_attack) == 0x0E, "HUD attack damage drift");
_Static_assert(offsetof(struct IfDamageState, frames_of_shake_remaining) == 0x0F, "HUD shake drift");
_Static_assert(offsetof(struct IfDamageState, flags) == 0x10, "HUD flags drift");
_Static_assert(offsetof(ifMagnifyPlayer, state) == 0x0C, "magnifier flags drift");
_Static_assert(offsetof(struct HSD_GObj, hsd_obj) == 0x28, "camera gobj drift");
_Static_assert(sizeof(struct HSD_JObj) == 0x88, "JObj size drift");
_Static_assert(offsetof(struct HSD_JObj, parent) == 0x0C, "JObj parent drift");
_Static_assert(offsetof(struct HSD_JObj, flags) == 0x14, "JObj flags drift");
_Static_assert(offsetof(struct HSD_JObj, rotate) == 0x1C, "JObj rotation drift");
_Static_assert(offsetof(struct HSD_JObj, scale) == 0x2C, "JObj scale drift");
_Static_assert(offsetof(struct HSD_JObj, translate) == 0x38, "JObj translation drift");
_Static_assert(offsetof(struct HSD_JObj, mtx) == 0x44, "JObj matrix drift");
_Static_assert(offsetof(struct HSD_CObj, near) == 0x38, "camera near drift");
_Static_assert(offsetof(struct HSD_CObj, far) == 0x3C, "camera far drift");
_Static_assert(offsetof(struct HSD_CObj, projection_param.perspective.fov) == 0x40,
               "camera FOV drift");
_Static_assert(offsetof(struct HSD_CObj, projection_type) == 0x50,
               "camera projection drift");

static int enabled, drawing;
static int hitlag_audit_requested, hitlag_audit;
static unsigned count, types[6];
static size_t draws, preparation_draws, used;
static size_t source_event_cursor = (size_t)-1;
static unsigned source_event_sequence;
/* One matrix record contains the normalized chain twice (bone->root and
 * root->bone), including local SRT and 3x4 matrix values for every node. */
static char line[65536];
#define MELEE_WEB_MATRIX_AUDIT_MAX_ANCESTORS 64
int melee_web_cpu_observation_available(void)
{
#ifdef __EMSCRIPTEN__
    return EM_ASM_INT({
        return typeof window !== 'undefined' &&
            window.__meleeCaptureWholeSessionCpuObservation === true &&
            typeof window.meleeCpuObservation === 'function';
    });
#else
    return 0;
#endif
}
static void put(const char* format, ...)
{
    va_list args; va_start(args, format);
    int n = vsnprintf(line + used, sizeof(line) - used, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof(line) - used) abort();
    used += (size_t)n;
}
static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static void vec(const Vec3* v)
{ put("[\"%08x\",\"%08x\",\"%08x\"]", bits(v->x), bits(v->y), bits(v->z)); }
static void quat(const Quaternion* q)
{ put("[\"%08x\",\"%08x\",\"%08x\",\"%08x\"]", bits(q->x), bits(q->y), bits(q->z), bits(q->w)); }
static void matrix(const Mtx m)
{
    put("[");
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned col = 0; col < 4; ++col)
            put("%s\"%08x\"", row || col ? "," : "", bits(m[row][col]));
    put("]");
}
static size_t matrix_chain(const HSD_JObj* target,
                           const HSD_JObj* nodes[MELEE_WEB_MATRIX_AUDIT_MAX_ANCESTORS])
{
    size_t count = 0;
    for (const HSD_JObj* node = target; node; node = node->parent) {
        if (count >= MELEE_WEB_MATRIX_AUDIT_MAX_ANCESTORS) abort();
        /* A malformed parent cycle would otherwise make the observer's
         * bounded walk look like a valid, longer skeleton. */
        for (size_t i = 0; i < count; ++i) if (nodes[i] == node) abort();
        nodes[count++] = node;
    }
    return count;
}
static void matrix_node(const HSD_JObj* node, size_t index, size_t count, int first)
{
    const int parent_index = index + 1 < count ? (int)(index + 1) : -1;
    put("%s{\"index\":%zu,\"parent_index\":%d,\"flags\":%u,\"rotate_bits\":",
        first ? "" : ",", index, parent_index, node->flags);
    quat(&node->rotate);
    put(",\"scale_bits\":"); vec(&node->scale);
    put(",\"translate_bits\":"); vec(&node->translate);
    put(",\"matrix_bits\":"); matrix(node->mtx);
    put("}");
}
static Fighter* fighter(unsigned slot)
{
    StaticPlayer* player = Player_GetPtrForSlot(slot);
    return player && player->player_entity[0] ? player->player_entity[0]->user_data : NULL;
}
static int target_slot(const Fighter* target)
{
    if (!target) return -1;
    for (unsigned slot = 0; slot < count; ++slot) if (fighter(slot) == target) return (int)slot;
    /* Never compare native or retail allocation addresses. */
    abort();
}
static void emit(void)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        const text = UTF8ToString($0);
        if (typeof window !== 'undefined' && window.meleeCpuObservation)
            window.meleeCpuObservation(text);
        else err('CPU_AUDIT ' + text);
    }, line);
#else
    fprintf(stderr, "CPU_AUDIT %s\n", line);
#endif
    used = 0;
}
static void cpu(const struct CpuFighter* c)
{
    uintptr_t base = (uintptr_t)c->buffer, write = (uintptr_t)c->write_pos;
    uintptr_t cursor = (uintptr_t)c->csP;
    if (write < base || write > base + sizeof(c->buffer) ||
        (cursor && (cursor < base || cursor >= base + sizeof(c->buffer))) ||
        c->xC8 > 8 || c->xEC > 8) abort();
    put("{\"kind\":%d,\"level\":%d,\"state\":%d,\"default_state\":%d,\"secondary_state\":%d,"
        "\"target_slot\":%d,\"buttons\":%u,\"sticks\":[%d,%d,%d,%d],\"triggers\":[%u,%u],"
        "\"command_duration\":%u,\"command_cursor\":%d,\"command_bytes\":\"",
        c->xC, c->level, c->x18, c->x1C, c->x20, target_slot(c->x44),
        (unsigned)c->buttons, c->lstick.x, c->lstick.y, c->cstick.x, c->cstick.y,
        c->ltrigger, c->rtrigger, c->command_duration, cursor ? (int)(cursor - base) : -1);
    for (size_t i = 0; i < write - base; ++i) put("%02x", (unsigned char)c->buffer[i]);
    put("\",\"decision_flags\":{\"xF8\":[%d,%d,%d,%d,%d,%d],"
        "\"xF9\":[%d,%d,%d,%d,%d,%d,%d,%d],"
        "\"xFA\":[%d,%d,%d,%d,%d,%d,%d],"
        "\"xFB\":[%d,%d,%d,%d,%d,%d,%d,%d]},\"defend_queue\":[",
        c->xF8_b0, c->xF8_b12, c->xF8_b34, c->xF8_b5, c->xF8_b6, c->xF8_b7,
        c->xF9_b0, c->xF9_b1, c->xF9_b2, c->xF9_b3,
        c->xF9_b4, c->xF9_b5, c->xF9_b6, c->xF9_b7,
        c->xFA_b0, c->xFA_b1, c->xFA_b2, c->xFA_b34,
        c->xFA_b5, c->xFA_b6, c->xFA_b7,
        c->xFB_b0, c->xFB_b1, c->xFB_b2, c->xFB_b3,
        c->xFB_b4, c->xFB_b5, c->xFB_b6, c->xFB_b7);
    for (unsigned i = 0; i < c->xC8; ++i) put("%s%d", i ? "," : "", c->xA8_array[i]);
    put("],\"attack_queue\":[");
    for (unsigned i = 0; i < c->xEC; ++i) put("%s%d", i ? "," : "", c->xCC_array[i]);
    put("]}");
}
static int item_owner_slot(const HSD_GObj* owner)
{
    if (!owner) return -1;
    int result = -1;
    for (unsigned i = 0; i < count; ++i) {
        StaticPlayer* player = Player_GetPtrForSlot(i);
        if (player && player->player_entity[0] == owner) {
            if (result >= 0) abort();
            result = (int)i;
        }
    }
    return result;
}
static void item_fields(const Item* item, int full)
{
    const int owner_slot = item_owner_slot(item->owner);
    put("{\"kind\":%d,\"spawn_kind\":%d,\"owner_present\":%s,\"owner_slot\":",
        (int)item->kind, (int)item->spawn_kind, item->owner ? "true" : "false");
    if (owner_slot >= 0) put("%d", owner_slot); else put("null");
    put(",\"damage_state\":{\"xC34_damageDealt\":%d,\"xC50\":%u,"
        "\"xCF4_fighter_gobj\":\"%p\",\"xDCE_b5\":%u},"
        "\"dispatch_state\":",
        item->xC34_damageDealt, (unsigned)item->xC50,
        (void*)item->xCF4_fighterGObjUnk, item->xDCE_flag.b5 ? 1u : 0u);
    if (item->kind == 65) {
        const ItemCommonData* common_data = it_804D6D28;
        const ItemLogicTable* logic = item->xB8_itemLogicTable;
        if (!common_data || !logic) abort();
        put("{\"xDCE_raw\":%u,\"xDCE_b4\":%s,\"xDCE_b5\":%s,"
            "\"ground_or_air\":%d,\"xC54_angle_bits\":\"0x%08x\","
            "\"unk_degrees_bits\":\"0x%08x\",\"shield_bounced_present\":%s,"
            "\"hit_shield_present\":%s}",
            (unsigned)item->xDCE_flag.u8, item->xDCE_flag.b4 ? "true" : "false",
            item->xDCE_flag.b5 ? "true" : "false", (int)item->ground_or_air,
            bits(item->xC54), bits(common_data->unk_degrees),
            logic->shield_bounced ? "true" : "false",
            logic->hit_shield ? "true" : "false");
    } else {
        put("null");
    }
    put(",\"position_bits\":");
    vec(&item->pos);
    put(",\"velocity_bits\":"); vec(&item->x40_vel);
    put(",\"facing_bits\":\"%08x\",\"scale_bits\":\"%08x\","
        "\"anim_id\":%d,\"anim_frame_bits\":\"%08x\","
        "\"anim_speed_bits\":\"%08x\",\"scheduler_priority\":%d,"
        "\"scheduler_cycle\":%d,\"hitboxes\":[",
        bits(item->facing_dir), bits(item->scl), (int)item->anim_id,
        bits(item->x5CC_currentAnimFrame), bits(item->x5D0_animFrameSpeed),
        (int)HSD_GObj_804D7834, (int)HSD_GObj_804D783C);
    for (unsigned i = 0; i < 4; ++i) {
        const HitCapsule* hit = &item->x5D4_hitboxes[i].hit;
        const int active = hit->state != HitCapsule_Disabled &&
                           hit->state != HitCapsule_Enabled;
        put("%s{\"index\":%u,\"state\":%u",
            i ? "," : "", i, (unsigned)hit->state);
        if (full || active) {
            put(",\"x4\":%u,\"damage_bits\":\"%08x\","
                "\"x42_b0\":%u,\"x42_b3\":%u,\"x42_b4\":%u,"
                "\"x42_b5\":%u,\"x43_b2\":%u,\"element\":%d",
                hit->x4, bits(hit->damage), hit->x42_b0 ? 1u : 0u,
                hit->x42_b3 ? 1u : 0u, hit->x42_b4 ? 1u : 0u,
                hit->x42_b5 ? 1u : 0u, hit->x43_b2 ? 1u : 0u,
                (int)hit->element);
            put(",\"b_offset_bits\":"); vec(&hit->b_offset);
            put(",\"scale_bits\":\"%08x\",\"x4c_bits\":", bits(hit->scale));
            vec(&hit->x4C); put(",\"x58_bits\":"); vec(&hit->x58);
            put(",\"hurt_coll_pos_bits\":"); vec(&hit->hurt_coll_pos);
            put(",\"coll_distance_bits\":\"%08x\"", bits(hit->coll_distance));
        }
        put("}");
    }
    if (full) {
        put("],\"p_link\":%u,\"process_priorities\":[",
            (unsigned)item->entity->p_link);
        unsigned process_count = 0;
        for (HSD_GObjProc* proc = item->entity->proc; proc; proc = proc->child) {
            if (process_count == 16) abort();
            put("%s%u", process_count ? "," : "", (unsigned)proc->s_link);
            ++process_count;
        }
        put("],\"process_count\":%u", process_count);
    } else {
        put("]");
    }
    put("}");
}
static void emit_source_event(void);
void melee_web_cpu_observation_shield_overlap(const char* phase,
                                             const Item* item,
                                             const HitCapsule* hurt,
                                             const Fighter* fp,
                                             const void* transform,
                                             int skip_update_pos,
                                             float item_scale,
                                             float fighter_scale_y,
                                             float fighter_pos_z,
                                             int overlap,
                                             const Vec3* collision_pos,
                                             float collision_angle)
{
    int hitbox_index = -1;
    for (int index = 0; item && index < 4; ++index) {
        if (&item->x5D4_hitboxes[index].hit == hurt) {
            hitbox_index = index;
            break;
        }
    }
    if (!enabled || source_event_cursor < 27045 || source_event_cursor > 27060 ||
        !item || !hurt || !fp || item->kind != 65 ||
        hitbox_index < 0 || item_owner_slot(item->owner) != 3 || fp != fighter(0))
        return;

    put("{\"session_cursor\":%zu,\"sequence\":%u,\"label\":\"arrow_shield_overlap_%s\","
        "\"hitbox_index\":%d,\"overlap\":%d,\"skip_update_pos\":%d,"
        "\"item_scale_bits\":\"%08x\","
        "\"fighter_scale_y_bits\":\"%08x\",\"fighter_pos_z_bits\":\"%08x\","
        "\"fighter_position_bits\":",
        source_event_cursor, source_event_sequence++, phase, hitbox_index, overlap, skip_update_pos,
        bits(item_scale), bits(fighter_scale_y), bits(fighter_pos_z));
    vec(&fp->cur_pos);
    put(",\"fighter_flags\":{\"x221B_b0\":%u,\"x221B_b1\":%u,"
        "\"x221B_b2\":%u,\"x221B_b3\":%u,\"x221B_b4\":%u},"
        "\"shield_hit\":{\"bone\":\"%p\",\"skip_update_pos\":%u,"
        "\"position_bits\":",
        fp->x221B_b0 ? 1u : 0u, fp->x221B_b1 ? 1u : 0u,
        fp->x221B_b2 ? 1u : 0u, fp->x221B_b3 ? 1u : 0u,
        fp->x221B_b4 ? 1u : 0u, (void*)fp->shield_hit.bone,
        fp->shield_hit.skip_update_pos ? 1u : 0u);
    vec(&fp->shield_hit.pos);
    put(",\"offset_bits\":"); vec(&fp->shield_hit.offset);
    put(",\"size_bits\":\"%08x\"},\"transform\":",
        bits(fp->shield_hit.size));
    if (transform) {
        const float (*matrix)[4] = (const float (*)[4])transform;
        put("[[\"%08x\",\"%08x\",\"%08x\",\"%08x\"],"
            "[\"%08x\",\"%08x\",\"%08x\",\"%08x\"],"
            "[\"%08x\",\"%08x\",\"%08x\",\"%08x\"]]",
            bits(matrix[0][0]), bits(matrix[0][1]), bits(matrix[0][2]), bits(matrix[0][3]),
            bits(matrix[1][0]), bits(matrix[1][1]), bits(matrix[1][2]), bits(matrix[1][3]),
            bits(matrix[2][0]), bits(matrix[2][1]), bits(matrix[2][2]), bits(matrix[2][3]));
    } else {
        put("null");
    }
    put(",\"shield_bone_matrix_bits\":");
    if (fp->shield_hit.bone) {
        const float (*matrix)[4] = (const float (*)[4])fp->shield_hit.bone->mtx;
        put("[[\"%08x\",\"%08x\",\"%08x\",\"%08x\"],"
            "[\"%08x\",\"%08x\",\"%08x\",\"%08x\"],"
            "[\"%08x\",\"%08x\",\"%08x\",\"%08x\"]]",
            bits(matrix[0][0]), bits(matrix[0][1]), bits(matrix[0][2]), bits(matrix[0][3]),
            bits(matrix[1][0]), bits(matrix[1][1]), bits(matrix[1][2]), bits(matrix[1][3]),
            bits(matrix[2][0]), bits(matrix[2][1]), bits(matrix[2][2]), bits(matrix[2][3]));
    } else {
        put("null");
    }
    if (collision_pos) {
        put(",\"ftcoll_inputs\":{\"collision_pos_bits\":");
        vec(collision_pos);
        put(",\"collision_angle_bits\":\"%08x\"}", bits(collision_angle));
    } else {
        put(",\"ftcoll_inputs\":null");
    }
    put(",\"arrow\":"); item_fields(item, 1);
    put("}");
    emit_source_event();
}
static void item_diagnostic(const struct CpuFighter* cpu, size_t slot, size_t index)
{
    if (slot != 2 || index < 27040 || index > 27060) {
        put("null");
        return;
    }
    if (!HSD_GObj_Entities) abort();
    HSD_GObj* nodes[64];
    size_t item_count = 0, selected_order = 0;
    int selected = 0;
    for (HSD_GObj* gobj = HSD_GObj_Entities->items; gobj; gobj = gobj->next) {
        if (item_count == 64) abort();
        nodes[item_count] = gobj;
        if (gobj->classifier == 6) {
            if (!gobj->user_data) abort();
            Item* item = (Item*)gobj->user_data;
            if (item->entity != gobj) abort();
            if (item == cpu->xF4) {
                if (selected) abort();
                selected = 1;
                selected_order = item_count;
            }
        }
        ++item_count;
    }
    put("{\"item_gobj_count\":%zu,\"xF4_present\":%s,\"xF4_member\":%s,"
        "\"selected_order\":",
        item_count, cpu->xF4 ? "true" : "false", selected ? "true" : "false");
    if (selected) put("%zu", selected_order); else put("null");
    put(",\"items\":[");
    for (size_t i = 0; i < item_count; ++i) {
        HSD_GObj* gobj = nodes[i];
        if (i) put(",");
        put("{\"list_order\":%zu,\"classifier\":%u,\"xF4_target\":%s,\"item\":",
            i, (unsigned)gobj->classifier,
            gobj->classifier == 6 && gobj->user_data == cpu->xF4 ? "true" : "false");
        if (gobj->classifier == 6) {
            Item* item = (Item*)gobj->user_data;
            if (!item || item->entity != gobj) abort();
            item_fields(item, item == cpu->xF4);
        } else {
            put("null");
        }
        put("}");
    }
    put("]}");
}
static Item* target_link_arrow(size_t* order, size_t* item_count)
{
    if (!HSD_GObj_Entities) abort();
    Item* target = NULL;
    size_t target_order = 0, visited = 0;
    for (HSD_GObj* gobj = HSD_GObj_Entities->items; gobj; gobj = gobj->next) {
        if (visited == 64) abort();
        if (gobj->classifier == 6) {
            if (!gobj->user_data) abort();
            Item* item = (Item*)gobj->user_data;
            if (item->entity != gobj) abort();
            if (item->kind == 65 && item_owner_slot(item->owner) == 3) {
                if (target) abort();
                target = item;
                target_order = visited;
            }
        }
        ++visited;
    }
    if (order) *order = target_order;
    if (item_count) *item_count = visited;
    return target;
}
void melee_web_cpu_observation_set_event_cursor(size_t index)
{
    source_event_cursor = index;
    source_event_sequence = 0;
}
static void emit_source_event(void)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        const text = UTF8ToString($0);
        if (typeof window !== 'undefined' && window.meleeCpuItemEvent)
            window.meleeCpuItemEvent(text);
        else err('CPU_ITEM_EVENT ' + text);
    }, line);
#else
    fprintf(stderr, "CPU_ITEM_EVENT %s\n", line);
#endif
    used = 0;
}

int melee_web_cpu_observation_lb_collision_probe_active(void)
{
    return enabled && source_event_cursor == 27055;
}

void melee_web_cpu_observation_lb_collision_probe(const MeleeWebLbCollProbe* probe)
{
    if (!probe || !melee_web_cpu_observation_lb_collision_probe_active())
        return;
    /* This single selected capsule is the previously localized Link Arrow
     * shield overlap. The observer does not feed these values back to source. */
    if (bits(probe->b[0]) != 0xc22e70e6 || bits(probe->b[1]) != 0x3fdb54c5 ||
        bits(probe->b[2]) != 0x00000000 || bits(probe->c[0]) != 0xc229058f ||
        bits(probe->c[1]) != 0x3f4fe2eb || bits(probe->c[2]) != 0x00000000)
        return;

    put("{\"session_cursor\":%zu,\"sequence\":%u,"
        "\"label\":\"arrow_lb_coll_800077a0_return\","
        "\"a_address\":\"%p\",\"matrix_address\":\"%p\","
        "\"b_address\":\"%p\",\"c_address\":\"%p\","
        "\"d_address\":\"%p\",\"e_address\":\"%p\","
        "\"angle_address\":\"%p\",\"radius_bits\":\"%08x\","
        "\"distance_offset_bits\":\"%08x\",\"a_bits\":[\"%08x\",\"%08x\",\"%08x\"],"
        "\"matrix_bits\":[",
        source_event_cursor, source_event_sequence++, (void*)probe->a_address,
        (void*)probe->matrix_address, (void*)probe->b_address, (void*)probe->c_address,
        (void*)probe->d_address, (void*)probe->e_address, (void*)probe->angle_address,
        bits(probe->radius), bits(probe->distance_offset), bits(probe->a[0]),
        bits(probe->a[1]), bits(probe->a[2]));
    for (size_t i = 0; i < 12; ++i)
        put("%s\"%08x\"", i ? "," : "", bits(probe->matrix[i]));
    put("],\"b_bits\":[\"%08x\",\"%08x\",\"%08x\"],"
        "\"c_bits\":[\"%08x\",\"%08x\",\"%08x\"],"
        "\"transformed_radius_bits\":[\"%08x\",\"%08x\",\"%08x\"],"
        "\"transformed_origin_bits\":[\"%08x\",\"%08x\",\"%08x\"],"
        "\"diff_cb_bits\":[\"%08x\",\"%08x\",\"%08x\"],"
        "\"diff_ba_bits\":[\"%08x\",\"%08x\",\"%08x\"],"
        "\"distance_bits\":\"%08x\",\"offset_distance_bits\":\"%08x\","
        "\"dot_diff_cb_bits\":\"%08x\",\"n0_bits\":\"%08x\","
        "\"ba_dot_bits\":\"%08x\",\"n1_bits\":\"%08x\",\"scale_bits\":\"%08x\","
        "\"normalize_e_bits\":[\"%08x\",\"%08x\",\"%08x\"],"
        "\"normal_bits\":[\"%08x\",\"%08x\",\"%08x\"],"
        "\"collision_position_bits\":[\"%08x\",\"%08x\",\"%08x\"],"
        "\"angle_bits\":\"%08x\"}",
        bits(probe->b[0]), bits(probe->b[1]), bits(probe->b[2]),
        bits(probe->c[0]), bits(probe->c[1]), bits(probe->c[2]),
        bits(probe->transformed_radius[0]), bits(probe->transformed_radius[1]),
        bits(probe->transformed_radius[2]), bits(probe->transformed_origin[0]),
        bits(probe->transformed_origin[1]), bits(probe->transformed_origin[2]),
        bits(probe->diff_cb[0]), bits(probe->diff_cb[1]), bits(probe->diff_cb[2]),
        bits(probe->diff_ba[0]), bits(probe->diff_ba[1]), bits(probe->diff_ba[2]),
        bits(probe->distance), bits(probe->offset_distance), bits(probe->dot_diff_cb),
        bits(probe->n0), bits(probe->ba_dot), bits(probe->n1), bits(probe->scale),
        bits(probe->normalize_e[0]), bits(probe->normalize_e[1]), bits(probe->normalize_e[2]),
        bits(probe->normal[0]), bits(probe->normal[1]), bits(probe->normal[2]),
        bits(probe->collision_position[0]), bits(probe->collision_position[1]),
        bits(probe->collision_position[2]), bits(probe->angle));
    emit_source_event();
}

void melee_web_cpu_observation_source_event(const char* label,
                                           const Fighter* fp,
                                           HSD_GObj* gobj)
{
    if (!enabled || source_event_cursor < 27035 || source_event_cursor > 27060)
        return;

    const Fighter* cpu_fp = NULL;
    Item* observed_arrow = NULL;
    if (gobj) {
        if (gobj->classifier != 6 || !gobj->user_data) abort();
        Item* item = (Item*)gobj->user_data;
        if (item->entity != gobj) abort();
        if (item->kind != 65 || item_owner_slot(item->owner) != 3) return;
        observed_arrow = item;
    } else if (fp) {
        if (fp != fighter(2)) return;
        cpu_fp = fp;
    } else {
        if (strcmp(label, "gameplay_step_return") != 0 &&
            strcmp(label, "post_tick_observer") != 0)
            abort();
        if (!fighter(2)) abort();
    }

    size_t arrow_order = 0, item_count = 0;
    Item* listed_arrow = target_link_arrow(&arrow_order, &item_count);
    if (observed_arrow && listed_arrow != observed_arrow) abort();
    if (!observed_arrow) observed_arrow = listed_arrow;

    put("{\"session_cursor\":%zu,\"sequence\":%u,\"label\":\"%s\","
        "\"item_gobj_count\":%zu,\"arrow_in_item_list\":%s,\"arrow_list_order\":",
        source_event_cursor, source_event_sequence++, label, item_count,
        listed_arrow ? "true" : "false");
    if (listed_arrow) put("%zu", arrow_order); else put("null");
    if (observed_arrow) {
        put(",\"arrow_identity\":{\"gobj\":\"%p\",\"item\":\"%p\","
            "\"owner_gobj\":\"%p\"}", (void*)observed_arrow->entity,
            (void*)observed_arrow, (void*)observed_arrow->owner);
        HSD_GObj* shield_target = observed_arrow->xCF4_fighterGObjUnk;
        int shield_target_slot = -1;
        if (shield_target) {
            for (unsigned slot = 0; slot < count; ++slot) {
                Fighter* target = fighter(slot);
                if (!target) abort();
                if (target->gobj == shield_target) {
                    if (shield_target_slot >= 0) abort();
                    shield_target_slot = (int)slot;
                }
            }
        }
        put(",\"arrow_collision\":{\"xC34_damageDealt\":%d,\"xC50\":%u,"
            "\"xCF4_fighter_gobj\":\"%p\",\"xCF4_fighter_slot\":",
            observed_arrow->xC34_damageDealt, (unsigned)observed_arrow->xC50,
            (void*)shield_target);
        if (shield_target_slot >= 0) put("%d", shield_target_slot);
        else put("null");
        put("}");
    } else {
        put(",\"arrow_identity\":null");
        put(",\"arrow_collision\":null");
    }
    Fighter* slot2 = fighter(2);
    put(",\"cpu_slot2_xF4_present\":");
    if (slot2) put("%s", slot2->cpu.xF4 ? "true" : "false");
    else put("null");
    put(",\"cpu_slot2_xF4_points_to_arrow\":");
    if (slot2 && observed_arrow)
        put("%s", slot2->cpu.xF4 == observed_arrow ? "true" : "false");
    else put("null");
    if (cpu_fp) {
        put(",\"cpu_scan_target_xF4_present\":%s,\"cpu_scan_xF8_b12\":%u",
            cpu_fp->cpu.xF4 ? "true" : "false", cpu_fp->cpu.xF8_b12);
    }
    if (observed_arrow) {
        put(",\"arrow\":");
        item_fields(observed_arrow, 1);
    } else {
        put(",\"arrow\":null");
    }
    put("}");
    emit_source_event();
}
void melee_web_cpu_observation_item_state_event(const char* phase,
                                                HSD_GObj* gobj,
                                                int msid,
                                                unsigned flags)
{
    char label[96];
    snprintf(label, sizeof(label), "%s_msid_%d_flags_%08x", phase, msid,
             flags);
    melee_web_cpu_observation_source_event(label, NULL, gobj);
}
void melee_web_cpu_observation_scheduler_return(void)
{
    if (!enabled || source_event_cursor < 27035 || source_event_cursor > 27060)
        return;
    if (!fighter(2)) abort();
    melee_web_cpu_observation_source_event("gameplay_step_return", NULL, NULL);
}
static void hitlag_audit_line(size_t index, unsigned slot, const Fighter* fp)
{
    const unsigned flags = (fp->x221A_b0 ? 1u : 0u) |
                           (fp->x221A_b1 ? 2u : 0u) |
                           (fp->allow_sdi ? 4u : 0u) |
                           (fp->x221A_b3 ? 8u : 0u) |
                           (fp->fall_fast ? 16u : 0u) |
                           (fp->x221A_b5 ? 32u : 0u) |
                           (fp->x221A_b6 ? 64u : 0u) |
                           (fp->x221A_b7 ? 128u : 0u);
    /* Keep this on stderr as a separate diagnostic stream. The regular
     * CPU_AUDIT schema and accepted replay bytes remain untouched. */
    fprintf(stderr,
            "HITLAG_AUDIT tick=%zu slot=%u motion=%d flags=%02x "
            "x221A_b3=%u allow_sdi=%u hitlag_frames_bits=%08x "
            "hitlag_frames=%g x1964_bits=%08x x1964=%g "
            "kb_applied_bits=%08x kb_applied=%g force_applied_on_hit=%u "
            "x1838_percentTemp_bits=%08x x1838_percentTemp=%g "
            "x183C_applied=%d x1840=%d x18a0_bits=%08x x18a0=%g "
            "x189C_num_frames_bits=%08x x189C_num_frames=%g\n",
            index, slot, fp->motion_id, flags, fp->x221A_b3 ? 1u : 0u,
            fp->allow_sdi ? 1u : 0u, bits(fp->dmg.x195c_hitlag_frames),
            fp->dmg.x195c_hitlag_frames, bits(fp->x1964), fp->x1964,
            bits(fp->dmg.kb_applied), fp->dmg.kb_applied,
            fp->dmg.kb_applied != 0.0f ? 1u : 0u,
            bits(fp->dmg.x1838_percentTemp), fp->dmg.x1838_percentTemp,
            fp->dmg.x183C_applied, fp->dmg.x1840, bits(fp->dmg.x18a0),
            fp->dmg.x18a0, bits(fp->dmg.x189C_unk_num_frames),
            fp->dmg.x189C_unk_num_frames);
}
static void hitlag_audit_tick(size_t index)
{
    if (!hitlag_audit) return;
    for (unsigned slot = 0; slot < count; ++slot) {
        Fighter* fp = fighter(slot);
        if (!fp) abort();
        hitlag_audit_line(index, slot, fp);
        /* Read the matrix left by source execution; never call SetupMatrix
         * or recompute a transform from an observer. This separate diagnostic
         * localizes camera-subject rounding without altering CPU_AUDIT. */
        const int bone = fp->ft_data->x0->camera_zoom_target_bone;
        if (bone < 0 || bone >= MAX_FT_PARTS || !fp->parts) abort();
        const HSD_JObj* joint = fp->parts[bone].joint;
        if (!joint) abort();
        const HSD_JObj* nodes[MELEE_WEB_MATRIX_AUDIT_MAX_ANCESTORS];
        const size_t node_count = matrix_chain(joint, nodes);
        put("{\"tick\":%zu,\"slot\":%u,\"phase\":\"after_source_tick\","
            "\"bone\":%d,\"fighter_bone_flags8\":%u,\"fighter_bone_flagsC\":%u,"
            "\"joint_flags\":%u,\"parent_present\":%s,\"matrix_bits\":",
            index, slot, bone, fp->parts[bone].flags8, fp->parts[bone].flagsC,
            joint->flags, joint->parent ? "true" : "false");
        matrix(joint->mtx);
        put(",\"offset_bits\":"); vec(&fp->co_attrs.x170);
        put(",\"camera_offset_bits\":"); vec(&fp->co_attrs.x170);
        put(",\"subject_bone_bits\":");
        if (fp->x890_cameraBox) vec(&fp->x890_cameraBox->bone_pos);
        else put("null");
        put(",\"ancestor_count_including_bone\":%zu,\"nodes_bone_to_root_order\":[",
            node_count);
        for (size_t i = 0; i < node_count; ++i)
            matrix_node(nodes[i], i, node_count, i == 0);
        put("],\"nodes_root_to_bone_parent_order\":[");
        for (size_t reverse = 0; reverse < node_count; ++reverse)
            matrix_node(nodes[node_count - reverse - 1], node_count - reverse - 1,
                        node_count, reverse == 0);
        put("]");
        put("}"); fprintf(stderr, "MATRIX_AUDIT %s\n", line); used = 0;
    }
}
static void snapshot(size_t index)
{
    Vec3 position, interest;
    Camera_GetTransformPosition(&position); Camera_GetTransformInterest(&interest);
    /* Camera_800310B8 recomputes a matrix: an observer must not call it. */
    const HSD_GObj* camera_object = Camera_80030A50();
    HSD_CObj* camera = camera_object ? camera_object->hsd_obj : NULL;
    if (!camera) abort();
    put("\"match\":{\"frame\":%u,\"seconds\":%u,\"subframe\":%u,\"outcome\":%d,\"end_state\":%d},"
        "\"camera\":{\"position_bits\":", gm_GetFrameCount(), gm_8016AEEC(), gm_8016AEFC(),
        melee_web_match_source_result(), melee_web_match_end_state());
    vec(&position); put(",\"interest_bits\":"); vec(&interest);
    put(",\"projection\":%u,\"fov_bits\":\"%08x\",\"near_bits\":\"%08x\",\"far_bits\":\"%08x\"},\"players\":[",
        camera->projection_type, bits(HSD_CObjGetFov(camera)), bits(camera->near), bits(camera->far));
    const HudIndex* hud = ifStatus_GetHUDInfo();
    for (unsigned slot = 0; slot < count; ++slot) {
        Fighter* fp = fighter(slot); if (!fp) abort();
        const IfDamageState* damage = &hud->players[slot];
        const ifMagnifyPlayer* magnify = &ifMagnify_804A1DE0.player[slot];
        put("%s{\"slot\":%u,\"cpu\":", slot ? "," : "", slot);
        if (types[slot] == 1) cpu(&fp->cpu); else put("null");
        if (slot == 2 && index >= 27040 && index <= 27060) {
            put(",\"item_diagnostic\":");
            item_diagnostic(&fp->cpu, slot, index);
        }
        put(",\"subject\":");
        const CmSubject* subject = fp->x890_cameraBox;
        if (!subject) put("null");
        else {
            put("{\"state\":%d,\"timer\":%d,\"on_ledge\":%u,\"force_inactive\":%u,\"was_framed\":%u,\"position_bits\":",
                subject->state, subject->state_timer, subject->on_ledge, subject->force_inactive, subject->was_framed);
            vec(&subject->pos); put(",\"bone_bits\":"); vec(&subject->bone_pos); put("}");
        }
        put(",\"magnifier\":{\"offscreen\":%u,\"ignore_offscreen\":%u,\"edge\":%u},"
            "\"hud\":{\"present\":%s,\"damage\":%d,\"old_damage\":%d,\"last_attack_damage\":%u,\"shake_frames\":%u,"
            "\"explode\":%u,\"randomize_velocity\":%u,\"force_shake\":%u,\"hide_digits\":%u,\"animation_status\":%u}}",
            magnify->state.is_offscreen, magnify->state.ignore_offscreen, magnify->state.edge,
            damage->HUD_parent_entity ? "true" : "false", damage->damage_percent, damage->old_damage,
            damage->damage_from_last_attack, damage->frames_of_shake_remaining,
            damage->flags.explode_animation, damage->flags.randomize_velocity, damage->flags.force_digit_shake,
            damage->flags.hide_all_digits, damage->flags.animation_status_id);
    }
    put("]");
}
void melee_web_cpu_observation_begin(const uint8_t setup[0x138], size_t frames, int source_drawing)
{
    enabled = 1; drawing = source_drawing; count = 0; draws = 0; preparation_draws = 0; used = 0;
    hitlag_audit = hitlag_audit_requested;
    hitlag_audit_requested = 0;
    for (unsigned slot = 0; slot < 6; ++slot) {
        types[slot] = setup[0x61 + slot * 0x24];
        if (count < 4 && (types[slot] == Gm_PKind_Human ||
                          types[slot] == Gm_PKind_Cpu)) {
            ++count;
        }
    }
    if (count < 2 || count > 4) abort();
    for (unsigned slot = count; slot < 6; ++slot) {
        if (types[slot] != Gm_PKind_NA) abort();
    }
    put("{\"record\":\"header\",\"schema\":\"melee-web-cpu-observation\",\"version\":1,"
        "\"frames_requested\":%zu,\"source_drawing\":%s,\"setup_hex\":\"", frames, drawing ? "true" : "false");
    for (unsigned i = 0; i < 0x138; ++i) put("%02x", setup[i]);
    put("\"}"); emit();
    put("{\"record\":\"initial\","); snapshot(0); put("}"); emit();
#ifndef MELEE_WEB_PUBLIC_RUNTIME
    /* Keep native address diagnostics out of the public binary, even when
     * unexported replay support retains this observer in the link closure. */
    if (hitlag_audit) {
        /* Native allocation identities are diagnostic data, never a retail
         * pointer model or part of the semantic CPU comparison. */
        put("{\"schema\":\"melee-web-native-cpu-address-diagnostic\",\"version\":1,"
            "\"phase\":\"initial\",\"players\":[");
        for (unsigned slot = 0; slot < count; ++slot) {
            const Fighter* fp = fighter(slot);
            if (!fp) abort();
            put("%s{\"slot\":%u,\"fighter_pointer\":%u,\"cpu_pointer\":%u}",
                slot ? "," : "", slot, (uint32_t)(uintptr_t)fp,
                (uint32_t)(uintptr_t)&fp->cpu);
        }
        put("]}");
        fprintf(stderr, "CPU_ADDRESS_AUDIT %s\n", line); used = 0;
    }
#endif
}
void melee_web_cpu_observation_enable_hitlag_audit(void)
{
    /* Called by an explicit native diagnostic command-line option before the
     * observer begins. It cannot alter accepted pads or source state. */
    hitlag_audit_requested = 1;
}
void melee_web_cpu_observation_tick(size_t index)
{
    if (!enabled) return;
    put("{\"record\":\"frame\",\"index\":%zu,", index); snapshot(index); put("}"); emit();
    hitlag_audit_tick(index);
    melee_web_cpu_observation_source_event("post_tick_observer", NULL, NULL);
}
void melee_web_cpu_observation_draw(size_t index)
{
    if (!enabled || !drawing) return;
    put("{\"record\":\"draw\",\"index\":%zu,\"source_index\":%zu,", draws++, index);
    snapshot(index); put("}"); emit();
}
void melee_web_cpu_observation_preparation_draw(void)
{
    if (!enabled || !drawing) return;
    /* GPU preparation still traverses original source rendering. Record its
     * state changes separately from the paired tick/draw timeline; these
     * unmatched draws are an accuracy failure, not invisible warm-up work. */
    put("{\"schema\":\"melee-web-cpu-preparation-observation\",\"version\":1,"
        "\"phase\":\"after_source_preparation_draw\",\"index\":%zu,", preparation_draws++);
    snapshot((size_t)-1); put("}");
#ifdef __EMSCRIPTEN__
    EM_ASM({
        const text = UTF8ToString($0);
        if (typeof window !== 'undefined' && window.meleeCpuPreparationObservation)
            window.meleeCpuPreparationObservation(text);
        else err('CPU_PREPARATION_AUDIT ' + text);
    }, line);
#else
    fprintf(stderr, "CPU_PREPARATION_AUDIT %s\n", line);
#endif
    used = 0;
}
void melee_web_cpu_observation_end(size_t frames)
{
    if (!enabled) return;
    put("{\"record\":\"end\",\"frames\":%zu,\"draws\":%zu,\"remaining_fighter_slots\":[", frames, draws);
    int first = 1;
    for (unsigned slot = 0; slot < 4; ++slot) if (fighter(slot)) {
        put("%s%u", first ? "" : ",", slot); first = 0;
    }
    put("],\"result\":");
    int outcome, winners[6], winner_count;
    if (melee_web_match_rules_terminal_result(&outcome, &winner_count, winners)) {
        if (winner_count < 0 || winner_count > 6) abort();
        put("{\"outcome\":%d,\"winners\":[", outcome);
        for (int i = 0; i < winner_count; ++i) {
            if (winners[i] < 0 || (unsigned)winners[i] >= count) abort();
            put("%s%d", i ? "," : "", winners[i]);
        }
        put("]}");
    } else put("null");
    put(",\"status\":\"captured\"}"); emit(); enabled = 0;
}
