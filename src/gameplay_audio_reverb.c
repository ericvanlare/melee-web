#include <dolphin/axfx.h>
#include <math.h>
#include <stdint.h>
#include <limits.h>
_Static_assert(sizeof(long)==4,"Original AXFX callbacks use32-bit samples");
/* Scalar translation of original reverb_std.c HandleReverb. Operations follow
 * the PPC single-precision/fused instruction order; the original pre-delay wrap
 * comparison (base + preDelayTime -1) is retained. */
static float delay_read(struct AXFX_REVSTD_DELAYLINE* line){float value=line->inputs[line->outPoint/4];line->outPoint+=4;if(line->outPoint==line->length)line->outPoint=0;return value;}
static void delay_write(struct AXFX_REVSTD_DELAYLINE* line,float value){line->inputs[line->inPoint/4]=value;line->inPoint+=4;if(line->inPoint==line->length)line->inPoint=0;}
static long fctiwz(float value){if(isnan(value)||value<=-2147483648.0f)return INT32_MIN;if(value>=2147483648.0f)return INT32_MAX;return (long)value;}
void melee_web_audio_handle_reverb(long* samples,struct AXFX_REVSTD_WORK* work){
 float wet=work->level*0.6f,dry=0.6f-wet;
 for(unsigned channel=0;channel<3;channel++){
  struct AXFX_REVSTD_DELAYLINE* c0=&work->C[channel*2],*c1=c0+1,*a0=&work->AP[channel*2],*a1=a0+1;
  float lowpass=work->lpLastout[channel],*pre=work->preDelayPtr[channel];
  for(unsigned sample=0;sample<160;sample++){
   float input=(float)samples[channel*160+sample],delayed=input;
   if(work->preDelayTime){delayed=*pre;*pre++=input;if(pre==work->preDelayLine[channel]+work->preDelayTime-1)pre=work->preDelayLine[channel];}
   float comb0=fmaf(work->combCoef[channel*2],c0->lastOutput,delayed),comb1=fmaf(work->combCoef[channel*2+1],c1->lastOutput,delayed);
   delay_write(c0,comb0);delay_write(c1,comb1);c0->lastOutput=delay_read(c0);c1->lastOutput=delay_read(c1);
   float sum=c0->lastOutput+c1->lastOutput;
   float first=fmaf(work->allPassCoeff,a0->lastOutput,sum);delay_write(a0,first);
   float value=-fmaf(work->allPassCoeff,first,-a0->lastOutput);a0->lastOutput=delay_read(a0);
   value=value*0.3f;value=fmaf(work->damping,lowpass,value);lowpass=value;
   float second=fmaf(work->allPassCoeff,a1->lastOutput,value);delay_write(a1,second);
   value=-fmaf(work->allPassCoeff,second,-a1->lastOutput);a1->lastOutput=delay_read(a1);
   float original=dry*input;value=fmaf(wet,value,original);samples[channel*160+sample]=fctiwz(value);
  }
  work->lpLastout[channel]=lowpass;work->preDelayPtr[channel]=pre;
 }
}
