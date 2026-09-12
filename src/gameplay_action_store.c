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
struct MeleeWebActionBinding {
    Fighter* fighter;
    void* context;
    MeleeWebActionSelect select;
    MeleeWebActionTransfer transfer;
};
static struct MeleeWebActionBinding bindings[6];
int melee_web_action_bind(Fighter* fighter, void* context, MeleeWebActionSelect select,
    MeleeWebActionTransfer transfer)
{
    if (!fighter || !context || !select || !transfer) return 0;
    for (unsigned i = 0; i < 6; ++i) if (bindings[i].fighter == fighter) return 0;
    for (unsigned i = 0; i < 6; ++i) if (!bindings[i].fighter) {
        bindings[i].fighter = fighter; bindings[i].context = context;
        bindings[i].select = select; bindings[i].transfer = transfer; return 1;
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
        const struct MeleeWebActionBinding *source_binding = NULL;
        const struct MeleeWebActionBinding *destination_binding = NULL;
        for (unsigned i = 0; i < 6; ++i) {
            if (bindings[i].fighter == source) source_binding = &bindings[i];
            if (bindings[i].fighter == destination) destination_binding = &bindings[i];
        }
        if (!source_binding) {
            snprintf(error, sizeof(error), "Source fighter has no bound owned action store");
        } else if (!destination_binding) {
            snprintf(error, sizeof(error), "Destination fighter has no bound owned action store");
        } else {
            FigaTree* tree = NULL; void* identity = NULL;
            int ok;
            if (destination == source) {
                ok = source_binding->select(source_binding->context, motion, slot,
                    &tree, &identity, error, sizeof(error));
            } else {
                /* The destination callback retains source streams in the destination store. */
                ok = destination_binding->transfer(destination_binding->context, source_binding->context,
                    destination, source, motion, slot, &tree, &identity, error, sizeof(error));
            }
            if (ok) {
                if (slot) { destination->x598 = tree; destination->x5A8 = identity; }
                else { destination->x590 = tree; destination->x5A4 = identity; }
                return;
            }
        }
    }
    fprintf(stderr, "Owned action load failed: %s\n", error); abort();
}
struct NativeCommandAllocation {
    union CmdUnion* native;
    uint32_t* canonical;
    size_t count;
    struct NativeCommandAllocation* next;
};
static struct NativeCommandAllocation* command_allocations;
int melee_web_command_original_word_checked(const void* command,uint32_t* word)
{
    if(!command||!word)return 0;
    const uintptr_t address=(uintptr_t)command;
    for(struct NativeCommandAllocation* p=command_allocations;p;p=p->next){
        const uintptr_t begin=(uintptr_t)p->native;
        if(address>=begin&&address-begin<p->count*sizeof(*p->native)&&
           (address-begin)%sizeof(*p->native)==0){
            *word=p->canonical[(address-begin)/sizeof(*p->native)];return 1;
        }
    }
    return 0;
}
uint32_t melee_web_command_original_word(const void* command)
{
    uint32_t word;
    if(melee_web_command_original_word_checked(command,&word))return word;
    fprintf(stderr,"Original command word lookup is outside a live native command allocation\n");abort();
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
        case 0: case 1: case 2: case 3: case 4: case 6: case 8:
            result[i].Command_00.code = op; result[i].Command_00.value = word & 0x3ffffff; break;
        case 5: case 7:
            if (i + 1 >= count || words[i].target >= count) goto fail;
            result[i].Command_00.code = op;
            result[i + 1].Command_05.ptr = &result[words[i].target]; ++i; break;
        case 12:
            if (((word >> 23) & 7) >= 4) goto fail;
            result[i].set_hitbox_damage = (struct set_hitbox_damage){op, (word >> 23) & 7, word & 0x7fffff}; break;
        case 14:
            result[i].set_hitbox_x42_b57 = (struct set_hitbox_x42_b57){op,
                (word >> 2) & 0xffffff, (word >> 1) & 1, word & 1}; break;
        case 15:
            result[i].set_throw_flags = (struct set_throw_flags){op,word & 0x3ffffff}; break;
        case 20:
            result[i].set_throw_flags = (struct set_throw_flags){op,word & 0x3ffffff}; break;
        case 21: case 22: case 23: case 24:
            result[i].Command_00 = (struct Command_00){op,word & 0x3ffffff}; break;
        case 25: case 26: case 27:
            result[i].set_airborne_state = (struct set_airborne_state){op,word & 0x3ffffff}; break;
        case 28:
            result[i].set_hurt_state = (struct set_hurt_state){op,(word >> 18) & 255,word & 0x3ffff}; break;
        case 34:
            if(i+2>=count || ((word >> 23) & 7)>=2)goto fail;
            result[i].set_throw_hitbox_0 = (struct set_throw_hitbox_0){op,(word >> 23) & 7,word & 0x7fffff};
            uint32_t throw1=words[i+1].word,throw2=words[i+2].word;
            result[i+1].set_throw_hitbox_1 = (struct set_throw_hitbox_1){throw1 >> 23,(throw1 >> 14) & 511,(throw1 >> 5) & 511};
            result[i+2].set_throw_hitbox_2 = (struct set_throw_hitbox_2){throw2 >> 23,(throw2 >> 19) & 15,(throw2 >> 16) & 7,(throw2 >> 12) & 15};
            i+=2;break;
        case 35:
            result[i].unk27 = (struct unk27){op,word & 0x3ffffff};break;
        case 30:
            result[i].set_jab_rapid = (struct set_jab_rapid){op,word & 0x3ffffff}; break;
        case 31:
            result[i].set_dobj_flags = (struct set_dobj_flags){op,
                signed_bits((word >> 19) & 127, 7), signed_bits(word & 0x7ffff, 19)}; break;
        case 36:
            result[i].set_article_vis = (struct set_article_vis){op,word & 0x3ffffff}; break;
        case 37:
            result[i].set_fighter_vis = (struct set_fighter_vis){op,word & 0x3ffffff}; break;
        case 38:
            if (i + 6 >= count) goto fail;
            result[i].pseudo_random_sfx_0 = (struct pseudo_random_sfx_0){op,
                (word >> 18) & 255, (word >> 10) & 255, (word >> 6) & 15, word & 63};
            for (size_t j = 1; j <= 6; ++j)
                result[i + j].pseudo_random_sfx_1.sfx_id = words[i + j].word;
            i += 6; break;
        case 41:
            result[i].part_anim = (struct part_anim){op,
                signed_bits((word >> 19) & 127, 7), signed_bits((word >> 12) & 127, 7), word & 4095}; break;
        case 42:
            result[i].unk9 = (struct unk9){op,(word >> 13) & 8191,word & 8191}; break;
        case 56:
            if(i+1>=count)goto fail;
            result[i].smash_charge_0 = (struct smash_charge_0){op,(word >> 16) & 1023,word & 65535};
            result[i+1].smash_charge_1 = (struct smash_charge_1){words[i+1].word >> 24,words[i+1].word & 0xffffff};
            ++i;break;
        case 11:
            if (i + 5 >= count || ((word >> 23) & 7) >= 4) goto fail;
            result[i].create_hitbox_0 = (struct spawn_hitbox_0){op, (word >> 23) & 7,
                (word >> 20) & 7, (word >> 19) & 1, (word >> 11) & 255,
                (word >> 10) & 1, word & 1023};
            result[i+1].create_hitbox_1 = (struct spawn_hitbox_1){words[i+1].word >> 16, signed_bits(words[i+1].word & 65535,16)};
            result[i+2].create_hitbox_2 = (struct spawn_hitbox_2){signed_bits(words[i+2].word >> 16,16), signed_bits(words[i+2].word & 65535,16)};
            uint32_t hit3=words[i+3].word,hit4=words[i+4].word;
            result[i+3].create_hitbox_3 = (struct spawn_hitbox_3){hit3 >> 23,
                (hit3 >> 14) & 511, (hit3 >> 5) & 511, (hit3 >> 4) & 1,
                (hit3 >> 3) & 1, (hit3 >> 2) & 1, (hit3 >> 1) & 1, hit3 & 1};
            result[i+4].create_hitbox_4 = (struct spawn_hitbox_4){hit4 >> 23,
                (hit4 >> 18) & 31, signed_bits((hit4 >> 10) & 255,8),
                (hit4 >> 7) & 7, (hit4 >> 2) & 31, (hit4 >> 1) & 1, hit4 & 1};
            i += 4; break;
        case 13:
            if (((word >> 23) & 7) >= 4) goto fail;
            result[i].set_hitbox_scale = (struct set_hitbox_scale){op, (word >> 23) & 7, word & 0x7fffff}; break;
        case 16: case 18:
            result[i].Command_00 = (struct Command_00){op,word & 0x3ffffff}; break;
        case 29:
            result[i].set_jab_combo = (struct set_jab_combo){op,word & 0x3ffffff}; break;
        case 10:
            if (i + 4 >= count) goto fail;
            result[i].spawn_gfx_0 = (struct spawn_gfx_0){op, (word >> 18) & 255,
                (word >> 17) & 1, (word >> 16) & 1, (word >> 15) & 1, word & 32767};
            result[i+1].spawn_gfx_1 = (struct spawn_gfx_1){words[i+1].word >> 16, words[i+1].word & 65535};
            result[i+2].spawn_gfx_2 = (struct spawn_gfx_2){signed_bits(words[i+2].word >> 16,16), signed_bits(words[i+2].word & 65535,16)};
            result[i+3].spawn_gfx_3 = (struct spawn_gfx_3){signed_bits(words[i+3].word >> 16,16), words[i+3].word & 65535};
            result[i+4].spawn_gfx_4 = (struct spawn_gfx_4){words[i+4].word >> 16, words[i+4].word & 65535};
            i += 4; break;
        case 17:
            if (i + 2 >= count) goto fail;
            result[i].sound_effect_0 = (struct sound_effect_0){op, (word >> 18) & 255, word & 0x3ffff};
            result[i+1].sound_effect_1.sfx_id = words[i+1].word;
            result[i+2].sound_effect_2 = (struct sound_effect_2){words[i+2].word >> 16,
                (words[i+2].word >> 8) & 255, words[i+2].word & 255};
            i += 2; break;
        case 19:
            result[i].set_cmd_var = (struct set_cmd_var){op, (word >> 24) & 3, word & 0xffffff}; break;
        case 43:
            result[i].unk10 = (struct unk10){op, (word >> 25) & 1, (word >> 13) & 4095, word & 8191}; break;
        case 46:
            result[i].unk13 = (struct unk13){op, (word >> 18) & 255, word & 0x3ffff}; break;
        case 40:
            result[i].set_tex_anim.opcode = op;
            result[i].set_tex_anim.b = (word >> 25) & 1;
            result[i].set_tex_anim.idx = signed_bits((word >> 18) & 127, 7);
            result[i].set_tex_anim.idx2 = signed_bits((word >> 11) & 127, 7);
            result[i].set_tex_anim.frame = signed_bits(word & 2047, 11); break;
        case 52:
            result[i].unk19.unk0 = op; result[i].unk19.unk1 = word & 0x3ffffff; break;
        case 49:
            result[i].unk16 = (struct unk16){op,
                signed_bits((word >> 25) & 1, 1), signed_bits(word & 0x1ffffff, 25)}; break;
        case 32: case 33: case 44: case 45: case 47: case 48:
            result[i].Command_00 = (struct Command_00){op,word & 0x3ffffff}; break;
        case 50:
            result[i].unk17 = (struct unk17){op,signed_bits(word & 0x3ffffff, 26)}; break;
        case 51:
            result[i].unk18 = (struct unk18){op,signed_bits(word & 0x3ffffff, 26)}; break;
        case 53:
            result[i].unk20 = (struct unk20){op,word & 0x3ffffff}; break;
        case 57:
            result[i].unk21 = (struct unk21){op,(word >> 25) & 1,(word >> 17) & 255}; break;
        case 39:
            if (i + 3 >= count) goto fail;
            result[i].stage_sfx_0 = (struct stage_sfx_0){op,(word >> 16) & 1023,
                (word >> 8) & 255,word & 255};
            result[i + 1].stage_sfx_1.sfx_id = words[i + 1].word;
            result[i + 2].stage_sfx_2 = (struct stage_sfx_2){words[i + 2].word >> 16, words[i + 2].word & 0xffff};
            result[i + 3].stage_sfx_3 = (struct stage_sfx_3){words[i + 3].word >> 16,
                (words[i + 3].word >> 8) & 255, words[i + 3].word & 255};
            i += 3; break;
        case 54:
            if (i + 2 >= count) goto fail;
            result[i].footstep_fx_0 = (struct footstep_fx_0){op,
                (word >> 18) & 255, (word >> 17) & 1, (word >> 16) & 1,
                (word >> 8) & 255, word & 255};
            result[i + 1].sound_effect_1.sfx_id = words[i + 1].word;
            result[i + 2].sound_effect_2 = (struct sound_effect_2){words[i + 2].word >> 16,
                (words[i + 2].word >> 8) & 255, words[i + 2].word & 255};
            i += 2; break;
        case 58:
            if (i + 3 >= count) goto fail;
            result[i].wind_fx_0 = (struct wind_fx_0){op,(word >> 8) & 0x3ffff,word & 255};
            result[i + 1].wind_fx_1 = (struct wind_fx_1){
                signed_bits(words[i + 1].word >> 16, 16), signed_bits(words[i + 1].word & 0xffff, 16)};
            result[i + 2].wind_fx_2 = (struct wind_fx_2){
                signed_bits(words[i + 2].word >> 16, 16), signed_bits(words[i + 2].word & 0xffff, 16)};
            result[i + 3].wind_fx_3 = (struct wind_fx_3){
                signed_bits(words[i + 3].word >> 16, 16), signed_bits(words[i + 3].word & 0xffff, 16)};
            i += 3; break;
        case 55:
            if (i + 2 >= count) goto fail;
            /* The checked little-endian ftAction_80072E4C adapter reads
             * flag/gfx bits from unknown, then reuses this word through the
             * original sound consumer ftAction_80071B50. Keep its shared
             * native sound representation; unk_fx's native bitfield layout
             * is different despite sharing the same retail command word. */
            result[i].sound_effect_0 = (struct sound_effect_0){op,
                (word >> 18) & 255, word & 0x3ffff};
            result[i + 1].sound_effect_1.sfx_id = words[i + 1].word;
            result[i + 2].sound_effect_2 = (struct sound_effect_2){words[i + 2].word >> 16,
                (words[i + 2].word >> 8) & 255, words[i + 2].word & 255};
            i += 2; break;
        default: goto fail;
        }
    }
    struct NativeCommandAllocation* allocation=calloc(1,sizeof(*allocation));
    if(!allocation)goto fail;
    allocation->canonical=malloc(count*sizeof(*allocation->canonical));
    if(!allocation->canonical){free(allocation);goto fail;}
    for(size_t i=0;i<count;i++)allocation->canonical[i]=words[i].word;
    allocation->native=result;allocation->count=count;allocation->next=command_allocations;
    command_allocations=allocation;
    return result;
fail: free(result); return NULL;
}
void melee_web_commands_destroy(void* native)
{
    if(!native)return;
    struct NativeCommandAllocation** link=&command_allocations;
    while(*link&&(*link)->native!=native)link=&(*link)->next;
    if(!*link){fprintf(stderr,"Native command allocation is not owned\n");abort();}
    struct NativeCommandAllocation* p=*link;*link=p->next;
    free(p->canonical);free(p->native);free(p);
}
void* melee_web_commands_at(void* p, size_t i) { return &((union CmdUnion*)p)[i]; }
void* melee_web_commands_unsupported(void)
{ static union CmdUnion unsupported; unsupported.Command_00.code = 63; return &unsupported; }
struct MeleeWebNativeActionRows {
    struct Fighter_WaitAnimData* rows;
    char** symbols;
    uint8_t* blends;
    MeleeWebWaitChoice* waits;
};
MeleeWebNativeActionRows* melee_web_action_rows_create(const MeleeWebActionRow* rows, size_t count,
    const MeleeWebWaitChoice* waits, size_t wait_count)
{
    if (!rows || !count || count > 1024 || !waits || !wait_count || wait_count > count) return NULL;
    MeleeWebNativeActionRows* p = calloc(1, sizeof(*p)); if (!p) return NULL;
    p->rows = calloc(count, sizeof(*p->rows)); p->symbols = calloc(count + 1, sizeof(*p->symbols));
    p->blends = malloc(count * 2);
    p->waits = calloc(wait_count + 1, sizeof(*p->waits));
    if (!p->rows || !p->symbols || !p->blends || !p->waits) { melee_web_action_rows_destroy(p); return NULL; }
    for (size_t i = 0; i < count; ++i) {
        if (!rows[i].symbol) { melee_web_action_rows_destroy(p); return NULL; }
        const size_t symbol_size = strlen(rows[i].symbol) + 1;
        p->symbols[i] = malloc(symbol_size);
        if (!p->symbols[i]) { melee_web_action_rows_destroy(p); return NULL; }
        memcpy(p->symbols[i], rows[i].symbol, symbol_size);
        p->rows[i] = (struct Fighter_WaitAnimData){p->symbols[i], rows[i].offset, rows[i].size,
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
{ if (p) {
    if (p->symbols) for (size_t i = 0; p->rows && p->symbols[i]; ++i) free(p->symbols[i]);
    free(p->symbols); free(p->rows); free(p->blends); free(p->waits); free(p);
} }
void* melee_web_action_rows(MeleeWebNativeActionRows* p) { return p->rows; }
void* melee_web_action_identity(MeleeWebNativeActionRows* p, size_t motion)
{ return (void*)(uintptr_t)p->rows[motion].x14; }
void* melee_web_action_blends(MeleeWebNativeActionRows* p) { return p->blends; }
void* melee_web_action_waits(MeleeWebNativeActionRows* p) { return p->waits; }

void melee_web_command_require_supported(uint32_t opcode)
{
    switch (opcode) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 10: case 11: case 13: case 16: case 17: case 18: case 19: case 20: case 23: case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31: case 34: case 35: case 37: case 38: case 40: case 41: case 43: case 46: case 49: case 52: case 54: case 55: case 56: case 58: return;
    default:
        fprintf(stderr, "Unsupported native fighter command opcode %u\n", opcode);
        for (unsigned i = 0; i < 6; ++i) if (bindings[i].fighter)
            fprintf(stderr, "Bound fighter %u: motion %d, animation %d\n", i,
                    bindings[i].fighter->motion_id, bindings[i].fighter->anim_id);
        abort();
    }
}
