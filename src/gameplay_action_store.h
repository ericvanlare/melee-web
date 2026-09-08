#ifndef MELEE_WEB_GAMEPLAY_ACTION_STORE_H
#define MELEE_WEB_GAMEPLAY_ACTION_STORE_H
#include "hsd_animation_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
struct Fighter;
struct FigaTree;
typedef struct MeleeWebNativeClip MeleeWebNativeClip;
MeleeWebNativeClip* melee_web_native_clip_create(uint32_t type, uint32_t flags, float frames,
    const uint8_t* nodes, size_t node_count, const MeleeWebAnimationTrack* tracks, size_t track_count);
void melee_web_native_clip_destroy(MeleeWebNativeClip*);
struct FigaTree* melee_web_native_clip_tree(MeleeWebNativeClip*);
/* Callback owns both active slots until unbind. A false result is fatal to the
 * original void loader; failure never masquerades as an empty source clip. */
typedef int (*MeleeWebActionSelect)(void* context, int motion, unsigned slot,
    struct FigaTree** tree, void** identity, char* error, size_t error_size);
int melee_web_action_bind(struct Fighter*, void* context, MeleeWebActionSelect);
void melee_web_action_unbind(struct Fighter*);
void melee_web_action_load(struct Fighter* destination, struct Fighter* source, int motion, unsigned slot);
/* Canonical instruction words are decoded into original native named fields.
 * target is the native word index of a checked branch; UINT32_MAX otherwise. */
typedef struct MeleeWebCommandWord { uint32_t word, target; } MeleeWebCommandWord;
void* melee_web_commands_create(const MeleeWebCommandWord*, size_t count);
uint32_t melee_web_command_original_word(const void* command);
int melee_web_command_original_word_checked(const void* command, uint32_t* word);
void melee_web_commands_destroy(void*);
void* melee_web_commands_at(void*, size_t index);
void* melee_web_commands_unsupported(void);
void melee_web_command_require_supported(uint32_t opcode);
typedef struct MeleeWebActionRow {
    const char* symbol;
    uint32_t offset, size, flags;
    void* commands;
    uint8_t blend[2];
} MeleeWebActionRow;
typedef struct MeleeWebWaitChoice { uint32_t motion, weight; } MeleeWebWaitChoice;
typedef struct MeleeWebNativeActionRows MeleeWebNativeActionRows;
MeleeWebNativeActionRows* melee_web_action_rows_create(const MeleeWebActionRow*, size_t count,
    const MeleeWebWaitChoice*, size_t wait_count);
void melee_web_action_rows_destroy(MeleeWebNativeActionRows*);
void* melee_web_action_rows(MeleeWebNativeActionRows*);
void* melee_web_action_identity(MeleeWebNativeActionRows*, size_t motion);
void* melee_web_action_blends(MeleeWebNativeActionRows*);
void* melee_web_action_waits(MeleeWebNativeActionRows*);
#ifdef __cplusplus
}
#endif
#endif
