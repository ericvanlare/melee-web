#include "gameplay_audio_bank.hpp"
#include <algorithm>
#include <stdexcept>
#define CHECK(condition) do {if(!(condition)) throw std::runtime_error("Audio trace check failed: " #condition);} while(0)
#include <cmath>
#include <bit>
#include <cstdio>
#include <fstream>
extern "C" int lbAudioAx_800237A8(int,int,int);
static std::vector<uint8_t> read(const char* path){std::ifstream f(path,std::ios::binary);if(!f)throw melee_web::DatError("Cannot open audio fixture");return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv){try{
 if(argc<5){puts("usage: audio_trace main.ssm mario.ssm smash2.sem dsp_coef.bin [extra.ssm ...]");return 2;}
 auto main=read(argv[1]),mario=read(argv[2]),sem=read(argv[3]),coefficients=read(argv[4]);char error[256];
 for(int sound:{0,74,443,180000,180001}){
  std::vector<float> reference;
  for(unsigned cycle=0;cycle<2;cycle++){
   melee_web::GameplayAudioBank bank(sem,{main,mario},coefficients);auto* a=bank.get();
   CHECK(lbAudioAx_800237A8(sound,127,64)>=0);
   std::vector<float> pcm(64000);CHECK(melee_web_audio_render(a,pcm.data(),cycle?17:160,error,sizeof(error)));
   if(cycle)CHECK(melee_web_audio_render(a,pcm.data()+34,143,error,sizeof(error)));
   uint32_t ids[64];int count=melee_web_audio_active_samples(a,ids,64);CHECK(count>0);for(int i=0;i<count;i++)CHECK(sound<10000?ids[i]<246:ids[i]>=783&&ids[i]<=814);
   printf("SEM sound%d selects original sample%u\n",sound,ids[0]);
   if(!melee_web_audio_render(a,pcm.data()+320,31840,error,sizeof(error)))throw melee_web::DatError(error);
   double energy=0;for(float value:pcm){CHECK(std::isfinite(value)&&value>=-1&&value<=1);energy+=value*value;}CHECK(energy>0);
   uint64_t hash=14695981039346656037ULL;
   for(float value:pcm){uint32_t bits=std::bit_cast<uint32_t>(value);for(unsigned b=0;b<4;b++){hash^=(bits>>(b*8))&255;hash*=1099511628211ULL;}}
   printf("sound%d cycle%u PCM FNV-1a %016llx\n",sound,cycle,(unsigned long long)hash);
   if(cycle)CHECK(pcm==reference);else reference=std::move(pcm);
   printf("cycle%u source voice PCM energy %.9f\n",cycle,energy);
  }
 }

 if(argc>5){
  std::vector<std::vector<uint8_t>> extra_storage;
  std::vector<std::span<const uint8_t>> banks{main,mario};
  for(int i=5;i<argc;i++){extra_storage.push_back(read(argv[i]));banks.emplace_back(extra_storage.back());}
  {
   melee_web::GameplayAudioBank overlapping_banks(sem,banks,coefficients);
   CHECK(melee_web_audio_generation(overlapping_banks.get())>0);
  }
  puts("Overlapping SSM sample IDs retained through original synth bucket construction and teardown");
 }

 puts("Original SEM/synth/AX four-tap PCM, partition invariance and scoped restart and source ITD ramp passed");
 }catch(const std::exception& e){puts(e.what());return 1;}return 0;}
