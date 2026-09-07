#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void melee_web_asset_clear(void);
int melee_web_asset_ready(void);
int melee_web_asset_kind(void); // 0 none, 1 ordinary, 2 registered fighter, 3 stage entry.
int melee_web_asset_stage_count(void);
int melee_web_asset_stage_select(uint32_t index, int opaque_only);
const char* melee_web_asset_stage_message(void);
int melee_web_asset_common_open(const void* bytes, uint32_t size);
const char* melee_web_asset_common_message(void);
int melee_web_asset_action_count(void);
const char* melee_web_asset_action_name(uint32_t index);
int melee_web_asset_container_ready(void);
int melee_web_asset_container_open(const void* bytes, uint32_t size);
int melee_web_asset_action_select(uint32_t index);
int melee_web_asset_open(const void* bytes, uint32_t size);
const char* melee_web_asset_symbol(uint32_t index);
int melee_web_asset_select(uint32_t index);
const char* melee_web_asset_message(void);
int melee_web_asset_fighter_open(const void* bytes, uint32_t size);
const char* melee_web_asset_fighter_message(void);
int melee_web_asset_animation_open(const void* bytes, uint32_t size);
int melee_web_asset_animation_play(int enabled);
int melee_web_asset_animation_playing(void);
int melee_web_asset_animation_loaded(void);
void melee_web_asset_animation_visible(int visible);
const char* melee_web_asset_animation_message(void);
// Inspection animation advances at fixed 60 Hz independently of presentation.
void melee_web_asset_tick(double now_ms);
// 1: drew an asset; 0: no asset loaded. Unexpected bridge errors clear the asset.
int melee_web_asset_draw(void);
#ifdef __cplusplus
}
#endif
