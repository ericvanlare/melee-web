#include "dat_lights.hpp"
#include "gameplay_bootstrap.h"
#include "gameplay_stage_context.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>
using namespace melee_web;
void check(bool b,const char* msg){if(!b)throw std::runtime_error(msg);}
int main(int argc,char** argv){try{
    std::vector<MeleeWebStageLightDesc> input(2);
    input[0].flags=4;input[0].color[0]=0xb3;input[0].color[3]=255;
    input[1].flags=13;input[1].has_position=1;input[1].position[0]=.5f;
    input[1].position[1]=-1;input[1].position[2]=1;input[1].color[0]=255;input[1].color[3]=255;
    if(argc==2){std::ifstream f(argv[1],std::ios::binary);check(bool(f),"Open stage archive");std::vector<uint8_t>b((std::istreambuf_iterator<char>(f)),{});input=DatLights(DatArchive(b)).lights;}
    char error[256];MeleeWebStageLights* stale=nullptr;uint32_t count;uint16_t flags[64];uint8_t colors[256];
    for(int pass=0;pass<2;pass++){
        check(melee_web_gameplay_startup(4*1024*1024,error,sizeof(error)),error);
        if(stale){check(!melee_web_stage_lights_stats(stale,&count,flags,colors,64,error,sizeof(error)),"Stale light stats rejected");check(melee_web_stage_lights_destroy(stale,error,sizeof(error)),error);}
        auto* h=melee_web_stage_lights_create(input.data(),input.size(),error,sizeof(error));check(h,error);
        check(melee_web_stage_lights_attach(h,error,sizeof(error)),error);
        check(!melee_web_stage_lights_attach(h,error,sizeof(error)),"Duplicate publication rejected");
        check(melee_web_stage_lights_load(h,error,sizeof(error)),error);
        check(melee_web_stage_lights_stats(h,&count,flags,colors,64,error,sizeof(error)),error);
        check(count==input.size(),"Original light count");
        for(uint32_t i=0;i<count;i++){check((flags[i]&31)==(input[i].flags&31),"Original light flags");check(!std::memcmp(colors+i*4,input[i].color,4),"Original light color");}
        check(melee_web_stage_lights_detach(h,error,sizeof(error)),error);
        check(melee_web_gameplay_step(error,sizeof(error)),error);
        check(melee_web_gameplay_shutdown(error,sizeof(error)),error);stale=h;
    }
    check(melee_web_stage_lights_destroy(stale,error,sizeof(error)),error);
    std::cout<<"Original stage light construction, publication, unload and restart passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
