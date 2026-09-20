#ifndef MELEE_WEB_GAMEPLAY_STAGE_OLD_YOSHI_H
#define MELEE_WEB_GAMEPLAY_STAGE_OLD_YOSHI_H

#include "native_dat.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Exact anonymous payload consumed by the pinned grOldYoshi source module.
 * The source declares seven signed 16-bit values around three floats and the
 * compiler supplies two bytes of final padding.  Names retain source offsets;
 * source-consumer descriptions live in the stage port note and are not
 * reinterpreted as a different native schema here.
 */
typedef struct MeleeWebOldYoshiYakumono {
    int16_t x0;
    int16_t x2;
    float x4;
    float x8;
    float xC;
    int16_t x10;
    int16_t x12;
    int16_t x14;
    int16_t x16;
    int16_t x18;
    uint16_t _final_padding;
} MeleeWebOldYoshiYakumono;

/* Decode exactly the source-consumed 28-byte public payload. */
void* melee_web_old_yoshi_yakumono_decode(const MeleeWebNativeDat*,
                                          uint32_t root);

#ifdef __cplusplus
}
#endif

#endif
