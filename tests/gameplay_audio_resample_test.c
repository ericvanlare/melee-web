#include "gameplay_audio_resample.h"
#include "gameplay_audio_itd.h"
#include <assert.h>
#include <stdio.h>
static int16_t read(void* p){int16_t* value=p;return (*value)++;}
int main(void){
 int16_t coefficients[512]={0};for(unsigned i=0;i<128;i++)coefficients[i*4+2]=32767;
 MeleeWebAudioResample state={{1,2,3,4},0};int16_t sample=100;
 assert(melee_web_audio_resample(&state,65536,0,coefficients,read,&sample)==3);
 assert(sample==101&&state.history[3]==100&&state.fraction==0);
 assert(melee_web_audio_resample(&state,32768,0,coefficients,read,&sample)==3);
 assert(sample==101&&state.fraction==32768);
 assert(melee_web_audio_resample(&state,32768,0,coefficients,read,&sample)==99);
 state=(MeleeWebAudioResample){{-32768,-32768,-32768,-32768},0};for(unsigned i=0;i<4;i++)coefficients[i]=32767;
 assert(melee_web_audio_resample(&state,0,0,coefficients,read,&sample)==-32768);
 state=(MeleeWebAudioResample){{-3,0,4,8},0};assert(melee_web_audio_resample(&state,32768,1,0,read,&sample)==-2);
 state=(MeleeWebAudioResample){{1,2,3,4},0};sample=200;assert(melee_web_audio_resample(&state,65536,2,0,read,&sample)==200);assert(state.history[0]==2&&state.history[3]==200);
 uint16_t shift=0;for(unsigned i=0;i<31;i++){assert(melee_web_audio_itd_delay(shift)==32-i);shift=melee_web_audio_itd_step(shift,31);}assert(shift==31&&melee_web_audio_itd_step(shift,31)==31);for(unsigned i=0;i<31;i++)shift=melee_web_audio_itd_step(shift,0);assert(shift==0);
 puts("Four-tap phase/history/saturation, signed linear rounding and nearest passed");
}
