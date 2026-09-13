#include "gameplay_world.hpp"
#include "gameplay_match_context.h"
#include "dat_archive.hpp"
#include "gameplay_action_trace.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace melee_web;
extern "C" void melee_web_fighter_link_gate(void);
extern "C" int melee_web_match_context_trace(MeleeWebCollision*,const MeleeWebMatchSettings*,uint32_t,char*,size_t);
extern "C" int melee_web_match_two_player_trace(MeleeWebCollision*,const MeleeWebPlayerSettings*,uint32_t,const PADStatus*,char*,size_t);
namespace {
std::vector<uint8_t> bytes(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary|std::ios::ate);auto n=f.tellg();
    if(n<=0||n>64*1024*1024)throw DatError("Invalid local asset: "+p.string());
    f.seekg(0);std::vector<uint8_t> out(static_cast<size_t>(n));
    if(!f.read(reinterpret_cast<char*>(out.data()),n))throw DatError("Truncated local asset");return out;
}
void check(int ok,const char* error){if(!ok)throw DatError(error);}
}
int main(int argc,char** argv){
    try{
        melee_web_fighter_link_gate();
        if(argc<2||argc>4)throw DatError("Usage: fighter_runtime_probe.js LOCAL_ASSET_DIRECTORY [--movement | --action KIND]");
        RuntimeFiles files;
        for(const char* name:{"PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","ItCo.usd","EfMrData.dat","EfCoData.dat","PdPm.dat","LbRb.dat","sislib_font.bin"})
            files[name]=bytes(std::filesystem::path(argv[1])/name);
        char error[256];
        for(unsigned pass=0;pass<2;pass++){
            GameplayWorld world(files);
            MeleeWebMatchSettings settings{};settings.player={0,0,4,{0,world.floor_height(0)+1,0},1};
            settings.camera_subjects=70;settings.random_seed=0x13579bdf;
            check(melee_web_match_context_trace(world.collision(),&settings,120,error,sizeof(error)),error);
            MeleeWebPlayerSettings players[2]={{0,0,4,{-20,world.floor_height(-20)+1,0},1},{1,1,4,{20,world.floor_height(20)+1,0},-1}};
            PADStatus movement{};movement.stickX=48;movement.err=0;
            check(melee_web_match_two_player_trace(world.collision(),players,120,argc==3?&movement:nullptr,error,sizeof(error)),error);
            if(argc==4){
                MeleeWebActionTraceKind kind;
                const std::string requested=argv[3];
                if(requested=="jab")kind=MeleeWebActionTrace_Jab;
                else if(requested=="jump")kind=MeleeWebActionTrace_JumpLanding;
                else if(requested=="shield")kind=MeleeWebActionTrace_Shield;
                else if(requested=="damage")kind=MeleeWebActionTrace_ContactDamage;
                else throw DatError("Unknown source action trace");
                if(kind==MeleeWebActionTrace_ContactDamage){players[0].position[0]=-3;players[1].position[0]=3;}
                MeleeWebActionTraceReport report{};
                check(melee_web_match_action_trace(world.collision(),players,kind,&report,error,sizeof(error)),error);
            }
            world.verify_immutable_archives();world.close();
        }
        std::cout<<"Original single/two-player Fighter_Create, neutral ticks, unload and restart completed in two worlds\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
