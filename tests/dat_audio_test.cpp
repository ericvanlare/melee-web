#include "dat_audio.hpp"
#include <fstream>
#include <iterator>
#include <iostream>
#include <cstring>
using namespace melee_web;
extern "C" uint32_t THPAudioDecode(int16_t*,const uint8_t*,int32_t);
static void check(bool v){if(!v)throw std::runtime_error("SSM audio test failed");}
static void word(std::vector<uint8_t>& b,size_t at,uint32_t v){for(int i=0;i<4;i++)b.at(at+i)=v>>(24-8*i);}
static void half(std::vector<uint8_t>& b,size_t at,uint16_t v){b.at(at)=v>>8;b.at(at+1)=v;}
static uint32_t read(const std::vector<uint8_t>& b,size_t at){return uint32_t(b.at(at))<<24|uint32_t(b.at(at+1))<<16|uint32_t(b.at(at+2))<<8|b.at(at+3);}
static void compare_aurora(const std::vector<uint8_t>& bytes,const DatAudioBank& bank){
 size_t payload=(size_t(read(bytes,0))+47)&~size_t(31);
 for(auto& sample:bank.samples)for(auto& channel:sample.channels){
  check(channel.current_nibble%16==2);
  size_t begin=channel.current_nibble/2-1,end=channel.end_nibble/2+1;
  std::vector<uint8_t> thp(80+end-begin);word(thp,4,channel.pcm.size());
  for(unsigned i=0;i<16;i++)half(thp,8+2*i,uint16_t(channel.coefficients[i]));
  half(thp,72,uint16_t(channel.history1));half(thp,74,uint16_t(channel.history2));
  std::memcpy(thp.data()+80,bytes.data()+payload+begin,end-begin);
  check(thp[80]==channel.predictor_scale);
  std::vector<int16_t> decoded(channel.pcm.size()*2);
  check(THPAudioDecode(decoded.data(),thp.data(),1)==channel.pcm.size());
  for(size_t i=0;i<channel.pcm.size();i++)check(decoded[i]==channel.pcm[i]&&decoded[i+channel.pcm.size()]==channel.pcm[i]);
 }
}
int main(int argc,char** argv){
 std::vector<uint8_t> bytes(104,0);word(bytes,0,72);word(bytes,4,8);word(bytes,8,1);word(bytes,12,123);word(bytes,16,1);word(bytes,20,32000);
 half(bytes,24,1);word(bytes,28,3);word(bytes,32,15);word(bytes,36,2);half(bytes,40,1024);half(bytes,76,100);half(bytes,82,200);
 for(size_t i=97;i<104;i++)bytes[i]=0x11;
 DatAudioBank authored(bytes);auto& s=authored.sample(123);check(s.sample_rate==32000&&s.channels.size()==1);auto& c=s.channels[0];
 check(c.pcm.size()==14&&c.pcm[0]==51&&c.pcm[1]==27&&c.loop_pcm.size()==13&&c.loop_pcm[0]==101);compare_aurora(bytes,authored);
 int rejected=0;for(auto [at,val]:std::vector<std::pair<size_t,uint32_t>>{{0,0xffffffff},{4,9},{8,0},{12,0xffffffff},{16,3},{20,0},{24,0x00010001},{28,1},{32,16},{36,18},{72,0x00010000},{72,0x00000080}}){auto bad=bytes;word(bad,at,val);try{DatAudioBank invalid(bad);}catch(const DatError&){rejected++;}}
 check(rejected==12);try{authored.sample(124);check(false);}catch(const DatError&){}
 for(int i=1;i<argc;i++){std::ifstream f(argv[i],std::ios::binary);check(bool(f));std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)),{});DatAudioBank bank(b);compare_aurora(b,bank);size_t frames=0,loops=0;for(auto& sound:bank.samples)for(auto& chan:sound.channels){frames+=chan.pcm.size();loops+=chan.looping;}
  std::cout<<"SSM base="<<bank.base_id<<" samples="<<bank.samples.size()<<" frames="<<frames<<" loops="<<loops<<" exactly match Aurora THP PCM\n";
 }
 std::cout<<"SSM authored loop history, malformed bounds, and independent decoder comparison passed\n";
}
