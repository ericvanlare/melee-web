#ifndef MELEE_WEB_GAMEPLAY_STAGE_STADIUM_H
#define MELEE_WEB_GAMEPLAY_STAGE_STADIUM_H

#include "native_dat.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Exact scalar prefix consumed by the pinned grPStadium.c
 * grPStadium_YakumonoParam. The public symbol's 0x70-byte interval is not the
 * native object size: the source struct ends at 0x54, where another authored
 * object begins. Names retain source offsets because their consumers do not
 * establish more specific meanings.
 *
 * The two explicit padding members make the native ABI offsets assertable;
 * the decoder writes them as zero and never reads source padding bytes.
 */
typedef struct MeleeWebStadiumYakumono {
    int32_t x0;
    int32_t x4;
    int32_t x8;
    int32_t xC;
    int32_t x10;
    int32_t x14;
    int32_t x18;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t _rgb_padding;
    uint32_t x20;
    uint32_t x24;
    uint32_t x28;
    uint32_t x2C;
    uint32_t x30;
    uint32_t x34;
    uint32_t x38;
    uint32_t x3C;
    uint32_t x40;
    uint32_t x44;
    int16_t x48;
    int16_t x4A;
    int16_t x4C;
    int16_t x4E;
    int16_t x50;
    uint16_t _final_padding;
} MeleeWebStadiumYakumono;

/* Decode the source scalar ABI into arena-owned native storage. */
void* melee_web_stadium_yakumono_decode(const MeleeWebNativeDat*, uint32_t root);

#ifdef __cplusplus
}
#endif

#endif
