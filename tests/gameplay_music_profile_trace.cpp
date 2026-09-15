// Real match construction and HPS ownership; no ticks, rendering or retail
// equivalence claim. The separate captured workload supplies that comparison.
#include "gameplay_match_session.hpp"
#include "gameplay_menu.h"
#include "gameplay_audio_stream.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

extern "C" uint16_t* gmMainLib_GetUnlockedCharactersBitmaskPtr(void);
extern "C" uint16_t* gmMainLib_8015EDA4(void);
extern "C" uint32_t* seed_ptr;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static uint32_t advance(uint32_t seed,unsigned count){while(count--)seed=seed*214013u+2531011u;return seed;}
int main(int argc,char** argv){try{
    check(argc==3,"Expected owned menu and game directories");
    melee_web::RuntimeFiles files;
    for(const char* directory:{argv[1],argv[2]})
        for(const auto& entry:std::filesystem::directory_iterator(directory)){
            if(!entry.is_regular_file()||entry.file_size()>64*1024*1024)continue;
            std::ifstream file(entry.path(),std::ios::binary);
            files[entry.path().filename().string()]={std::istreambuf_iterator<char>(file),{}};
        }
    char error[256]{};
    MeleeWebMenuRuntime services{nullptr,
        [](void*,MeleeWebMenuScene,char*,size_t){return 1;},
        [](void*,char*,size_t){return 1;},
        [](void*,MeleeWebMenuScene,int*,char*,size_t){return 1;}};
    auto* menu=melee_web_menu_session_create(&services,nullptr,error,sizeof(error));check(menu,error);
    MeleeWebMenuMatchSelection selection{};
    selection.start=melee_web_menu_css(menu)->vs.start;
    check(melee_web_menu_session_destroy(menu,error,sizeof(error)),error);
    selection.start.rules.match_kind=MatchKind_Stock;
    selection.start.rules.is_stock=selection.start.rules.is_vs=true;
    selection.start.rules.xB=-1;selection.start.rules.stkind=32;
    selection.start.rules.x0_3=selection.hud_layout=2;
    selection.random_seed=1264785038u;selection.player_count=2;
    selection.save_profile_present=1;selection.unlocked_stages=0x1c0;
    selection.start.players[0].ckind=23;selection.start.players[1].ckind=22;
    for(unsigned i=0;i<2;i++){
        selection.start.players[i].stocks=4;
        selection.start.players[i].color=0;
        selection.players[i]={i,4,0,0};
    }
    const uint16_t previous_characters=*gmMainLib_GetUnlockedCharactersBitmaskPtr();
    const uint16_t previous_stages=*gmMainLib_8015EDA4();
    struct Case {uint16_t mask;unsigned music_mode;bool silent;const char* path;unsigned calls;};
    const Case cases[]={
        {0,0,false,"/audio/sp_end.hps",8},
        {0x7ff,0,false,"/audio/sp_end.hps",9},
        {0x7ff,1,false,"/audio/hyaku2.hps",9},
        {0x7ff,0,true,nullptr,8},
    };
    for(const auto& value:cases){
        selection.unlocked_characters=value.mask;
        selection.start.rules.xA=value.music_mode;
        selection.start.rules.x1_4=value.silent;
        melee_web::GameplayMatchSession match(files,selection);
        const char* path=melee_web_audio_stream_path(match.audio());
        check(value.path ? path&&std::string(path)==value.path : !path,"Original selected HPS differs");
        check(*seed_ptr==advance(selection.random_seed,value.calls),"Original music RNG call order differs");
        check(*gmMainLib_GetUnlockedCharactersBitmaskPtr()==value.mask&&
              *gmMainLib_8015EDA4()==selection.unlocked_stages,"Match profile was not published");
        match.close();
        check(*gmMainLib_GetUnlockedCharactersBitmaskPtr()==previous_characters&&
              *gmMainLib_8015EDA4()==previous_stages,"Match teardown leaked its save profile");
    }
    selection.start.rules.x1_4=false;selection.start.rules.xA=1;
    files.erase("hyaku2.hps");
    bool rejected=false;
    try{melee_web::GameplayMatchSession missing(files,selection);}
    catch(const std::exception& e){rejected=std::string(e.what()).find("was not imported")!=std::string::npos;}
    check(rejected,"Missing selected music silently succeeded");
    check(*gmMainLib_GetUnlockedCharactersBitmaskPtr()==previous_characters&&
          *gmMainLib_8015EDA4()==previous_stages,"Failed construction leaked its save profile");
    std::cout<<"Original music profiles, alternate HPS, RNG order and teardown passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
