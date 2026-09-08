#include "dat_audio_programs.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
using namespace melee_web;
std::vector<uint8_t> encode(std::vector<uint32_t> words){std::vector<uint8_t> bytes;for(auto word:words)for(int shift:{24,16,8,0})bytes.push_back(word>>shift);return bytes;}
int main(int argc,char**argv){
 auto words=std::vector<uint32_t>{0,0,1,0,1,28,0,0x01000000,0xfd000000,0x0e000000};
 auto valid=DatAudioPrograms(encode(words));assert(valid.program_index(0)==0);assert(valid.words[8]==0xfd000000);
 unsigned bad=0;auto reject=[&](std::vector<uint32_t> w){try{DatAudioPrograms p(encode(w));assert(false);}catch(const DatError&){bad++;}};
 auto w=words;w[2]=0xffffffff;reject(w);w=words;w[5]=4;reject(w);w=words;w[5]=29;reject(w);w=words;w[7]=0xfe000000;reject(w);w=words;w[9]=0;reject(w);w=words;w[8]=0x03000009;reject(w);w=words;w[8]=0x03000001;reject(w);
 try{valid.program_index(10000);assert(false);}catch(const DatError&){bad++;}
 // Original SEM ignores FD metadata; finite counted loops without a delay are legal.
 w={0,0,1,0,1,28,0,0x02000002,0xfd000000,0x03000002,0x0e000000};DatAudioPrograms counted(encode(w));
 w={0,0,1,0,1,28,0,0x02000002,0xfd000000,0x03000003,0x0e000000};reject(w);
 w={0,0,1,0,1,28,0,0x0200ffff,0xfd000000,0x03000002,0x0e000000};reject(w);
 printf("SEM authored metadata/finite-loop and%u malformed checks passed\n",bad);
 if(argc==2){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f),{}};DatAudioPrograms real(bytes);assert(real.tables[2].size()==55&&real.tables[3].size()==4035);assert(real.program_index(180000)==2025);puts("Original SEM55 banks/4035 programs validated");}
}
