// Explicit source-start fixtures for new content. This executes the real match
// lifecycle but does not claim CSS input, rendered output or retail equivalence.
#include "gameplay_match_session.hpp"
#include "gameplay_menu.h"
#include "gameplay_content.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>

extern "C" int melee_web_test_content_player(unsigned,int,unsigned);
extern "C" int melee_web_test_item_count(int);
extern "C" int melee_web_story_state(uint32_t*,unsigned*,int*,int*,int*,int*);
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    if(argc<3||argc>6)throw std::runtime_error("Expected owned menu/game directories and optional StKind/P1 CKind/P2 CKind");
    melee_web::RuntimeFiles files;
    for(const auto* root:{argv[1],argv[2]})for(const auto& entry:std::filesystem::directory_iterator(root)){
        if(!entry.is_regular_file())continue;
        const auto name=entry.path().filename().string();
        if(entry.file_size()>64*1024*1024)continue;
        std::ifstream stream(entry.path(),std::ios::binary);
        files[name]={(std::istreambuf_iterator<char>(stream)),{}};
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
    selection.hud_layout=2;selection.start.rules.x0_3=2;selection.random_seed=0x13579bdf;
    selection.start.rules.stkind=argc>=4?std::stoi(argv[3]):St_Kind_Last;
    const int fighter_ckind=argc>=5?std::stoi(argv[4]):CKIND_FALCO;
    const auto* fighter_content=melee_web_fighter_content(fighter_ckind);
    check(fighter_content,"Selected fighter has no admitted source content");
    const int opponent_ckind=argc==6?std::stoi(argv[5]):CKIND_MARIO;
    const auto* opponent_content=melee_web_fighter_content(opponent_ckind);
    check(opponent_content,"Selected opponent has no admitted source content");
    selection.start.players[0].ckind=fighter_ckind;
    selection.start.players[1].ckind=opponent_ckind;
    for(unsigned cycle=0;cycle<4;cycle++){
        selection.start.players[0].color=cycle;
        selection.start.players[1].color=cycle;
        for(unsigned i=0;i<2;i++)selection.players[i]={i,4,cycle,0};
        std::cout<<"Construct mixed content stage="<<selection.start.rules.stkind<<" costume="<<cycle<<std::endl;
        melee_web::GameplayMatchSession match(files,selection);
        PADStatus raw[4]{};raw[2].err=raw[3].err=PAD_ERR_NO_CONTROLLER;
        float pcm[1068];unsigned phase=0;
        auto tick=[&](){match.tick(raw);phase+=32000;const auto count=phase/60;phase%=60;
            check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
            for(unsigned i=0;i<2;i++){const auto state=match.player_stats(i);
                check(std::isfinite(state.position[0])&&std::isfinite(state.position[1]),"Nonfinite fighter state");}};
        for(unsigned n=0;!match.ready()&&n<600;n++)tick();
        check(match.ready(),"Original Ready did not finish");
        check(melee_web_test_content_player(0,fighter_content->fighter_kind,cycle),"Original selected-fighter identity/costume/icon differs");
        check(melee_web_test_content_player(1,opponent_content->fighter_kind,cycle),"Original opponent identity/costume/icon differs");
        if(selection.start.rules.stkind==St_Kind_Story){
            uint32_t map_mask=0;unsigned map_count=0;int randall_timer=0,shy_timer=0,shy_count=0,shy_pattern=0;
            check(melee_web_story_state(&map_mask,&map_count,&randall_timer,&shy_timer,&shy_count,&shy_pattern),
                  "Yoshi's Story original map lifecycle is incomplete");
            check(map_mask==0xf&&map_count==4&&randall_timer>=-1&&randall_timer<=29&&
                      shy_timer>=0&&shy_timer<=120&&shy_count>=0&&shy_count<=5,
                  "Yoshi's Story Randall/Shy Guy source state is outside its authored bounds");
            bool shy_live=melee_web_test_item_count(0xd2)>0;
            for(unsigned n=0;n<180&&!shy_live;n++){tick();shy_live=melee_web_test_item_count(0xd2)>0;}
            check(shy_live,"Yoshi's Story did not create an original Shy Guy item after its 120-frame timer");
            std::cout<<"Yoshi's Story maps="<<map_count<<" Randall timer="<<randall_timer
                     <<" Shy timer="<<shy_timer<<" pattern="<<shy_pattern<<std::endl;
        }else{
        // Battlefield's authored versus spawns put P2 on the top platform.
        // Drop that source Fighter through its pass-through platforms before
        // checking a horizontal projectile on the main floor.
        for(unsigned n=0;n<180;n++){
            const auto p1=match.player_stats(0),p2=match.player_stats(1);
            if(std::abs(p2.position[1]-p1.position[1])<5.0f)break;
            raw[1].stickY=-80;tick();
        }
        raw[1].stickY=0;
        check(std::abs(match.player_stats(1).position[1]-match.player_stats(0).position[1])<5.0f,
              "Raw input did not bring both fighters to the same stage level");
        for(unsigned n=0;n<120;n++){
            const auto p1=match.player_stats(0),p2=match.player_stats(1);
            if(std::abs(p2.position[0]-p1.position[0])<35.0f)break;
            raw[0].stickX=p2.position[0]>p1.position[0]?80:-80;tick();
        }
        raw[0].stickX=0;
        check(std::abs(match.player_stats(1).position[0]-match.player_stats(0).position[0])<35.0f,
              "Raw input did not bring the selected fighter within laser range");
        check(match.player_stats(0).ground_or_air==0,"Selected fighter left the ground before the laser check");
        const auto damage=match.player_stats(1).damage_percent;
        for(unsigned n=0;n<240&&match.player_stats(1).damage_percent==damage;n++){
            raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
        }
        raw[0].button=0;
        check(match.player_stats(1).damage_percent>damage,"Selected fighter ground laser did not damage the opponent");
        std::cout<<fighter_content->name<<" laser damage="<<match.player_stats(1).damage_percent<<std::endl;
        if(cycle==0){
            bool jab1=false,jab2=false,rapid=false,rapid_end=false;
            for(unsigned n=0;n<180&&!rapid;n++){
                raw[0].button=n%2==0?PAD_BUTTON_A:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                jab1|=motion==44;jab2|=motion==45;
                rapid|=motion==47||motion==48;
            }
            raw[0].button=0;
            for(unsigned n=0;n<180&&!rapid_end;n++){
                tick();rapid_end=match.player_stats(0).motion_id==49;
            }
            check(jab1&&jab2&&rapid&&rapid_end,
                  "Fox-family jab input did not complete source Attack11/12/100 start-loop-end motions");
        }
        for(unsigned n=0;n<120&&match.player_stats(0).ground_or_air==0;n++){
            raw[0].button=n%8==0?PAD_BUTTON_X:0;tick();
        }
        raw[0].button=0;
        check(match.player_stats(0).ground_or_air==1,"Selected fighter did not jump");
        for(unsigned n=0;n<100;n++){raw[0].button=n<20?PAD_BUTTON_B:0;tick();}
        if(cycle==0){
            for(unsigned n=0;n<300&&match.player_stats(0).ground_or_air!=0;n++)tick();
            check(match.player_stats(0).ground_or_air==0,"Selected fighter did not land after the air laser");
            bool reflector=false;
            for(unsigned n=0;n<180&&!reflector;n++){
                raw[0].stickY=-80;raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                reflector=motion>=360&&motion<=369;
            }
            check(reflector,"Reflector did not enter its original special state");
            raw[0].stickY=0;raw[0].button=0;
            for(unsigned n=0;n<300;n++){
                const auto state=match.player_stats(0);
                if(state.ground_or_air==0&&(state.motion_id<341||state.motion_id>369))break;
                tick();
            }
            bool phantasm=false;
            for(unsigned n=0;n<180&&!phantasm;n++){
                raw[0].stickX=80;raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                phantasm=motion>=347&&motion<=352;
            }
            check(phantasm,"Side special did not enter its original source state");
            raw[0].stickX=0;raw[0].button=0;
            for(unsigned n=0;n<300;n++){
                const auto motion=match.player_stats(0).motion_id;
                if(motion<341||motion>369)break;
                tick();
            }
            bool fire_bird=false;
            for(unsigned n=0;n<180&&!fire_bird;n++){
                raw[0].stickY=80;raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                fire_bird=motion>=353&&motion<=359;
            }
            check(fire_bird,"Up special did not enter its original source state");
            raw[0].stickY=0;raw[0].button=0;
        }
        }
        raw[0].button=PAD_BUTTON_START;tick();raw[0].button=0;
        for(unsigned n=0;n<30&&!match.paused();n++)tick();
        check(match.paused(),"Source pause did not engage");
        for(unsigned n=0;n<15;n++)tick();
        raw[0].button=PAD_TRIGGER_L|PAD_TRIGGER_R|PAD_BUTTON_A|PAD_BUTTON_START;tick();raw[0].button=0;
        for(unsigned n=0;n<500&&!match.complete();n++)tick();
        check(match.complete(),"Original No Contest did not end the mixed match");
        match.close();match.close();
    }
    std::cout<<"Mixed source content intro, costumes, stage lifecycle, combat, pause and repeat teardown passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
