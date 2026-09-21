#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Versioned semantic wire data: 30 configuration bytes, then master/copy/game
 * histories, four ports each, 66 bytes per port. No pointers or C padding. */
enum { MELEE_WEB_PAD_STATE_BYTES = 30 + 3 * 4 * 66 };
typedef struct MeleeWebPadState MeleeWebPadState;
MeleeWebPadState* melee_web_pad_state_decode(const uint8_t*,size_t,char*,size_t);
void melee_web_pad_state_free(MeleeWebPadState*);
void melee_web_pad_state_capture(uint8_t out[MELEE_WEB_PAD_STATE_BYTES]);
/* Internal initialization adapter; callers must own an unstepped scene and
 * establish its queue/rumble lifetime before applying semantic history. */
void melee_web_pad_state_apply(const MeleeWebPadState*);
#ifdef __cplusplus
}
#endif
