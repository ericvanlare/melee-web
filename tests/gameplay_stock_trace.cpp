#include "gameplay_world.hpp"
#include "gameplay_match_context.h"
#include "gameplay_render.h"
#include "gameplay_match_rules.h"
#include "dat_archive.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace melee_web;
static void check(int ok,const char* why){if(!ok){std::cerr<<why<<'\n';throw DatError(why);}}
static std::vector<uint8_t> bytes(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);check(bool(f),"Open owned stock-test asset");return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){try{
 check(argc==2,"Expected local asset directory");RuntimeFiles files;
 for(const char* name:{"PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","ItCo.usd","EfMrData.dat","EfCoData.dat","PdPm.dat","sislib_font.bin"})files[name]=bytes(std::filesystem::path(argv[1])/name);
 char error[256];
 for(unsigned cycle=0;cycle<2;cycle++){
  GameplayWorld world(files);
  MeleeWebPlayerSettings players[2]={{0,0,4,{-20,world.floor_height(-20)+1,0},1},{1,1,4,{20,world.floor_height(20)+1,0},-1}};
  auto* match=melee_web_match_begin_players(players,2,70,1,world.collision(),error,sizeof(error));check(match!=nullptr,error);
  check(melee_web_match_create_fighters(match,error,sizeof(error)),error);
  MeleeWebRenderSettings settings{640,480,{0,25,180},{0,15,0},30,1,1000,(UINT64_C(1)<<3)|(UINT64_C(1)<<5)};
  auto* camera=melee_web_render_begin_match(&settings,error,sizeof(error));check(camera!=nullptr,error);
  world.enable_full_stage();
  bool lost=false,returned=false,finished=false;int previous=-1,stock=4,respawns=0;bool jump=false;
  for(unsigned tick=0;tick<4000;tick++){
   PADStatus pads[4]={{0}};
   // Ordinary sustained right input leaves the platform. Release as soon as
   // original stock accounting observes the fall; no positions/states are set.
   if(tick>=20&&!lost)pads[0].stickX=80;
   if(jump)pads[0].button=PAD_BUTTON_X;
   check(melee_web_match_step_raw(match,pads,error,sizeof(error)),error);
   MeleeWebMatchStats stats[2];for(unsigned i=0;i<2;i++)check(melee_web_match_player_stats(match,i,&stats[i],error,sizeof(error)),error);
   if(stats[0].motion_id!=previous){std::cerr<<"Stock cycle"<<cycle<<" tick"<<tick<<" action"<<stats[0].motion_id<<" stocks"<<stats[0].stocks<<" x"<<stats[0].position[0]<<" y"<<stats[0].position[1]<<'\n';previous=stats[0].motion_id;}
   check(stats[1].stocks==4,"Stationary opponent lost a stock");
   jump=!lost&&stock<4&&stats[0].ground_or_air==0&&stats[0].position[0]>65;
   if(stats[0].stocks<stock){lost=true;stock=stats[0].stocks;}
   int winner=-1;int outcome=melee_web_match_rules_outcome(&winner);
   if(stock==0){check(outcome==2&&winner==1,"Original elimination outcome or winner incorrect");finished=true;break;}
   check(outcome==0,"Original match ended before final stock");
   if(lost&&stats[0].motion_id==14&&stats[0].ground_or_air==0){returned=true;lost=false;++respawns;}

  }
  check(returned&&respawns==3&&finished,"Original four-stock elimination and three grounded respawns did not complete");
  world.end_stage();
  check(melee_web_render_end(camera,error,sizeof(error)),error);check(melee_web_match_end(match,error,sizeof(error)),error);world.close();
 }
 std::cout<<"Original input four-stock elimination, three respawns and winner passed in two worlds\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
