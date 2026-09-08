#include "gameplay_world.hpp"
#include "dat_archive.hpp"
#include "gameplay_match_context.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace melee_web;
extern "C" void melee_web_trajectory_sample(unsigned);
static void check(int ok,const char* why){if(!ok)throw DatError(why);}
int main(int argc,char** argv){try{
    check(argc==2,"Expected local asset directory");RuntimeFiles files;
    for(const char* name:{"PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","ItCo.usd","EfMrData.dat","EfCoData.dat","PdPm.dat","sislib_font.bin"}){
        std::ifstream f(std::filesystem::path(argv[1])/name,std::ios::binary);check(bool(f),"Cannot open owned trajectory asset");
        files[name]={std::istreambuf_iterator<char>(f),{}};
    }
    GameplayWorld world(files);char error[256];
    MeleeWebPlayerSettings players[2]={{0,0,4,{-40,world.floor_height(-40)+1,0},1},{1,1,4,{40,world.floor_height(40)+1,0},-1}};
    auto* match=melee_web_match_begin_players(players,2,70,1,world.collision(),error,sizeof(error));check(match!=nullptr,error);
    check(melee_web_match_create_fighters(match,error,sizeof(error)),error);
    PADStatus pads[4]={};
    for(unsigned i=0;i<120;i++)check(melee_web_match_step_raw(match,pads,error,sizeof(error)),error);
    melee_web_trajectory_sample(0);
    for(unsigned i=1;i<=102;i++){
        pads[0].button=i<=12?PAD_BUTTON_X:0;
        check(melee_web_match_step_raw(match,pads,error,sizeof(error)),error);melee_web_trajectory_sample(i);
    }
    check(melee_web_match_end(match,error,sizeof(error)),error);world.close();
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
