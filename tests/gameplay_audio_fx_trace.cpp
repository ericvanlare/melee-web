#include "gameplay_audio_bank.hpp"
extern "C" {
#include "gameplay_audio_fx.h"
}
#include <stdexcept>
#include <fstream>
#include <cstdio>
#include <cmath>
#define CHECK(c) do{if(!(c))throw std::runtime_error("AXFX trace check: " #c);}while(0)
extern "C" int lbAudioAx_800237A8(int,int,int);
static std::vector<uint8_t> read(const char* path){std::ifstream f(path,std::ios::binary);CHECK(f.good());return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv){try{
 CHECK(argc==5);auto main=read(argv[1]),mario=read(argv[2]),sem=read(argv[3]),coef=read(argv[4]);std::vector<int32_t> first;
 for(unsigned cycle=0;cycle<2;cycle++){
  melee_web::GameplayAudioBank bank(sem,{main,mario},coef);char error[256];auto* effects=melee_web_audio_fx_create(error,sizeof(error));CHECK(effects);
  std::vector<int32_t> output;int delay_first=-1;double reverb_energy=0;
  for(unsigned block=0;block<200;block++){
   melee_web_audio_fx_prepare(effects);
   if(block==0){melee_web_audio_fx_send(effects,0,0,0,20000);melee_web_audio_fx_send(effects,1,1,0,20000);}
   for(unsigned sample=0;sample<160;sample++){
    int32_t left=melee_web_audio_fx_output(effects,0,sample),right=melee_web_audio_fx_output(effects,1,sample);output.push_back(left);output.push_back(right);reverb_energy+=(double)left*left;
    if(right&&delay_first<0){delay_first=block*160+sample;CHECK(right==6875);}
   }
  }
  CHECK(delay_first==63*160);CHECK(reverb_energy>1);if(cycle)CHECK(output==first);else first=std::move(output);
  printf("AXFX cycle%u original delay onset%d; translated source reverb energy%.0f\n",cycle,delay_first,reverb_energy);
  melee_web_audio_fx_destroy(effects);
 }
 std::vector<float> dry;
 for(unsigned wet=0;wet<2;wet++){
  melee_web::GameplayAudioBank bank(sem,{main,mario},coef);char error[256];if(wet)CHECK(melee_web_audio_enable_effects(bank.get(),error,sizeof(error)));
  CHECK(lbAudioAx_800237A8(74,127,64)>=0);std::vector<float> pcm(64000);CHECK(melee_web_audio_render(bank.get(),pcm.data(),32000,error,sizeof(error)));
  double energy=0;for(float sample:pcm)energy+=(double)sample*sample;CHECK(energy>0);
  if(wet){CHECK(pcm!=dry);double difference=0;for(size_t i=0;i<pcm.size();i++)difference+=fabs(pcm[i]-dry[i]);CHECK(difference>1);printf("Original jump SEM+AXFX PCM differs from dry by%.6f\n",difference);}else dry=std::move(pcm);
 }
 puts("Original AXFX allocation/callback/three-buffer latency/restart passed");
 }catch(const std::exception& e){puts(e.what());return 1;}return 0;}
