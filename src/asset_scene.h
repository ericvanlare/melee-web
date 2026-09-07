#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void melee_web_asset_clear(void);
int melee_web_asset_open(const void* bytes, uint32_t size);
const char* melee_web_asset_symbol(uint32_t index);
int melee_web_asset_select(uint32_t index);
const char* melee_web_asset_message(void);
// 1: drew an asset; 0: no asset loaded. Unexpected bridge errors clear the asset.
int melee_web_asset_draw(void);
#ifdef __cplusplus
}
#endif
