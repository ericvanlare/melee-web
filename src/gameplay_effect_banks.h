#ifndef MELEE_WEB_GAMEPLAY_EFFECT_BANKS_H
#define MELEE_WEB_GAMEPLAY_EFFECT_BANKS_H
#include "native_dat.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebEffectBank MeleeWebEffectBank;
typedef struct MeleeWebEffectBankStats {
    uint32_t bank, first_command, command_count, texture_groups, images, palettes;
    uint32_t particle_bank_ready, effect_entries_ready;
} MeleeWebEffectBankStats;
/* Owner allocations belong to the supplied native DAT arena. Stream bytes are
 * retained but not interpreted by this registration boundary. No EF model
 * entries are published; registration does not establish particle execution. */
MeleeWebEffectBank* melee_web_effect_bank_decode(const MeleeWebNativeDat*, uint32_t table_root,
    uint32_t command_bytes, uint32_t texture_bytes, uint32_t bank);
/* Separate actual map_ptcl/map_texg public roots share the same checked decoder. */
MeleeWebEffectBank* melee_web_effect_bank_decode_roots(const MeleeWebNativeDat*,uint32_t commands,uint32_t textures,
    uint32_t command_bytes,uint32_t texture_bytes,uint32_t bank);
void* melee_web_effect_bank_commands(MeleeWebEffectBank*);
void* melee_web_effect_bank_textures(MeleeWebEffectBank*);
int melee_web_effect_bank_attach(MeleeWebEffectBank*, char*, size_t);
int melee_web_effect_bank_detach(MeleeWebEffectBank*, char*, size_t);
int melee_web_effect_bank_stats(const MeleeWebEffectBank*, MeleeWebEffectBankStats*, char*, size_t);
#ifdef __cplusplus
}
#endif
#endif
