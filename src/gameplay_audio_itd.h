#ifndef MELEE_WEB_AUDIO_ITD_H
#define MELEE_WEB_AUDIO_ITD_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Recovered from original AX DSPCode.c: 0x02FC..0309 and0x0313..0320.
 * Called after each32-sample block. Shift indexes the previous32 samples;
 * a shift of0 therefore means32 samples of delay, and31 means one sample. */
uint16_t melee_web_audio_itd_step(uint16_t shift,uint16_t target);
unsigned melee_web_audio_itd_delay(uint16_t shift);
#ifdef __cplusplus
}
#endif
#endif
