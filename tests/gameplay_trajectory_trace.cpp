#include "gameplay_world.hpp"
#include "dat_archive.hpp"
#include "gameplay_match_context.h"
#include "gameplay_render.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
using namespace melee_web;
extern "C" void melee_web_trajectory_sample(unsigned);
extern "C" void melee_web_trajectory_sample_stock(unsigned);
static void check(int ok,const char* why){if(!ok)throw DatError(why);}
int main(int argc,char** argv){try{
    const bool stock_mode=argc==3&&(std::strcmp(argv[2],"--stock")==0||std::strcmp(argv[2],"--stock-jab")==0);
    const bool stock_jab=argc==3&&std::strcmp(argv[2],"--stock-jab")==0;
    check(argc==2||stock_mode,"Expected local asset directory or --stock");RuntimeFiles files;
    for(const char* name:{"PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","ItCo.usd","EfMrData.dat","EfCoData.dat","PdPm.dat","sislib_font.bin"}){
        std::ifstream f(std::filesystem::path(argv[1])/name,std::ios::binary);check(bool(f),"Cannot open owned trajectory asset");
        files[name]={std::istreambuf_iterator<char>(f),{}};
    }
    GameplayWorld world(files);char error[256];
    if(stock_mode){
        const auto p0=world.player_spawn(0),p1=world.player_spawn(1);
        MeleeWebPlayerSettings players[2]={{0,0,4,{p0[0],p0[1],p0[2]},1},{1,1,4,{p1[0],p1[1],p1[2]},-1}};
        auto* match=melee_web_match_begin_players(players,2,70,1,world.collision(),error,sizeof(error));check(match!=nullptr,error);
        check(melee_web_match_create_fighters(match,error,sizeof(error)),error);
        MeleeWebRenderSettings settings{640,480,{0,25,180},{0,15,0},30,1,1000,(UINT64_C(1)<<3)|(UINT64_C(1)<<5)};
        auto* camera=melee_web_render_begin_match(&settings,error,sizeof(error));check(camera!=nullptr,error);
        world.enable_full_stage();
        PADStatus pads[4]={{0}};
        for(unsigned i=0;i<120;i++)check(melee_web_match_step_raw(match,pads,error,sizeof(error)),error);
        melee_web_trajectory_sample_stock(0);
        const unsigned final_tick=stock_jab?753:540;
        for(unsigned i=1;i<=final_tick;i++){
            pads[1].stickX=i<=100?80:0;
            pads[0].button=(stock_jab&&i>=661&&i<=663)?PAD_BUTTON_A:0;
            check(melee_web_match_step_raw(match,pads,error,sizeof(error)),error);
            melee_web_trajectory_sample_stock(i);
        }
        world.end_stage();
        check(melee_web_render_end(camera,error,sizeof(error)),error);
        check(melee_web_match_end(match,error,sizeof(error)),error);
        world.close();
        return 0;
    }
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
