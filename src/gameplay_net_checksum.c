/* Opt-in per-tick state checksums for networked-session determinism checks.
 *
 * Reads only. The field list for fighters is the retail state printer's
 * visitor; menus hash the session-owned CSS/SSS selection payloads and the
 * live CSS cursors through the existing read-only observer. Every GObj in the
 * current world contributes its link identity and, for a JObj, its root
 * transform. Pointers, matrix caches and dirty flags never enter a hash:
 * absolute Wasm addresses and draw-time matrix setup are host properties. */
#include "gameplay_compat.h"
#include "gameplay_net_checksum.h"
#include "gameplay_bootstrap.h"
#include "gameplay_menu.h"
#include "gameplay_menu_host.h"
#include "gameplay_retail_state.h"
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/random.h>
#include <string.h>

extern u32 gm_GetFrameCount(void);
/* Read-only CSS observer from the menu source patch. */
extern int melee_web_css_observe_port(unsigned port, int ckind, int ids[14],
                                      float geometry[8]);

#define FNV_OFFSET 0xcbf29ce484222325ull
#define FNV_PRIME 0x100000001b3ull

uint64_t melee_web_net_fnv1a64(uint64_t hash, const void* bytes, size_t size)
{
    const uint8_t* p = bytes;
    for (size_t i = 0; i < size; ++i) {
        hash ^= p[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

typedef struct Hasher {
    uint64_t total;
    uint64_t component;
    /* The object channel is supplementary: it never enters `total`. */
    int in_total;
} Hasher;

static void feed(Hasher* h, const void* bytes, size_t size)
{
    if (h->in_total) h->total = melee_web_net_fnv1a64(h->total, bytes, size);
    h->component = melee_web_net_fnv1a64(h->component, bytes, size);
}
static void feed_u8(Hasher* h, uint8_t value) { feed(h, &value, 1); }
static void feed_u16(Hasher* h, uint16_t value)
{
    const uint8_t b[2] = {(uint8_t) value, (uint8_t) (value >> 8)};
    feed(h, b, 2);
}
static void feed_u32(Hasher* h, uint32_t value)
{
    const uint8_t b[4] = {(uint8_t) value, (uint8_t) (value >> 8),
                          (uint8_t) (value >> 16), (uint8_t) (value >> 24)};
    feed(h, b, 4);
}
static void feed_u64(Hasher* h, uint64_t value)
{
    feed_u32(h, (uint32_t) value);
    feed_u32(h, (uint32_t) (value >> 32));
}
static uint32_t float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}
static void feed_float(Hasher* h, float value) { feed_u32(h, float_bits(value)); }
static void begin_component(Hasher* h, int in_total)
{
    h->component = FNV_OFFSET;
    h->in_total = in_total;
}

/* Recipe wire order: u16 BE buttons, four signed sticks, two triggers,
 * analog A/B, error. The bytes match one MWRC/MWNI frame port record. */
static void feed_pad(Hasher* h, const PADStatus* pad)
{
    const uint8_t record[11] = {
        (uint8_t) (pad->button >> 8), (uint8_t) pad->button,
        (uint8_t) pad->stickX, (uint8_t) pad->stickY,
        (uint8_t) pad->substickX, (uint8_t) pad->substickY,
        pad->triggerLeft, pad->triggerRight, pad->analogA, pad->analogB,
        (uint8_t) pad->err};
    feed(h, record, sizeof(record));
}

static void feed_master_status(Hasher* h, const HSD_PadStatus* s)
{
    feed_u32(h, s->button);
    feed_u32(h, s->last_button);
    feed_u32(h, s->trigger);
    feed_u32(h, s->repeat);
    feed_u32(h, s->release);
    feed_u32(h, (uint32_t) s->repeat_count);
    feed_u8(h, (uint8_t) s->stickX);
    feed_u8(h, (uint8_t) s->stickY);
    feed_u8(h, (uint8_t) s->subStickX);
    feed_u8(h, (uint8_t) s->subStickY);
    feed_u8(h, s->analogL);
    feed_u8(h, s->analogR);
    feed_u8(h, s->analogA);
    feed_u8(h, s->analogB);
    feed_float(h, s->nml_stickX);
    feed_float(h, s->nml_stickY);
    feed_float(h, s->nml_subStickX);
    feed_float(h, s->nml_subStickY);
    feed_float(h, s->nml_analogL);
    feed_float(h, s->nml_analogR);
    feed_float(h, s->nml_analogA);
    feed_float(h, s->nml_analogB);
    feed_u8(h, s->cross_dir);
    feed_u8(h, (uint8_t) s->err);
}

/* Fighter visitor adapter: values only, never names. */
static void visit_unsigned(void* c, const char* name, uint32_t v)
{
    (void) name;
    feed_u32(c, v);
}
static void visit_signed(void* c, const char* name, int32_t v)
{
    (void) name;
    feed_u32(c, (uint32_t) v);
}
static void visit_bits(void* c, const char* name, uint32_t v)
{
    (void) name;
    feed_u32(c, v);
}
static void visit_vector(void* c, const char* name, const uint32_t v[3])
{
    (void) name;
    feed_u32(c, v[0]);
    feed_u32(c, v[1]);
    feed_u32(c, v[2]);
}
static void visit_input(void* c, const char* name, const uint32_t words[20],
                        const uint8_t timers[28])
{
    (void) name;
    for (unsigned i = 0; i < 20; ++i) feed_u32(c, words[i]);
    feed(c, timers, 28);
}
static const MeleeWebFighterFieldVisitor fighter_hash = {
    visit_unsigned, visit_signed, visit_bits, visit_vector, visit_input};

static unsigned feed_fighters(Hasher* h)
{
    unsigned count = 0;
    for (unsigned slot = 0; slot < 6; ++slot) {
        const StaticPlayer* player;
        feed_u32(h, (uint32_t) Player_GetPlayerSlotType(slot));
        if (Player_GetPlayerSlotType(slot) == Gm_PKind_NA) continue;
        player = Player_GetPtrForSlot(slot);
        if (!player) continue;
        for (unsigned entity = 0;
             entity < sizeof(player->player_entity) / sizeof(player->player_entity[0]);
             ++entity) {
            const HSD_GObj* gobj = player->player_entity[entity];
            feed_u8(h, gobj != NULL);
            if (!gobj || !gobj->user_data) continue;
            melee_web_fighter_fields_visit(slot, gobj->user_data, &fighter_hash, h);
            ++count;
        }
    }
    return count;
}

static void feed_start(Hasher* h, const StartMeleeData* start)
{
    const StartMeleeRules* r = &start->rules;
    feed_u32(h, r->match_kind);
    feed_u32(h, r->timer_enabled);
    feed_u32(h, r->timer_counts_up);
    feed_u32(h, r->friendly_fire);
    feed_u32(h, r->is_stock);
    feed_u32(h, r->disable_pausing);
    feed_u32(h, r->single_button);
    feed_u32(h, r->is_teams);
    feed_u32(h, (uint32_t) (int32_t) r->xB);
    feed_u32(h, r->stkind);
    feed_u32(h, r->time_limit);
    feed_u64(h, r->x20);
    feed_float(h, r->x2C);
    feed_float(h, r->x30);
    feed_float(h, r->game_speed);
    for (unsigned slot = 0; slot < 6; ++slot) {
        const PlayerInitData* p = &start->players[slot];
        /* Bytes 0x00-0x0F are fully declared scalar/bitfield bytes. */
        feed(h, p, 0x10);
        feed_u16(h, p->x10);
        feed_u16(h, p->x12);
        feed_u16(h, p->hp);
        feed_float(h, p->attack_ratio);
        feed_float(h, p->defense_ratio);
        feed_float(h, p->model_scale);
    }
}

static uint32_t feed_menu(Hasher* h, const MeleeWebMenuHost* host)
{
    StartMeleeData start;
    uint8_t header[6];
    int kind;
    if (!host) return 0;
    kind = melee_web_menu_host_selection_state(host, &start, header);
    feed_u32(h, (uint32_t) kind);
    if (!kind) return 0;
    feed(h, header, sizeof(header));
    feed_start(h, &start);
    if (kind == 1 && melee_web_menu_host_phase(host) == MELEE_WEB_MENU_CSS) {
        /* Only the live CSS scene owns these cursor objects. Mario's icon
         * always exists, so each port's cursor/model state is readable
         * without depending on a selection. */
        for (unsigned port = 0; port < 4; ++port) {
            int ids[14];
            float geometry[8];
            const int valid = melee_web_css_observe_port(port, 8, ids, geometry);
            feed_u8(h, (uint8_t) valid);
            if (!valid) continue;
            for (unsigned i = 0; i < 14; ++i) feed_u32(h, (uint32_t) ids[i]);
            for (unsigned i = 0; i < 8; ++i) feed_float(h, geometry[i]);
        }
    }
    return kind == 1 ? MELEE_WEB_NET_HASHED_CSS : MELEE_WEB_NET_HASHED_SSS;
}

static unsigned feed_objects(Hasher* h)
{
    unsigned count = 0;
    HSD_GObj** lists = (HSD_GObj**) HSD_GObj_Entities;
    if (!lists) return 0;
    for (unsigned link = 0; link <= HSD_GObjLibInitData.p_link_max; ++link) {
        feed_u32(h, link);
        for (const HSD_GObj* gobj = lists[link]; gobj; gobj = gobj->next) {
            feed_u16(h, gobj->classifier);
            feed_u8(h, gobj->p_link);
            feed_u8(h, gobj->gx_link);
            feed_u8(h, gobj->p_priority);
            feed_u8(h, gobj->render_priority);
            feed_u8(h, gobj->obj_kind);
            feed_u8(h, gobj->user_data_kind);
            if (gobj->obj_kind != HSD_GOBJ_OBJ_NONE &&
                gobj->obj_kind == HSD_GObj_JObjKind && gobj->hsd_obj) {
                const HSD_JObj* jobj = gobj->hsd_obj;
                feed_float(h, jobj->rotate.x);
                feed_float(h, jobj->rotate.y);
                feed_float(h, jobj->rotate.z);
                feed_float(h, jobj->rotate.w);
                feed_float(h, jobj->scale.x);
                feed_float(h, jobj->scale.y);
                feed_float(h, jobj->scale.z);
                feed_float(h, jobj->translate.x);
                feed_float(h, jobj->translate.y);
                feed_float(h, jobj->translate.z);
            }
            ++count;
        }
    }
    return count;
}

void melee_web_net_checksum_compute(uint32_t tick, uint32_t scene,
                                    const PADStatus pads[4],
                                    const MeleeWebMenuHost* host,
                                    MeleeWebNetChecksumRecord* out)
{
    Hasher h = {FNV_OFFSET, FNV_OFFSET, 1};
    memset(out, 0, sizeof(*out));
    out->tick = tick;
    out->scene = scene;
    out->seed = seed_ptr ? *seed_ptr : 0;
    out->frame = gm_GetFrameCount();
    feed_u32(&h, out->tick);
    feed_u32(&h, out->scene);
    feed_u8(&h, seed_ptr != NULL);
    feed_u32(&h, out->seed);
    feed_u32(&h, out->frame);

    begin_component(&h, 1);
    for (unsigned port = 0; port < 4; ++port) feed_pad(&h, &pads[port]);
    out->input = h.component;

    begin_component(&h, 1);
    for (unsigned port = 0; port < 4; ++port)
        feed_master_status(&h, &HSD_PadMasterStatus[port]);
    out->pad = h.component;

    begin_component(&h, 1);
    if (scene == 3) {
        if (feed_fighters(&h)) out->flags |= MELEE_WEB_NET_HASHED_FIGHTERS;
    } else {
        out->flags |= feed_menu(&h, host);
    }
    out->scene_state = h.component;

    begin_component(&h, 0);
    out->objects = feed_objects(&h);
    if (out->objects) out->flags |= MELEE_WEB_NET_HASHED_OBJECTS;
    out->object_state = h.component;

    out->total = h.total;
}

int melee_web_net_arena_hash(uint32_t* base, uint32_t* bytes, uint64_t* hash)
{
    const void* arena;
    size_t size;
    uint64_t value = FNV_OFFSET;
    if (!base || !bytes || !hash ||
        !melee_web_gameplay_session_arena(&arena, &size))
        return 0;
    const uint8_t* p = arena;
    size_t i = 0;
    for (; i + 8 <= size; i += 8) {
        uint64_t word;
        memcpy(&word, p + i, 8);
        value ^= word;
        value *= FNV_PRIME;
    }
    value = melee_web_net_fnv1a64(value, p + i, size - i);
    *base = (uint32_t) (uintptr_t) arena;
    *bytes = (uint32_t) size;
    *hash = value;
    return 1;
}
