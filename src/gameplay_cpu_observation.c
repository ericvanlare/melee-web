/* Semantic companion to tools/retail_cpu_observation.py. No source writes. */
#include "gameplay_cpu_observation.h"
#include "gameplay_match_rules.h"
#include <stddef.h>
#include <melee/cm/camera.h>
#include <melee/cm/types.h>
#include <melee/ft/types.h>
#include <melee/ft/ftparts.h>
#include <melee/if/ifmagnify.h>
#include <melee/if/ifstatus.h>
#include <melee/if/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
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
/* One matrix record contains the normalized chain twice (bone->root and
 * root->bone), including local SRT and 3x4 matrix values for every node. */
static char line[65536];
#define MELEE_WEB_MATRIX_AUDIT_MAX_ANCESTORS 64
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
    put("\",\"defend_queue\":[");
    for (unsigned i = 0; i < c->xC8; ++i) put("%s%d", i ? "," : "", c->xA8_array[i]);
    put("],\"attack_queue\":[");
    for (unsigned i = 0; i < c->xEC; ++i) put("%s%d", i ? "," : "", c->xCC_array[i]);
    put("]}");
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
static void snapshot(void)
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
    put("{\"record\":\"initial\","); snapshot(); put("}"); emit();
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
    put("{\"record\":\"frame\",\"index\":%zu,", index); snapshot(); put("}"); emit();
    hitlag_audit_tick(index);
}
void melee_web_cpu_observation_draw(size_t index)
{
    if (!enabled || !drawing) return;
    put("{\"record\":\"draw\",\"index\":%zu,\"source_index\":%zu,", draws++, index);
    snapshot(); put("}"); emit();
}
void melee_web_cpu_observation_preparation_draw(void)
{
    if (!enabled || !drawing) return;
    /* GPU preparation still traverses original source rendering. Record its
     * state changes separately from the paired tick/draw timeline; these
     * unmatched draws are an accuracy failure, not invisible warm-up work. */
    put("{\"schema\":\"melee-web-cpu-preparation-observation\",\"version\":1,"
        "\"phase\":\"after_source_preparation_draw\",\"index\":%zu,", preparation_draws++);
    snapshot(); put("}");
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
