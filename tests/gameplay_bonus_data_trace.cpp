#include "gameplay_bonus_data.h"
#include "native_dat.hpp"
#include <fstream>
#include <iterator>
#include <iostream>
#include <cstring>
using namespace melee_web;
/* Real source getter has pointer return ABI; headers remain in the C source
 * compilation lane because unrelated original structures are not C++ headers. */
extern "C" void* pl_80038914(void);
static void check(bool b){if(!b)throw std::runtime_error("Bonus data trace check failed");}
static void put(std::vector<uint8_t>& b,uint32_t o,uint32_t v){for(int i=0;i<4;i++)b[32+o+i]=v>>(24-8*i);}
int main(int argc,char** argv){
 if(argc!=2){std::cerr<<"Usage: gameplay_bonus_data_trace.js PATH_TO_PdPm.dat\n";return 64;}
 std::ifstream f(argv[1],std::ios::binary);if(!f){std::cerr<<"Cannot read bonus asset\n";return 65;}std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});auto a=std::make_shared<DatArchive>(bytes);uint32_t root=UINT32_MAX;
 for(auto symbol:a->public_symbols())if(symbol.name=="plLoadCommonData")root=symbol.data_offset;
 check(root!=UINT32_MAX);uint32_t thresholds=*a->pointer(root,0x184);NativeDatArena owner(a);auto* decoded=melee_web_bonus_data_decode(owner.reader(),root);char error[256];void* previous=pl_80038914();
 void** public_root=(void**)melee_web_bonus_data_public_data(decoded);
 check(public_root&&*public_root!=nullptr);
 for(int pass=0;pass<2;pass++){
  check(melee_web_bonus_data_begin(decoded,error,sizeof(error)));check(melee_web_bonus_data_ready(decoded));check(!melee_web_bonus_data_begin(decoded,error,sizeof(error)));
  auto* typed=(const uint8_t*)pl_80038914();check(typed&&typed!=previous);
  check(typed==*public_root);
  for(unsigned i=0;i<0x184;i+=4){if(i==0xc0)check(!std::memcmp(typed+i,a->range(thresholds+i,4).data(),4));else{uint32_t bits;std::memcpy(&bits,typed+i,4);check(bits==a->be32(thresholds+i));}}
  check(melee_web_bonus_data_end(decoded,error,sizeof(error)));check(!melee_web_bonus_data_ready(decoded));check(pl_80038914()==previous);check(!melee_web_bonus_data_end(decoded,error,sizeof(error)));
 }
 int rejected=0;for(auto [at,value]:std::vector<std::pair<uint32_t,uint32_t>>{{thresholds,0x7fc00000},{thresholds+0x8c,0x7f800000},{thresholds+0x180,0x7f800000},{root,root},{root,1}}){auto bad=bytes;put(bad,at,value);try{NativeDatArena malformed(std::make_shared<DatArchive>(bad));melee_web_bonus_data_decode(malformed.reader(),root);}catch(const DatError&){rejected++;}}
 check(rejected==5);std::cout<<"Original bonus getter reads all97 actual PdPm words; scoped restore/restart and5 malformed cases passed\n";
}
