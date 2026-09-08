#include "gameplay_action_store.h"
#include "gameplay_compat.h"
#include <melee/ft/types.h>
#include <melee/lb/lbanim.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
struct MeleeWebNativeClip { FigaTree tree; };
MeleeWebNativeClip* melee_web_native_clip_create(uint32_t type, uint32_t flags, float frames,
    const uint8_t* nodes, size_t node_count, const MeleeWebAnimationTrack* tracks, size_t track_count)
{
    if (!nodes || !tracks || !node_count || node_count > 140 || !track_count || track_count > 1260 ||
        type > 1 || (flags & ~0x30000000U) || !isfinite(frames) || frames <= 0) return NULL;
    size_t sum = 0;
    for (size_t i = 0; i < node_count; ++i) { if (nodes[i] > 9) return NULL; sum += nodes[i]; }
    if (sum != track_count) return NULL;
    for (size_t i = 0; i < track_count; ++i)
        if (tracks[i].length > UINT16_MAX || !melee_web_animation_validate_track(&tracks[i], NULL, 0)) return NULL;
    MeleeWebNativeClip* clip = calloc(1, sizeof(*clip));
    if (!clip) return NULL;
    clip->tree = (FigaTree){type, flags, frames, malloc(node_count + 1), calloc(track_count, sizeof(FigaTrack))};
    if (!clip->tree.nodes || !clip->tree.tracks) { melee_web_native_clip_destroy(clip); return NULL; }
    memcpy(clip->tree.nodes, nodes, node_count); clip->tree.nodes[node_count] = -1;
    for (size_t i = 0; i < track_count; ++i) {
        const MeleeWebAnimationTrack* t = &tracks[i];
        clip->tree.tracks[i] = (FigaTrack){t->length, t->start_frame, t->type, t->value_format, t->slope_format, NULL};
        /* Streams remain owned by the retained DatAnimation selection. */
        clip->tree.tracks[i].ad_head = (uint8_t*)t->bytes;
    }
    return clip;
}
void melee_web_native_clip_destroy(MeleeWebNativeClip* p)
{ if (p) { free(p->tree.nodes); free(p->tree.tracks); free(p); } }
FigaTree* melee_web_native_clip_tree(MeleeWebNativeClip* p) { return p ? &p->tree : NULL; }
static struct { Fighter* fighter; void* context; MeleeWebActionSelect select; } bindings[6];
int melee_web_action_bind(Fighter* fighter, void* context, MeleeWebActionSelect select)
{
    if (!fighter || !context || !select) return 0;
    for (unsigned i = 0; i < 6; ++i) if (bindings[i].fighter == fighter) return 0;
    for (unsigned i = 0; i < 6; ++i) if (!bindings[i].fighter) {
        bindings[i].fighter = fighter; bindings[i].context = context; bindings[i].select = select; return 1;
    }
    return 0;
}
void melee_web_action_unbind(Fighter* fighter)
{
    for (unsigned i = 0; i < 6; ++i) if (bindings[i].fighter == fighter) {
        memset(&bindings[i], 0, sizeof(bindings[i]));
        fighter->x590 = fighter->x598 = NULL; fighter->x5A4 = fighter->x5A8 = NULL;
    }
}
void melee_web_action_load(Fighter* destination, Fighter* source, int motion, unsigned slot)
{
    char error[256] = "No bound owned fighter action store";
    if (destination && source && slot < 2) {
        for (unsigned i = 0; i < 6; ++i) if (bindings[i].fighter == source) {
            /* Cross-fighter slots need destination-owned retention, not borrowed cache pointers. */
            if (destination != source) { snprintf(error, sizeof(error), "Cross-fighter action loading is unsupported"); break; }
            FigaTree* tree = NULL; void* identity = NULL;
            if (!bindings[i].select(bindings[i].context, motion, slot, &tree, &identity, error, sizeof(error))) break;
            if (slot) { destination->x598 = tree; destination->x5A8 = identity; }
            else { destination->x590 = tree; destination->x5A4 = identity; }
            return;
        }
    }
    fprintf(stderr, "Owned action load failed: %s\n", error); abort();
}
static int signed_bits(uint32_t v, unsigned bits)
{ const uint32_t sign = 1U << (bits - 1); return (int)(v ^ sign) - (int)sign; }
void* melee_web_commands_create(const MeleeWebCommandWord* words, size_t count)
{
    if (!words || !count || count > 8192) return NULL;
    union CmdUnion* result = calloc(count, sizeof(*result));
    if (!result) return NULL;
    for (size_t i = 0; i < count; ++i) {
        uint32_t word = words[i].word, op = word >> 26;
        switch (op) {
        case 0: case 1: case 2: case 6: case 8:
            result[i].Command_00.code = op; result[i].Command_00.value = word & 0x3ffffff; break;
        case 5: case 7:
            if (i + 1 >= count || words[i].target >= count) goto fail;
            result[i].Command_00.code = op;
            result[i + 1].Command_05.ptr = &result[words[i].target]; ++i; break;
        case 40:
            result[i].set_tex_anim.opcode = op;
            result[i].set_tex_anim.b = (word >> 25) & 1;
            result[i].set_tex_anim.idx = signed_bits((word >> 18) & 127, 7);
            result[i].set_tex_anim.idx2 = signed_bits((word >> 11) & 127, 7);
            result[i].set_tex_anim.frame = signed_bits(word & 2047, 11); break;
        case 52:
            result[i].unk19.unk0 = op; result[i].unk19.unk1 = word & 0x3ffffff; break;
        default: goto fail;
        }
    }
    return result;
fail: free(result); return NULL;
}
void melee_web_commands_destroy(void* p) { free(p); }
void* melee_web_commands_at(void* p, size_t i) { return &((union CmdUnion*)p)[i]; }
void* melee_web_commands_unsupported(void)
{ static union CmdUnion unsupported; unsupported.Command_00.code = 63; return &unsupported; }
struct MeleeWebNativeActionRows { struct Fighter_WaitAnimData* rows; uint8_t* blends; MeleeWebWaitChoice* waits; };
MeleeWebNativeActionRows* melee_web_action_rows_create(const MeleeWebActionRow* rows, size_t count,
    const MeleeWebWaitChoice* waits, size_t wait_count)
{
    if (!rows || !count || count > 1024 || !waits || !wait_count || wait_count > count) return NULL;
    MeleeWebNativeActionRows* p = calloc(1, sizeof(*p)); if (!p) return NULL;
    p->rows = calloc(count, sizeof(*p->rows)); p->blends = malloc(count * 2);
    p->waits = calloc(wait_count + 1, sizeof(*p->waits));
    if (!p->rows || !p->blends || !p->waits) { melee_web_action_rows_destroy(p); return NULL; }
    for (size_t i = 0; i < count; ++i) {
        p->rows[i] = (struct Fighter_WaitAnimData){(char*)rows[i].symbol, rows[i].offset, rows[i].size,
            rows[i].commands, rows[i].flags, rows[i].size ? (uint32_t)(uintptr_t)&p->rows[i] : 0};
        /* Match source archive identity across distinct motion rows. */
        for (size_t j = 0; j < i && rows[i].size; ++j)
            if (rows[j].offset == rows[i].offset && rows[j].size == rows[i].size &&
                strcmp(rows[j].symbol, rows[i].symbol) == 0) { p->rows[i].x14 = p->rows[j].x14; break; }
        memcpy(p->blends + i * 2, rows[i].blend, 2);
    }
    memcpy(p->waits, waits, wait_count * sizeof(*waits)); p->waits[wait_count] = (MeleeWebWaitChoice){UINT32_MAX, UINT32_MAX};
    return p;
}
void melee_web_action_rows_destroy(MeleeWebNativeActionRows* p)
{ if (p) { free(p->rows); free(p->blends); free(p->waits); free(p); } }
void* melee_web_action_rows(MeleeWebNativeActionRows* p) { return p->rows; }
void* melee_web_action_identity(MeleeWebNativeActionRows* p, size_t motion)
{ return (void*)(uintptr_t)p->rows[motion].x14; }
void* melee_web_action_blends(MeleeWebNativeActionRows* p) { return p->blends; }
void* melee_web_action_waits(MeleeWebNativeActionRows* p) { return p->waits; }

void melee_web_command_require_supported(uint32_t opcode)
{
    switch (opcode) {
    case 0: case 1: case 2: case 5: case 6: case 7: case 8: case 40: case 52: return;
    default: fprintf(stderr, "Unsupported native fighter command opcode %u\n", opcode); abort();
    }
}
