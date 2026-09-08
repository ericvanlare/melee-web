#include "dat_audio_stream.hpp"
#include <fstream>
#include <iterator>
#include <iostream>
#include <cstring>
using namespace melee_web;
extern "C" uint32_t THPAudioDecode(int16_t*,const uint8_t*,int32_t);
static void check(bool v){if(!v)throw std::runtime_error("HPS test failed");}
static void word(std::vector<uint8_t>& b,size_t o,uint32_t v){for(int i=0;i<4;i++)b.at(o+i)=v>>(24-8*i);}
static void half(std::vector<uint8_t>& b,size_t o,uint16_t v){b.at(o)=v>>8;b.at(o+1)=v;}
static size_t compare(const std::vector<uint8_t>& bytes,const DatAudioStream& stream){
 size_t frames=0;
 for(const auto& block:stream.blocks)for(size_t c=0;c<block.channels.size();c++){
  const auto& channel=block.channels[c];size_t n=block.payload_size/block.channels.size();
  std::vector<uint8_t> thp(80+n);word(thp,4,channel.pcm.size());
  for(unsigned i=0;i<16;i++)half(thp,8+2*i,uint16_t(channel.coefficients[i]));
  half(thp,72,uint16_t(channel.history1));half(thp,74,uint16_t(channel.history2));
  std::memcpy(thp.data()+80,bytes.data()+block.file_offset+32+c*n,n);
  std::vector<int16_t> decoded(channel.pcm.size()*2);
  check(THPAudioDecode(decoded.data(),thp.data(),1)==channel.pcm.size());
  for(size_t i=0;i<channel.pcm.size();i++)check(decoded[i]==channel.pcm[i]);
  frames+=channel.pcm.size();
 }
 return frames;
}
int main(int argc,char** argv){
 std::vector<uint8_t> b(256);std::memcpy(b.data()," HALPST\0",8);word(b,8,32000);word(b,12,1);
 half(b,16,1);word(b,20,2);word(b,24,127);word(b,28,2);half(b,32,1024);half(b,68,100);
 for(size_t o:{128U,192U}){word(b,o,32);word(b,o+4,63);word(b,o+8,192);half(b,o+14,o==128?100:200);for(size_t j=0;j<32;j++)b[o+32+j]=j%8?0x11:0;}
 DatAudioStream stream(b);check(stream.blocks.size()==2&&stream.loop_block==1);check(stream.blocks[0].channels[0].pcm[0]==51&&stream.blocks[1].channels[0].pcm[0]==101);compare(b,stream);
 auto terminal=b;word(terminal,200,UINT32_MAX);check(!DatAudioStream(terminal).loop_block);
 int rejected=0;for(auto [o,v]:std::vector<std::pair<size_t,uint32_t>>{{0,0},{8,0},{12,3},{16,0x10001},{20,1},{64,0x10000},{128,65568},{128,0},{132,64},{136,160},{136,256},{136,129},{140,0x800000},{136,0},{68,0}}){auto bad=b;word(bad,o,v);try{DatAudioStream nope(bad);}catch(const DatError&){rejected++;}}
 check(rejected==15);
 for(int i=1;i<argc;i++){std::ifstream f(argv[i],std::ios::binary);check(bool(f));std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});DatAudioStream real(bytes);const auto frames=compare(bytes,real);std::cout<<"HPS blocks="<<real.blocks.size()<<" loop="<<(real.loop_block?int(*real.loop_block):-1)<<" channel_samples="<<frames<<" exactly match Aurora THP PCM\n";}
 std::cout<<"HPS authored loop history, malformed bounds, and independent decoder comparison passed\n";
}
