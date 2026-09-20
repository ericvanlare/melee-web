#ifndef MELEE_WEB_GAMEPLAY_PRIZE_CONTEXT_H
#define MELEE_WEB_GAMEPLAY_PRIZE_CONTEXT_H

#include "gameplay_compat.h"
#include "gameplay_audio.h"
#include "gameplay_pad_state.h"
#include <dolphin/pad.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebMenuHost MeleeWebMenuHost;
typedef struct MeleeWebPrizeContext MeleeWebPrizeContext;

/* The prepared Prize world owns its assets, font and audio until end(). Mode
 * OnEnter and Scene OnEnter run in that world's heap, retaining the original
 * HSD-allocated notification list through every confirmation. */
MeleeWebPrizeContext* melee_web_prize_context_begin(MeleeWebMenuHost*,
    uint32_t seed, const MeleeWebPadState*, MeleeWebAudio*, char*, size_t);
int melee_web_prize_context_tick(MeleeWebPrizeContext*, const PADStatus[4], char*, size_t);
int melee_web_prize_context_draw(MeleeWebPrizeContext*, char*, size_t);
int melee_web_prize_context_requested(const MeleeWebPrizeContext*);
uint32_t melee_web_prize_context_random_seed(const MeleeWebPrizeContext*);
uint32_t melee_web_prize_context_ticks(const MeleeWebPrizeContext*);
int melee_web_prize_context_exit(MeleeWebPrizeContext*, char*, size_t);
int melee_web_prize_context_exit_ready(void);
int melee_web_prize_context_end(MeleeWebPrizeContext*, char*, size_t);
#ifdef __cplusplus
}
#endif
#endif
