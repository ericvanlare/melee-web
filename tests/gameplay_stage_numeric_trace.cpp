#include "native_dat.hpp"
#include "gameplay_stage_numeric.h"
#include "gameplay_ground_data.h"
#include "gameplay_bootstrap.h"
#include <fstream>
#include <iterator>
#include <iostream>
using namespace melee_web;
static void put(std::vector<uint8_t>& b,uint32_t o,uint32_t v){for(int i=0;i<4;i++)b[32+o+i]=v>>(24-8*i);}
int main(int argc,char** argv){
 if(argc!=2){std::cerr<<"Usage: gameplay_stage_numeric_trace.js PATH_TO_GrNLa.dat\n";return 64;}
 std::ifstream f(argv[1],std::ios::binary);if(!f){std::cerr<<"Cannot read stage asset\n";return 65;}std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)),{});auto a=std::make_shared<DatArchive>(b);uint32_t h=0;for(auto s:a->public_symbols())if(s.name=="map_head")h=s.data_offset;auto table=*a->pointer(h),root=*a->pointer(table),pairs=*a->pointer(table+4);NativeDatArena arena(a);if(!melee_web_stage_markers_decode(arena.reader(),h))return 1;

 int rejected=0;for(auto [o,v]:std::vector<std::pair<uint32_t,uint32_t>>{{h+4,2},{table+8,262},{root+4,1},{root+20,0x7f800000},{root+32,0},{pairs,0xffff0094},{pairs,0x00010105},{pairs+4,0x00020094}}){auto bad=b;put(bad,o,v);try{NativeDatArena ar(std::make_shared<DatArchive>(bad));melee_web_stage_markers_decode(ar.reader(),h);}catch(const DatError&){rejected++;}}

 if(rejected!=8)return 6;
 uint32_t ground=0;for(auto symbol:a->public_symbols())if(symbol.name=="grGroundParam")ground=symbol.data_offset;
 auto* param=melee_web_ground_data_decode(arena.reader(),ground);auto* markers=melee_web_stage_markers_decode(arena.reader(),h);char error[256];
 for(int pass=0;pass<2;pass++){
  if(!melee_web_gameplay_startup(32*1024*1024,error,sizeof(error)))throw std::runtime_error(error);
  void* previous=melee_web_ground_data_publish(param);
  auto* context=melee_web_stage_numeric_begin(markers,error,sizeof(error));if(!context)throw std::runtime_error(error);
  float cam[4],blast[4],offset[2];if(!melee_web_stage_numeric_bounds(context,cam,blast,offset,error,sizeof(error)))return 2;
  std::cout<<"Original FD camera "<<cam[0]<<","<<cam[1]<<","<<cam[2]<<","<<cam[3]<<" blast "<<blast[0]<<","<<blast[1]<<","<<blast[2]<<","<<blast[3]<<" offset "<<offset[0]<<","<<offset[1]<<"\n";
  // Regression values from GALE01 1.02 GrNLa marker positions, with original
  // camera-origin subtraction. Implementation derives these from descriptors.
  if(!(cam[0]==-170&&cam[1]==170&&cam[2]==102&&cam[3]==-92&&
       blast[0]==-246&&blast[1]==246&&blast[2]==176&&blast[3]==-152&&
       offset[0]==0&&offset[1]==12))return 3;
  if(!melee_web_stage_numeric_end(context,error,sizeof(error)))throw std::runtime_error(error);
  if(melee_web_ground_data_publish(previous)!=param)return 4;
  if(melee_web_gameplay_stats().objects!=0)return 5;
  if(!melee_web_gameplay_shutdown(error,sizeof(error)))throw std::runtime_error(error);
 }
 std::cout<<"Original FD marker range context loaded/unloaded/restarted twice\n";
}
