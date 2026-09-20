/* Optional comparison driver for scripts/check_audio_compatibility.py.
 * This tests compatibility with the old port, not hardware accuracy.
 * Reference source is compiled only into a temporary local test executable. */
#include "gameplay_audio_resample.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
int16_t old_resample(MeleeWebAudioResample*,uint32_t,unsigned,const int16_t*,MeleeWebAudioReadSample,void*);
typedef struct Input {uint32_t bits; unsigned count;} Input;
static int16_t next(void* p){Input* in=p;in->bits=in->bits*1664525u+1013904223u;in->count++;return (int16_t)(in->bits>>16);}
static unsigned long cases;
static void check(MeleeWebAudioResample* state,uint32_t ratio,unsigned mode,const int16_t* bank){
 MeleeWebAudioResample other=*state;Input a={0xcafe1234u,0},b=a;
 int16_t old=old_resample(state,ratio,mode,bank,next,&a);
 int16_t fresh=melee_web_audio_resample(&other,ratio,mode,bank,next,&b);
 if(old!=fresh||state->fraction!=other.fraction||memcmp(state->history,other.history,8)||a.count!=b.count||a.bits!=b.bits){fprintf(stderr,"Mismatch case%lu ratio%x mode%u\n",cases,ratio,mode);exit(1);}cases++;
}
int main(int argc,char**argv){
 if(argc!=2)return 2;FILE* f=fopen(argv[1],"rb");if(!f)return 2;int16_t coeffs[2048];for(unsigned i=0;i<2048;i++){int hi=fgetc(f),lo=fgetc(f);if(hi<0||lo<0)return 2;coeffs[i]=(int16_t)((hi<<8)|lo);}fclose(f);
 const uint32_t ratios[]={0,1,0x7fff,0x8000,0xffff,0x10000,0x10001,0x18000,0x30001,0x3ffff,0x40000};
 for(unsigned bank=0;bank<3;bank++)for(unsigned mode=0;mode<3;mode++)for(unsigned r=0;r<sizeof(ratios)/sizeof(ratios[0]);r++)for(unsigned phase=0;phase<65536;phase++){
  MeleeWebAudioResample state={{INT16_MIN,INT16_MAX,-1,1},phase};check(&state,ratios[r],mode,coeffs+512*bank);
 }
 Input rng={0x1234,0};MeleeWebAudioResample sequence={{0,0,0,0},0};int16_t synthetic[512];
 for(unsigned i=0;i<512;i++)synthetic[i]=next(&rng);
 for(unsigned i=0;i<1000000;i++){(void)next(&rng);uint32_t ratio=rng.bits%0x40001;unsigned mode=(rng.bits>>24)%3;check(&sequence,ratio,mode,synthetic);}
 printf("%lu exact sample, phase, four-history-word and source-read comparisons passed\n",cases);
}
