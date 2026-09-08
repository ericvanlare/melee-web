#include "gameplay_audio_itd.h"
#include <assert.h>
uint16_t melee_web_audio_itd_step(uint16_t shift,uint16_t target){assert(shift<32&&target<32);return shift<target?shift+1:shift>target?shift-1:shift;}
unsigned melee_web_audio_itd_delay(uint16_t shift){assert(shift<32);return 32-shift;}
