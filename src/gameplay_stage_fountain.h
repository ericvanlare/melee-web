#ifndef MELEE_WEB_GAMEPLAY_STAGE_FOUNTAIN_H
#define MELEE_WEB_GAMEPLAY_STAGE_FOUNTAIN_H

#include "native_dat.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Exact scalar payload consumed by the pinned source grIzumi.c
 * grIzumi_YakumonoParam.  The source ABI is a 32-bit float at x0, a signed
 * 32-bit int at x4, and 19 further 32-bit floats through x50.  The DAT public
 * symbol spans 0x70 bytes before the next public root, but the source struct
 * consumes only this first 0x54-byte prefix; the decoder deliberately leaves
 * the remaining archive bytes opaque.
 *
 * Field names retain the source offsets because several values are shared by
 * the platform constructor and the platform state machine, and assigning
 * semantic names here would claim knowledge the source does not provide.
 */
typedef struct MeleeWebFountainYakumono {
    float x0;
    int32_t x4;
    float x8;
    float xC;
    float x10;
    float x14;
    float x18;
    float x1C;
    float x20;
    float x24;
    float x28;
    float x2C;
    float x30;
    float x34;
    float x38;
    float x3C;
    float x40;
    float x44;
    float x48;
    float x4C;
    float x50;
} MeleeWebFountainYakumono;

/* Decode the source scalar ABI into arena-owned native storage. */
void* melee_web_fountain_yakumono_decode(const MeleeWebNativeDat*, uint32_t root);

#ifdef __cplusplus
}
#endif

#endif
