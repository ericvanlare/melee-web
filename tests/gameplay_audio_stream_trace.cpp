#include "gameplay_audio_bank.hpp"
#include "gameplay_audio_stream_asset.hpp"
#include <fstream>
#include <cstdio>
#include <cmath>
#include <bit>
#include <cstdint>
#include <stdexcept>
#define CHECK(c) do{if(!(c))throw std::runtime_error("HPS trace check: " #c);}while(0)
extern "C" int lbAudioAx_80023F28(int);
static std::vector<uint8_t> read(const char* path){std::ifstream f(path,std::ios::binary);CHECK(f.good());return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){try{
 CHECK(argc==6);auto main=read(argv[1]),mario=read(argv[2]),sem=read(argv[3]),coef=read(argv[4]),hps=read(argv[5]);uint64_t first_hash=0;
 for(unsigned cycle=0;cycle<2;cycle++){
  melee_web::GameplayAudioBank bank(sem,{main,mario},coef);melee_web::GameplayAudioStream stream(bank.get(),"/audio/sp_end.hps",hps);
  CHECK(lbAudioAx_80023F28(78)==0);char error[256];std::vector<float> pcm(64000);double energy=0;uint64_t hash=14695981039346656037ULL;
  // Cross all fifty block boundaries and the original intro-to-loop transition.
  for(unsigned second=0;second<100;second++){
   if(!melee_web_audio_render(bank.get(),pcm.data(),32000,error,sizeof(error)))throw std::runtime_error(error);
   for(float v:pcm){CHECK(std::isfinite(v));energy+=double(v)*v;uint32_t bits=std::bit_cast<uint32_t>(v);for(unsigned b=0;b<4;b++){hash^=(bits>>(b*8))&255;hash*=1099511628211ULL;}}
  }
  uint32_t completed,revisited;CHECK(melee_web_audio_stream_progress(bank.get(),&completed,&revisited));CHECK(completed>50&&revisited>0);printf("HPS payloads%u revisited%u\n",completed,revisited);
  if(cycle)CHECK(hash==first_hash);else first_hash=hash;
  CHECK(energy>1);printf("Original HPS cycle%u 100 seconds, energy%.9f\n",cycle,energy);
 }
 puts("Original HPS three-slot scheduler, native PCM loop, and restart passed");
 }catch(const std::exception& e){puts(e.what());return 1;}return 0;}
