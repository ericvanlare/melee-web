// Explicit source-start fixtures for new content. This executes the real match
// lifecycle but does not claim CSS input, rendered output or retail equivalence.
#include "gameplay_match_session.hpp"
#include "gameplay_menu.h"
#include "gameplay_content.h"
#include "gameplay_pad_state.h"
#include <melee/ft/kinds/ftMario/forward.h>
#include <melee/ft/kinds/ftMars/forward.h>
#include <melee/ft/kinds/ftCaptain/forward.h>
#include <melee/ft/kinds/ftDonkey/forward.h>
#include <melee/ft/kinds/ftKoopa/forward.h>
#include <melee/ft/kinds/ftNess/forward.h>
#include <melee/ft/kinds/ftPeach/forward.h>
#include <melee/ft/kinds/ftMewtwo/forward.h>
#include <melee/ft/kinds/ftLuigi/forward.h>
#include <melee/ft/kinds/ftPikachu/forward.h>
#include <melee/ft/kinds/ftPurin/forward.h>
#include <melee/ft/kinds/ftGameWatch/forward.h>
#include <melee/ft/kinds/ftKirby/forward.h>
#include <melee/ft/kinds/ftPopo/forward.h>
#include <melee/ft/kinds/ftSamus/forward.h>
#include <melee/ft/kinds/ftYoshi/forward.h>
#include <melee/ft/kinds/ftZelda/forward.h>
#include <melee/ft/kinds/ftSeak/forward.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/it/forward.h>
extern "C" {
#include <melee/gm/forward.h>
#include <melee/pl/forward.h>
}
#include <filesystem>
#include <fstream>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <optional>
#include <string>

extern "C" int melee_web_test_content_player(unsigned,int,int,unsigned);
extern "C" int melee_web_test_entity_state(unsigned,unsigned,int*,int*,int*,int*);
extern "C" int melee_web_test_entity_position(unsigned,unsigned,float*,float*);
extern "C" int melee_web_test_entity_damage(unsigned,unsigned,float*);
extern "C" int melee_web_test_active_fighter_kind(unsigned);
extern "C" int melee_web_test_kirby_copy_kind(unsigned);
extern "C" int melee_web_test_fighter_owns_victim(unsigned,unsigned);
extern "C" int melee_web_test_donkey_cargo(unsigned,int);
extern "C" int melee_web_test_koopa_capture(unsigned,int);
extern "C" int melee_web_test_item_count(int);
extern "C" int melee_web_test_purin_anim_id(int);
extern "C" int melee_web_test_quake_start(int);
extern "C" int melee_web_test_quake_translated(void);
extern "C" int melee_web_story_state(uint32_t*,unsigned*,int*,int*,int*,int*);
static void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
using PadStateOwner=std::unique_ptr<MeleeWebPadState,decltype(&melee_web_pad_state_free)>;
static PadStateOwner zelda_sheik_transform_input(){
    std::array<uint8_t,MELEE_WEB_PAD_STATE_BYTES> bytes{};
    const auto put_u32=[&](size_t offset,uint32_t value){
        for(unsigned byte=0;byte<4;++byte)
            bytes[offset+byte]=static_cast<uint8_t>(value>>(24-8*byte));
    };
    // Source match PAD setup: default repeat/dead-zone values with the exact
    // per-match stick and analog-L/R overrides from gameplay_match_context.c.
    put_u32(0,45);put_u32(4,8);bytes[9]=30;
    bytes[16]=80;bytes[18]=1;bytes[19]=140;bytes[22]=255;
    bytes[24]=80;bytes[25]=bytes[26]=255;
    for(unsigned bank=0;bank<3;++bank)for(unsigned slot=0;slot<4;++slot)
        bytes[30+(bank*4+slot)*66+64]=1;
    constexpr size_t copy_status=30+4*66;
    constexpr uint32_t button=PAD_BUTTON_A;
    put_u32(copy_status,button);
    char error[128]{};
    PadStateOwner state(melee_web_pad_state_decode(bytes.data(),bytes.size(),error,sizeof(error)),
                        melee_web_pad_state_free);
    check(bool(state),error);
    return state;
}
int main(int argc,char** argv){try{
    if(argc<3||argc>7)throw std::runtime_error("Expected owned menu/game directories and optional StKind/P1 CKind/P2 CKind/trace scope");
    const bool entry_only=argc==7&&std::string(argv[6])=="--entry-only";
    const bool platform_pass=argc==7&&std::string(argv[6])=="--platform-pass";
    const bool action_coverage=argc==7&&std::string(argv[6])=="--character-actions";
    if(argc==7&&!entry_only&&!platform_pass&&!action_coverage)throw std::runtime_error("Unknown source match trace scope");
    melee_web::RuntimeFiles files;
    for(const auto* root:{argv[1],argv[2]})for(const auto& entry:std::filesystem::directory_iterator(root)){
        if(!entry.is_regular_file())continue;
        const auto name=entry.path().filename().string();
        if(entry.file_size()>64*1024*1024)continue;
        std::ifstream stream(entry.path(),std::ios::binary);
        files[name]={(std::istreambuf_iterator<char>(stream)),{}};
    }
    // The source Captain registry intentionally stores PlCaRe.  Both source
    // language owners are explicit: a US setting selects .usd while a saved
    // non-US language selects .dat. A missing selected locale must remain a
    // hard boundary even when the other suffix is present.
    using melee_web::DatMenuSupportLanguage;
    melee_web::RuntimeFiles resolver_files{{"PlCaRe.dat",{1}},{"PlCaRe.usd",{1}},
                                            {"PlMsNr.dat",{1}}};
    check(melee_web::melee_web_runtime_file_name(
              resolver_files,"PlCaRe.",DatMenuSupportLanguage::English,
              DatMenuSupportLanguage::English)=="PlCaRe.usd",
          "Source trailing-dot filename did not select the US setting archive");
    check(melee_web::melee_web_runtime_file_name(
              resolver_files,"PlCaRe.",DatMenuSupportLanguage::Other,
              DatMenuSupportLanguage::Other)=="PlCaRe.dat",
          "Source trailing-dot filename did not select the non-US setting archive");
    check(melee_web::melee_web_runtime_file_name(
              resolver_files,"PlCaRe.",DatMenuSupportLanguage::English,
              DatMenuSupportLanguage::Other)=="PlCaRe.usd",
          "Source trailing-dot filename used saved locale instead of setting locale");
    melee_web::RuntimeFiles only_dat{{"PlCaRe.dat",{1}}};
    check(melee_web::melee_web_runtime_file_name(
              only_dat,"PlCaRe.",DatMenuSupportLanguage::English,
              DatMenuSupportLanguage::English).empty(),
          "Source trailing-dot filename silently fell back to another locale");
    check(melee_web::melee_web_runtime_file_name(resolver_files,"PlMsNr.dat")=="PlMsNr.dat",
          "Exact source filename resolution changed");
    char error[256]{};
    MeleeWebMenuRuntime services{nullptr,
        [](void*,MeleeWebMenuScene,char*,size_t){return 1;},
        [](void*,char*,size_t){return 1;},
        [](void*,MeleeWebMenuScene,int*,char*,size_t){return 1;}};
    auto* menu=melee_web_menu_session_create(&services,nullptr,error,sizeof(error));check(menu,error);
    MeleeWebMenuMatchSelection selection{};
    const VsModeData raw=melee_web_menu_css(menu)->vs;
    selection.start=raw.start;
    selection.start.rules.match_kind=MatchKind_Stock;
    selection.start.rules.is_stock=true;
    selection.start.rules.is_vs=true;
    selection.start.rules.xB=-1;
    for(unsigned i=0;i<GM_MAX_PLAYERS;i++){
        selection.start.players[i].stocks=4;
        selection.start.players[i].rumble_enabled=i<2;
    }
    check(melee_web_menu_session_destroy(menu,error,sizeof(error)),error);
    selection.hud_layout=2;selection.start.rules.x0_3=2;selection.random_seed=0x13579bdf;
    selection.start.rules.stkind=argc>=4?std::stoi(argv[3]):St_Kind_Last;
    const int fighter_ckind=argc>=5?std::stoi(argv[4]):CKIND_FALCO;
    const auto* fighter_content=melee_web_fighter_content(fighter_ckind);
    check(fighter_content,"Selected fighter has no admitted source content");
    const int opponent_ckind=argc>=6?std::stoi(argv[5]):CKIND_MARIO;
    const auto* opponent_content=melee_web_fighter_content(opponent_ckind);
    check(opponent_content,"Selected opponent has no admitted source content");
    selection.start.players[0].ckind=fighter_ckind;
    selection.start.players[1].ckind=opponent_ckind;
    const bool ice_action_case=action_coverage&&fighter_ckind==CKIND_POPONANA;
    if(ice_action_case){
        auto& opponent=selection.start.players[1];
        opponent.slot_type=Gm_PKind_Cpu;opponent.cpu_kind=4;opponent.cpu_level=9;
        opponent.rumble_enabled=0;
    }
    const bool kirby_action_case=action_coverage&&fighter_ckind==CKIND_KIRBY;
    if(kirby_action_case){
        check(opponent_ckind==CKIND_GAMEWATCH,
              "Kirby copy action probe requires Game & Watch as its scoped donor family");
    }
    const unsigned costume_cycles=action_coverage?1:(fighter_content->costumes>opponent_content->costumes?
        fighter_content->costumes:opponent_content->costumes);
    for(unsigned cycle=0;cycle<costume_cycles;cycle++){
        const unsigned fighter_color=cycle%fighter_content->costumes;
        selection.start.players[0].color=fighter_color;
        const unsigned opponent_color=cycle%opponent_content->costumes;
        selection.start.players[1].color=opponent_color;
        selection.players[0]={0,4,fighter_color,0};
        selection.players[1]={1,4,opponent_color,0};
        const bool transformation_form=fighter_ckind==CKIND_ZELDA||fighter_ckind==CKIND_SEAK;
        const bool startup_transform=transformation_form&&!action_coverage;
        std::cout<<"Construct mixed content stage="<<selection.start.rules.stkind<<" costume="<<cycle<<std::endl;
        std::optional<melee_web::GameplayMatchSession> match_owner;
        if(startup_transform){
            auto initial_input=zelda_sheik_transform_input();
            match_owner.emplace(files,selection,*initial_input);
        }else match_owner.emplace(files,selection);
        auto& match=*match_owner;
        const unsigned match_player_count=2U;
        PADStatus raw[4]{};if(match_player_count<3)raw[2].err=PAD_ERR_NO_CONTROLLER;
        if(ice_action_case)raw[1].err=PAD_ERR_NO_CONTROLLER;
        raw[3].err=PAD_ERR_NO_CONTROLLER;
        float pcm[1068];unsigned phase=0;
        auto tick=[&](){
            match.tick(raw);phase+=32000;const auto count=phase/60;phase%=60;
            check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
            for(unsigned i=0;i<match_player_count;i++){const auto state=match.player_stats(i);
                check(std::isfinite(state.position[0])&&std::isfinite(state.position[1]),"Nonfinite fighter state");}};
        for(unsigned n=0;!match.ready()&&n<600;n++)tick();
        check(match.ready(),"Original Ready did not finish");
        const int active_fighter_ckind=startup_transform?
            (fighter_ckind==CKIND_ZELDA?CKIND_SEAK:CKIND_ZELDA):fighter_ckind;
        const auto* active_fighter_content=melee_web_fighter_content(active_fighter_ckind);
        check(active_fighter_content&&melee_web_test_content_player(0,active_fighter_ckind,
              active_fighter_content->fighter_kind,fighter_color),
              "Original selected-fighter identity/costume/icon differs after source startup transform");
        if(startup_transform)
            std::cout<<"Source held-A startup transformed "<<fighter_content->name<<" to "
                     <<active_fighter_content->name<<" before Fighter_Create"<<std::endl;
        if(fighter_ckind==CKIND_POPONANA){
            int primary_kind=-1,partner_kind=-1,primary_motion=-1,partner_motion=-1;
            int primary_grounded=0,partner_grounded=0,primary_skeleton=0,partner_skeleton=0;
            check(melee_web_test_entity_state(0,0,&primary_kind,&primary_motion,&primary_grounded,&primary_skeleton)&&
                  melee_web_test_entity_state(0,1,&partner_kind,&partner_motion,&partner_grounded,&partner_skeleton)&&
                  primary_kind==FTKIND_POPO&&partner_kind==FTKIND_NANA&&primary_skeleton&&partner_skeleton,
                  "Ice Climbers source player must own distinct Popo and Nana fighter entities");
            std::cout<<"Ice pair primary="<<primary_kind<<"/"<<primary_motion<<"/"<<primary_grounded
                     <<" partner="<<partner_kind<<"/"<<partner_motion<<"/"<<partner_grounded<<std::endl;
        }
        check(melee_web_test_content_player(1,opponent_ckind,opponent_content->fighter_kind,opponent_color),"Original opponent identity/costume/icon differs");
        check(match.player_stats(0).stocks==4&&match.player_stats(1).stocks==4,
              "Source stock initialization changed for the selected content pair");
        if(action_coverage){
            auto neutral=[&](){for(unsigned i=0;i<4;i++){
                raw[i].button=0;raw[i].stickX=raw[i].stickY=0;}};
            auto settle_primary=[&](){
                neutral();
                for(unsigned n=0;n<720;n++){
                    const auto state=match.player_stats(0);
                    if(state.ground_or_air==0&&state.motion_id==ftCo_MS_Wait)return;
                    tick();
                }
                const auto state=match.player_stats(0);
                check(false,"Action coverage did not return the selected fighter to grounded Wait; motion="+
                      std::to_string(state.motion_id)+" ground_or_air="+
                      std::to_string(state.ground_or_air)+" active_kind="+
                      std::to_string(melee_web_test_active_fighter_kind(0))+" stocks="+
                      std::to_string(state.stocks)+" damage="+
                      std::to_string(state.damage_percent)+" position="+
                      std::to_string(state.position[0])+","+
                      std::to_string(state.position[1]));
            };
            if(fighter_ckind==CKIND_GAMEWATCH){
                bool chef=false,sausage=false;
                for(unsigned n=0;n<180&&!(chef&&sausage);n++){
                    raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                    chef|=match.player_stats(0).motion_id==ftGw_MS_SpecialN;
                    sausage|=melee_web_test_item_count(It_Kind_GameWatch_Chef)>0;
                }
                neutral();
                check(chef&&sausage,"Game & Watch Chef did not enter its source motion and create a sausage");
                for(unsigned n=0;n<600&&melee_web_test_item_count(It_Kind_GameWatch_Chef)>0;n++)tick();
                check(melee_web_test_item_count(It_Kind_GameWatch_Chef)==0,
                      "Game & Watch Chef article did not clear through its source lifetime");
                std::cout<<"Game & Watch action coverage: Chef motion/article lifetime passed; other specials unverified"<<std::endl;
            }else if(fighter_ckind==CKIND_SAMUS){
                bool bomb=false,down_motion=false;
                for(unsigned n=0;n<120&&!bomb;n++){
                    raw[0].stickY=-80;raw[0].button=n==0?PAD_BUTTON_B:0;tick();
                    const int motion=match.player_stats(0).motion_id;
                    down_motion|=motion==ftSs_MS_SpecialLw||motion==ftSs_MS_SpecialAirLw||
                                 motion==ftSs_MS_SpecialLwBomb||motion==ftSs_MS_SpecialAirLwBomb;
                    bomb|=melee_web_test_item_count(It_Kind_Samus_Bomb)>0;
                }
                neutral();
                check(down_motion&&bomb,"Samus down-B did not enter its source action and create a bomb");
                for(unsigned n=0;n<900&&melee_web_test_item_count(It_Kind_Samus_Bomb)>0;n++)tick();
                check(melee_web_test_item_count(It_Kind_Samus_Bomb)==0,
                      "Samus bomb did not clear through its source lifetime");
                std::cout<<"Samus action coverage: down-B Bomb article lifetime passed; other specials unverified"<<std::endl;
            }else if(fighter_ckind==CKIND_YOSHI){
                settle_primary();
                bool near_target=false;
                for(unsigned n=0;n<360;n++){
                    const auto self=match.player_stats(0),target=match.player_stats(1);
                    const float dx=target.position[0]-self.position[0];
                    const float dy=target.position[1]-self.position[1];
                    if(std::fabs(dx)<7.0f&&std::fabs(dy)<5.0f){near_target=true;break;}
                    raw[0].stickX=std::fabs(dx)<5.0f?0:(dx>0?80:-80);
                    raw[0].stickY=std::fabs(dy)<4.0f?0:(dy>0?60:-60);tick();
                }
                check(near_target,"Yoshi neutral-B case did not reach the target's collision spacing");
                neutral();settle_primary();
                bool egg_lay_motion=false,egg_lay_article=false;
                for(unsigned n=0;n<180&&!(egg_lay_motion&&egg_lay_article);n++){
                    raw[0].button=n==0?PAD_BUTTON_B:0;tick();
                    const int motion=match.player_stats(0).motion_id;
                    egg_lay_motion|=motion>=ftYs_MS_SpecialN1&&motion<=ftYs_MS_SpecialN2_1;
                    egg_lay_article|=melee_web_test_item_count(It_Kind_Yoshi_EggLay)>0;
                }
                neutral();
                check(egg_lay_motion,"Yoshi neutral-B did not enter an authored Egg Lay source motion");
                std::cout<<"Yoshi action coverage: neutral-B Egg Lay source motion passed; victim capture/article="
                         <<(egg_lay_article?"observed":"not observed")
                         <<"; other specials unverified"<<std::endl;
            }else if(fighter_ckind==CKIND_ZELDA||fighter_ckind==CKIND_SEAK){
                const int start_kind=fighter_ckind==CKIND_ZELDA?FTKIND_ZELDA:FTKIND_SEAK;
                check(melee_web_test_active_fighter_kind(0)==start_kind,
                      "Down-B test did not begin in the requested original fighter form");
                int current=start_kind;
                for(unsigned change=0;change<4;change++){
                    const int wanted=current==FTKIND_ZELDA?FTKIND_SEAK:FTKIND_ZELDA;
                    raw[0].stickY=-80;raw[0].button=PAD_BUTTON_B;tick();neutral();
                    bool down_motion=false,transformed=false;
                    for(unsigned n=0;n<240&&!transformed;n++){
                        const int motion=match.player_stats(0).motion_id;
                        down_motion|=motion==ftZd_MS_SpecialLw||motion==ftZd_MS_SpecialLw2||
                                     motion==ftSk_MS_SpecialLw||motion==ftSk_MS_SpecialLw2;
                        transformed=melee_web_test_active_fighter_kind(0)==wanted;
                        if(!transformed)tick();
                    }
                    if(!down_motion||!transformed)
                        std::cout<<"Zelda/Sheik transition miss from="<<current
                                 <<" wanted="<<wanted
                                 <<" active="<<melee_web_test_active_fighter_kind(0)
                                 <<" motion="<<match.player_stats(0).motion_id
                                 <<" down_motion="<<down_motion<<std::endl;
                    check(down_motion&&transformed,
                          "In-match down-B did not complete the requested Zelda/Sheik form change");
                    current=wanted;
                    std::cout<<"Zelda/Sheik down-B transformation="<<change+1
                             <<" active_kind="<<current<<std::endl;
                    // The source transformation leaves the active form in
                    // Common_Sleep, not the ordinary grounded Wait motion.
                    // Keep the match alive and verify ground/stock state;
                    // requiring Wait here prevents the repeated down-B probe
                    // from reaching its next source transition.
                    neutral();
                    for(unsigned n=0;n<90;n++)tick();
                    const auto transformed_state=match.player_stats(0);
                    check(transformed_state.ground_or_air==0&&
                          transformed_state.stocks==4,
                          "Zelda/Sheik transformation did not preserve the grounded four-stock fighter lifecycle");
                }
                check(current==start_kind&&match.player_stats(0).stocks==4,
                      "Repeated down-B transformation did not return to its starting form and stock");
                std::cout<<"Zelda/Sheik action coverage: both in-match down-B directions, repeated twice, passed; startup held-A is separate"<<std::endl;
            }else if(fighter_ckind==CKIND_POPONANA){
                float popo_x=0,popo_y=0,nana_x=0,nana_y=0;
                check(melee_web_test_entity_position(0,0,&popo_x,&popo_y)&&
                      melee_web_test_entity_position(0,1,&nana_x,&nana_y),
                      "Ice Climbers pair positions are unavailable");
                const float before=std::hypot(popo_x-nana_x,popo_y-nana_y);
                bool up_special=false;float farthest=before;
                for(unsigned n=0;n<180;n++){
                    raw[0].stickY=80;raw[0].button=n==0?PAD_BUTTON_B:0;tick();
                    int kind=-1,motion=-1,grounded=0,skeleton=0;
                    float px=0,py=0,nx=0,ny=0;
                    up_special|=melee_web_test_entity_state(0,0,&kind,&motion,&grounded,&skeleton)&&
                        motion>=ftPp_MS_SpecialHiStart_0&&motion<=ftPp_MS_SpecialHi_5;
                    if(melee_web_test_entity_position(0,0,&px,&py)&&
                       melee_web_test_entity_position(0,1,&nx,&ny))
                        farthest=std::max(farthest,std::hypot(px-nx,py-ny));
                }
                neutral();
                check(up_special&&farthest>before+3.0f,
                      "Ice Climbers Belay did not produce a source up-special and separated partner positions");
                std::cout<<"Ice Climbers action coverage: Belay separated Popo/Nana from "<<before
                         <<" to "<<farthest<<" units"<<std::endl;
                float max_partner_damage=0;
                bool partner_died=false,partner_removed=false,primary_survived=true;
                unsigned observed_ticks=0;
                for(;observed_ticks<7200&&!match.complete();observed_ticks++){
                    tick();
                    const auto primary=match.player_stats(0);
                    if(primary.stocks==0||melee_web_test_active_fighter_kind(0)!=FTKIND_POPO){
                        primary_survived=false;break;
                    }
                    int partner_kind=-1,partner_motion=-1,partner_grounded=0,partner_skeleton=0;
                    if(!melee_web_test_entity_state(0,1,&partner_kind,&partner_motion,
                                                    &partner_grounded,&partner_skeleton)){
                        partner_removed=true;break;
                    }
                    float damage=0;
                    if(melee_web_test_entity_damage(0,1,&damage))
                        max_partner_damage=std::max(max_partner_damage,damage);
                    partner_died=partner_motion>=ftCo_MS_DeadDown&&
                                 partner_motion<=ftCo_MS_DeadUpFallHitCameraIce;
                    if(partner_died)break;
                }
                bool primary_lifecycle=false,partner_present_after=false;
                int partner_motion_after=-1;
                if(partner_died||partner_removed){
                    for(unsigned n=0;n<180&&!match.complete();n++)tick();
                    int primary_kind=-1,primary_motion=-1,primary_grounded=0,primary_skeleton=0;
                    primary_lifecycle=melee_web_test_entity_state(0,0,&primary_kind,&primary_motion,
                                &primary_grounded,&primary_skeleton)&&
                                primary_kind==FTKIND_POPO&&match.player_stats(0).stocks>0;
                    int partner_kind=-1,partner_motion=-1,partner_grounded=0,partner_skeleton=0;
                    partner_present_after=melee_web_test_entity_state(0,1,&partner_kind,&partner_motion,
                                                    &partner_grounded,&partner_skeleton);
                    if(partner_present_after)partner_motion_after=partner_motion;
                }
                std::cout<<"Ice Climbers partner-lifecycle probe: source CPU9 opponent ticks="
                         <<observed_ticks<<" Nana max_damage="<<max_partner_damage
                         <<" death_motion="<<partner_died<<" entity_removed="<<partner_removed
                         <<" Popo_survived="<<primary_survived
                         <<" Popo_stock_and_entity_after="<<primary_lifecycle
                         <<" Nana_present_after="<<partner_present_after
                         <<" Nana_motion_after="<<partner_motion_after<<std::endl;
                check(partner_died||partner_removed,
                      "Ice Climbers CPU9 probe did not exercise Nana death or source entity removal");
                check(primary_survived&&primary_lifecycle&&(!partner_died||partner_present_after),
                      "Ice Climbers partner loss did not preserve the leader and complete its 180-tick follow-up lifecycle");
            }else if(kirby_action_case){
                const auto acquire=[&](unsigned target_slot,int donor_kind){
                    for(unsigned n=0;n<360;n++){
                        const auto self=match.player_stats(0),target=match.player_stats(target_slot);
                        const float dx=target.position[0]-self.position[0];
                        const float dy=target.position[1]-self.position[1];
                        if(std::fabs(dx)<7.0f&&std::fabs(dy)<5.0f)break;
                        raw[0].stickX=std::fabs(dx)<5.0f?0:(dx>0?80:-80);
                        raw[0].stickY=std::fabs(dy)<4.0f?0:(dy>0?60:-60);tick();
                    }
                    neutral();settle_primary();
                    bool captured=false;
                    for(unsigned n=0;n<240&&!captured;n++){
                        raw[0].button=n==0?PAD_BUTTON_B:0;tick();
                        const int target_motion=match.player_stats(target_slot).motion_id;
                        captured=melee_web_test_fighter_owns_victim(0,target_slot)||
                            target_motion==ftCo_MS_CaptureKirby||
                            target_motion==ftCo_MS_CaptureWaitKirby||
                            target_motion==ftCo_MS_ThrownKirbyStar||
                            target_motion==ftCo_MS_ThrownCopyStar||
                            target_motion==ftCo_MS_ThrownKirby;
                    }
                    neutral();
                    check(captured,"Kirby inhale did not reach the source capture/swallow transition for donor slot "+
                          std::to_string(target_slot));
                    for(unsigned n=0;n<360&&match.player_stats(0).motion_id!=ftKb_MS_EatWait;n++)tick();
                    check(match.player_stats(0).motion_id==ftKb_MS_EatWait,
                          "Kirby capture did not settle into EatWait; motion="+
                          std::to_string(match.player_stats(0).motion_id)+" target_motion="+
                          std::to_string(match.player_stats(target_slot).motion_id)+" owns_victim="+
                          std::to_string(melee_web_test_fighter_owns_victim(0,target_slot)));
                    raw[0].button=PAD_BUTTON_B;tick();neutral();
                    for(unsigned n=0;n<240&&melee_web_test_kirby_copy_kind(0)!=donor_kind;n++)tick();
                    check(melee_web_test_kirby_copy_kind(0)==donor_kind,
                          "Kirby EatWait B did not acquire the donor's source copy ability; motion="+
                          std::to_string(match.player_stats(0).motion_id)+" copy_kind="+
                          std::to_string(melee_web_test_kirby_copy_kind(0)));
                    std::cout<<"Kirby acquired donor FighterKind="<<donor_kind
                             <<" from slot="<<target_slot<<std::endl;
                };
                const auto use_copy=[&](int donor_kind){
                    bool copied_move=false;
                    for(unsigned n=0;n<180&&!copied_move;n++){
                        raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                        const int motion=match.player_stats(0).motion_id;
                        if(donor_kind==FTKIND_GAMEWATCH)
                            copied_move=motion==ftKb_MS_GwSpecialN||
                                melee_web_test_item_count(It_Kind_Kirby_GameWatchChefPan)>0;
                        else if(donor_kind==FTKIND_MARIO)
                            copied_move=motion==ftKb_MS_MrSpecialN||
                                melee_web_test_item_count(It_Kind_Mario_Fire)>0;
                        else if(donor_kind==FTKIND_SAMUS)
                            copied_move=motion>=ftKb_MS_SsSpecialNStart&&
                                motion<=ftKb_MS_SsSpecialN;
                    }
                    neutral();
                    check(copied_move,"Kirby did not execute the source neutral special for donor "+
                          std::to_string(donor_kind));
                };
                acquire(1,FTKIND_GAMEWATCH);use_copy(FTKIND_GAMEWATCH);
                bool lost=false;
                for(unsigned n=0;n<120&&!lost;n++){
                    raw[0].button=n==0?PAD_BUTTON_DOWN:0;tick();
                    lost=melee_web_test_kirby_copy_kind(0)==FTKIND_KIRBY;
                }
                neutral();
                check(lost,"Kirby down taunt did not lose the Game & Watch copy ability through its source path");
                acquire(1,FTKIND_GAMEWATCH);use_copy(FTKIND_GAMEWATCH);
                check(melee_web_test_kirby_copy_kind(0)==FTKIND_GAMEWATCH,
                      "Kirby did not replace its lost Game & Watch ability through the source path");
                std::cout<<"Kirby action coverage: Game & Watch acquire/use/loss/replacement and match teardown path passed; Mario, Samus and other donor effect families unverified"<<std::endl;
            }else{
                check(false,"--character-actions is only defined for the newly admitted source fighters");
            }
        }else if(platform_pass){
            check(selection.start.rules.stkind==St_Kind_Battle&&
                  opponent_content->fighter_kind==FTKIND_DONKEY,
                  "Platform Pass probe requires Battlefield with Donkey in P2");
            // Source Battlefield slot 1 is the upper platform. Guard + down
            // enters original Pass without changing the Fighter or collision.
            const auto initial=match.player_stats(1);
            std::cout<<"Donkey platform start x="<<initial.position[0]
                     <<" y="<<initial.position[1]<<" motion="<<initial.motion_id<<std::endl;
            raw[1].button=PAD_TRIGGER_L;tick();
            bool entered=false;
            for(unsigned n=0;n<8&&!entered;n++){
                // Source x464 is .66, while earlier spot-dodge IASA uses
                // x314=-.70. Raw -54/80 lies between those authored gates.
                raw[1].button=PAD_TRIGGER_L;raw[1].stickY=-54;tick();
                std::cout<<"Donkey platform input frame="<<n<<" motion="
                         <<match.player_stats(1).motion_id<<std::endl;
                entered=match.player_stats(1).motion_id==ftCo_MS_Pass;
            }
            raw[1].button=0;raw[1].stickY=0;
            check(entered,"Battlefield raw shield/down did not enter Donkey Pass");
            unsigned pass_ticks=0;
            for(;pass_ticks<40&&match.player_stats(1).motion_id==ftCo_MS_Pass;pass_ticks++)tick();
            check(match.player_stats(1).motion_id!=ftCo_MS_Pass,
                  "Donkey source Pass did not finish its animation lifetime");
            std::cout<<"Donkey original Pass exited after "<<pass_ticks
                     <<" ticks, motion="<<match.player_stats(1).motion_id<<std::endl;
        }else if(entry_only){
            // Stage entry has its own scope: do not reuse the FD combat
            // positioning recipe on geometry where that walk leaves a ledge.
            for(unsigned n=0;n<60;n++)tick();
            check(match.player_stats(0).stocks==4&&match.player_stats(1).stocks==4,
                  "Source entry-only window changed initial stocks");
        }else if(selection.start.rules.stkind==St_Kind_Story){
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
        const bool purin=fighter_content->fighter_kind==FTKIND_PURIN;
        if(purin){
            // Keep the source FD placement constraints used by the other
            // fighter branches, then drive Purin from raw controller edges.
            for(unsigned n=0;n<180;n++){
                const auto p1=match.player_stats(0),p2=match.player_stats(1);
                if(std::abs(p2.position[1]-p1.position[1])<5.0f)break;
                raw[1].stickY=-80;tick();
            }
            raw[1].stickY=0;
            check(std::abs(match.player_stats(1).position[1]-match.player_stats(0).position[1])<5.0f,
                  "Raw input did not bring Purin's opponent to the same stage level");
            for(unsigned n=0;n<120;n++){
                const auto p1=match.player_stats(0),p2=match.player_stats(1);
                if(std::abs(p2.position[0]-p1.position[0])<35.0f)break;
                raw[0].stickX=p2.position[0]>p1.position[0]?80:-80;tick();
            }
            raw[0].stickX=0;
            check(std::abs(match.player_stats(1).position[0]-match.player_stats(0).position[0])<35.0f &&
                  match.player_stats(0).ground_or_air==0,
                  "Raw input did not place Purin in the authored ground-special range");

            if(cycle==0){
            auto settle_purin=[&](){
                raw[0].button=0;raw[0].stickX=raw[0].stickY=0;
                for(unsigned n=0;n<720;n++){
                    const auto state=match.player_stats(0);
                    if(state.ground_or_air==0&&state.motion_id==ftCo_MS_Wait)return;
                    tick();
                }
                check(false,"Purin special did not return to a grounded common motion");
            };
            auto jump_purin=[&](){
                raw[0].stickX=raw[0].stickY=0;raw[0].button=PAD_BUTTON_X;tick();
                raw[0].button=0;
                for(unsigned n=0;n<120&&match.player_stats(0).ground_or_air==0;n++)tick();
                check(match.player_stats(0).ground_or_air!=0,
                      "Purin did not enter the authored aerial state");
            };
            auto jump_high_purin=[&](){
                raw[0].stickX=raw[0].stickY=0;raw[0].button=PAD_BUTTON_X;tick();tick();
                raw[0].button=0;
                for(unsigned n=0;n<10;n++)tick();
                bool fifth=false;
                for(unsigned n=0;n<180&&!fifth;n++){
                    raw[0].button=PAD_BUTTON_X;tick();
                    fifth=match.player_stats(0).motion_id==ftPr_MS_JumpAerialF5;
                }
                raw[0].button=0;
                // JumpAerial IASA checks air specials before the jump gate.
                // Enter at the fifth jump, while rising; waiting for its
                // animation to finish loses the altitude needed to charge.
                check(fifth&&match.player_stats(0).ground_or_air!=0,
                      "Purin raw multi-jump input did not retain an aerial state");
            };

            // x28 is the source WaitStruct {(31,80),(32,20),(-1,-1)}. Hold
            // crouch until the original callback selects animation 32; the
            // fixture observes Fighter.anim_id and never substitutes a state.
            bool crouch_anim32=false;
            raw[0].stickY=-80;
            for(unsigned n=0;n<720&&!crouch_anim32;n++){
                tick();crouch_anim32|=melee_web_test_purin_anim_id(32)!=0;
            }
            raw[0].stickY=0;
            check(crouch_anim32,"Purin crouch Wait did not observe source anim_id 32");
            settle_purin();

            // The first ground jump is common; holding X after the source's
            // grounded grace period must visit JumpAerialF1..F5.  The held
            // input is required by ftCo_800D730C for the later jumps.
            bool aerial_jumps[5]={false,false,false,false,false};
            bool aerial_jump_logged[5]={false,false,false,false,false};
            raw[0].button=PAD_BUTTON_X;tick();tick();raw[0].button=0;
            for(unsigned n=0;n<10;n++)tick();
            for(unsigned n=0;n<180;n++){
                raw[0].button=PAD_BUTTON_X;tick();
                const auto motion=match.player_stats(0).motion_id;
                for(unsigned jump=0;jump<5;jump++){
                    aerial_jumps[jump]|=motion==ftPr_MS_JumpAerialF1+(int)jump;
                    if(aerial_jumps[jump]&&!aerial_jump_logged[jump]){
                        aerial_jump_logged[jump]=true;
                        std::cout<<"Purin aerial jump state="<<motion<<" frame="<<n
                                 <<" y="<<match.player_stats(0).position[1]<<std::endl;
                    }
                }
                if(aerial_jumps[0]&&aerial_jumps[1]&&aerial_jumps[2]&&
                   aerial_jumps[3]&&aerial_jumps[4])break;
            }
            raw[0].button=0;
            for(unsigned jump=0;jump<5;jump++)
                check(aerial_jumps[jump],"Purin aerial jump state was not observed");
            settle_purin();

            bool rollout_start=false,rollout_full=false,rollout_release=false;
            for(unsigned n=0;n<300&&!rollout_release;n++){
                raw[0].button=n<150?PAD_BUTTON_B:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                rollout_start|=motion==ftPr_MS_SpecialNStartR||motion==ftPr_MS_SpecialNStartL||
                    motion==ftPr_MS_SpecialNLoop;
                rollout_full|=motion==ftPr_MS_SpecialNFull;
                rollout_release|=motion==ftPr_MS_SpecialNRelease;
            }
            raw[0].button=0;
            check(rollout_start&&rollout_full&&rollout_release,
                  "Purin ground Rollout did not preserve its authored charge/release states");
            settle_purin();

            bool air_rollout_start=false,air_rollout_full=false,air_rollout_release=false;
            bool air_rollout_full_seen=false;
            jump_high_purin();
            int air_rollout_previous=-1;
            for(unsigned n=0;n<240&&!air_rollout_release;n++){
                // Charge starts at xA0, then advances by xA8 up to xA4.
                // Release only after observing ChargeFull, so the
                // source IASA callback enters ChargeRelease naturally.
                raw[0].button=air_rollout_full_seen?0:PAD_BUTTON_B;tick();
                const auto motion=match.player_stats(0).motion_id;
                const bool aerial=match.player_stats(0).ground_or_air!=0;
                if(motion!=air_rollout_previous) {
                    air_rollout_previous=motion;
                    std::cout<<"Purin air Rollout state="<<motion<<" frame="<<n
                             <<" aerial="<<aerial<<" y="<<match.player_stats(0).position[1]
                             <<std::endl;
                }
                air_rollout_start|=aerial&&(motion==ftPr_MS_SpecialAirNStartR||
                    motion==ftPr_MS_SpecialAirNStartL||
                    motion==ftPr_MS_SpecialAirNChargeLoop);
                air_rollout_full|=aerial&&motion==ftPr_MS_SpecialAirNChargeFull;
                air_rollout_release|=aerial&&motion==ftPr_MS_SpecialAirNChargeRelease;
                air_rollout_full_seen|=air_rollout_full;
            }
            raw[0].button=0;
            check(air_rollout_start&&air_rollout_full&&air_rollout_release,
                  "Purin aerial Rollout did not preserve its authored charge/full/release states");
            settle_purin();

            auto run_simple_special=[&](bool air,int ground_left,int ground_right,
                                        int air_left,int air_right,int stick_x,int stick_y,
                                        const char* failure){
                if(air)jump_purin();else settle_purin();
                bool entered=false;
                for(unsigned n=0;n<240&&!entered;n++){
                    raw[0].stickX=stick_x;raw[0].stickY=stick_y;
                    raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                    const auto motion=match.player_stats(0).motion_id;
                    entered|=motion==(air?air_left:ground_left)||
                        motion==(air?air_right:ground_right);
                }
                raw[0].button=0;raw[0].stickX=raw[0].stickY=0;
                check(entered,failure);settle_purin();
            };
            run_simple_special(false,ftPr_MS_SpecialS,ftPr_MS_SpecialS,
                               ftPr_MS_SpecialAirS,ftPr_MS_SpecialAirS,80,0,
                               "Purin ground Pound did not enter its original source state");
            run_simple_special(true,ftPr_MS_SpecialS,ftPr_MS_SpecialS,
                               ftPr_MS_SpecialAirS,ftPr_MS_SpecialAirS,80,0,
                               "Purin aerial Pound did not enter its original source state");
            run_simple_special(false,ftPr_MS_SpecialHiL,ftPr_MS_SpecialHiR,
                               ftPr_MS_SpecialAirHiL,ftPr_MS_SpecialAirHiR,0,80,
                               "Purin ground Sing did not enter its original source state");
            run_simple_special(true,ftPr_MS_SpecialHiL,ftPr_MS_SpecialHiR,
                               ftPr_MS_SpecialAirHiL,ftPr_MS_SpecialAirHiR,0,80,
                               "Purin aerial Sing did not enter its original source state");
            run_simple_special(false,ftPr_MS_SpecialLwL,ftPr_MS_SpecialLwR,
                               ftPr_MS_SpecialAirLwL,ftPr_MS_SpecialAirLwR,0,-80,
                               "Purin ground Rest did not enter its original source state");
            run_simple_special(true,ftPr_MS_SpecialLwL,ftPr_MS_SpecialLwR,
                               ftPr_MS_SpecialAirLwL,ftPr_MS_SpecialAirLwR,0,-80,
                               "Purin aerial Rest did not enter its original source state");
            std::cout<<"Purin original Wait anim_id32, five aerial jumps, Rollout charge/release, and ground/air Pound/Sing/Rest states passed"<<std::endl;
            }
        }else{
        const bool fox_family=fighter_content->fighter_kind==FTKIND_FOX||
            fighter_content->fighter_kind==FTKIND_FALCO;
        const bool doctor=fighter_content->fighter_kind==FTKIND_DRMARIO;
        const bool captain=fighter_content->fighter_kind==FTKIND_CAPTAIN;
        const bool ganon=fighter_content->fighter_kind==FTKIND_GANON;
        const bool donkey=fighter_content->fighter_kind==FTKIND_DONKEY;
        const bool koopa=fighter_content->fighter_kind==FTKIND_KOOPA;
        const bool ness=fighter_content->fighter_kind==FTKIND_NESS;
        const bool peach=fighter_content->fighter_kind==FTKIND_PEACH;
        const bool mewtwo=fighter_content->fighter_kind==FTKIND_MEWTWO;
        const bool luigi=fighter_content->fighter_kind==FTKIND_LUIGI;
        const bool pikachu_family=fighter_content->fighter_kind==FTKIND_PIKACHU||
            fighter_content->fighter_kind==FTKIND_PICHU;
        const bool pichu=fighter_content->fighter_kind==FTKIND_PICHU;
        const bool mars_family=fighter_content->fighter_kind==FTKIND_MARS||
            fighter_content->fighter_kind==FTKIND_EMBLEM;
        // Authored versus spawns may put P2 on a platform. Drop that source
        // Fighter to P1's level before checking their real attack paths.
        for(unsigned n=0;n<180;n++){
            const auto p1=match.player_stats(0),p2=match.player_stats(1);
            if(std::abs(p2.position[1]-p1.position[1])<5.0f)break;
            raw[1].stickY=-80;tick();
        }
        raw[1].stickY=0;
        check(std::abs(match.player_stats(1).position[1]-match.player_stats(0).position[1])<5.0f,
              "Raw input did not bring both fighters to the same stage level");
        // Ness's released PK Flash detonates where the flash spawned instead
        // of traveling, so the opponent must stand inside the explosion.
        const float attack_distance=(ganon||captain||donkey||ness)?16.0f:35.0f;
        for(unsigned n=0;n<120;n++){
            const auto p1=match.player_stats(0),p2=match.player_stats(1);
            if(std::abs(p2.position[0]-p1.position[0])<attack_distance)break;
            raw[0].stickX=p2.position[0]>p1.position[0]?80:-80;tick();
        }
        raw[0].stickX=0;
        check(std::abs(match.player_stats(1).position[0]-match.player_stats(0).position[0])<attack_distance,
              "Raw input did not bring the selected fighter within neutral-special range");
        check(match.player_stats(0).ground_or_air==0,"Selected fighter left the ground before the laser check");
            // Confusion capture: the source side special is a command grab
            // on contact (ftCo_800BCF18 -> CaptureMewtwo), and the victim
            // enters the common ThrownMewtwo submotions. A staging crash
            // report showed the player stopping exactly on this capture,
            // whose command rows the action store had never admitted.
            if(mewtwo){
                raw[0].button=0;raw[0].stickX=raw[0].stickY=0;
                for(unsigned n=0;n<720;n++){
                    const auto state=match.player_stats(0);
                    if(state.ground_or_air==0&&state.motion_id==14)break;
                    tick();
                }
                // Close to contact range so the Confusion hit connects, then
                // stop: the capture is a command grab on contact.
                for(unsigned n=0;n<120;n++){
                    const auto p1=match.player_stats(0),p2=match.player_stats(1);
                    if(std::abs(p2.position[0]-p1.position[0])<16.0f)break;
                    raw[0].stickX=p2.position[0]>p1.position[0]?80:-80;tick();
                }
                raw[0].stickX=0;
                // The dash momentum must settle before the side special edge
                // registers; a running capture input was consumed by the run
                // state in the failing staging flow.
                for(unsigned n=0;n<120;n++){
                    if(match.player_stats(0).motion_id==14)break;
                    tick();
                }
                std::cout<<"Mewtwo capture pre-B p1="<<match.player_stats(0).motion_id
                    <<" x="<<match.player_stats(0).position[0]
                    <<" p2="<<match.player_stats(1).motion_id
                    <<" p2x="<<match.player_stats(1).position[0]<<std::endl;
                raw[0].button=PAD_BUTTON_B;
                raw[0].stickX=match.player_stats(1).position[0]>match.player_stats(0).position[0]?80:-80;
                tick();
                raw[0].button=0;raw[0].stickX=0;
                bool entered=false,captured=false;
                for(unsigned n=0;n<240&&!(entered&&captured);n++){
                    tick();
                    entered|=match.player_stats(0).motion_id==ftMt_MS_SpecialS;
                    captured|=match.player_stats(1).motion_id==ftCo_MS_ThrownMewtwo;
                }
                check(entered,"Mewtwo capture recipe did not enter its side special");
                check(captured,"Mewtwo Confusion hit did not run its source capture on the victim");
                // Tick through the source capture lifetime and release.
                for(unsigned n=0;n<400;n++){
                    tick();
                    if(match.player_stats(1).motion_id!=ftCo_MS_ThrownMewtwo)break;
                }
                /* The release leaves the victim launched; settle both fighters
                 * so the recipes that follow observe a grounded idle
                 * opponent, as their own positioning already re-closes the
                 * range. */
                raw[1].button=0;raw[1].stickX=0;
                for(unsigned n=0;n<120;n++){raw[1].stickY=-80;tick();}
                raw[1].stickY=0;
                for(unsigned n=0;n<600;n++){
                    const auto p2=match.player_stats(1);
                    if(p2.ground_or_air==0&&p2.motion_id==14)break;
                    tick();
                }
                raw[0].button=0;raw[0].stickX=raw[0].stickY=0;
                for(unsigned n=0;n<720;n++){
                    const auto state=match.player_stats(0);
                    if(state.ground_or_air==0&&state.motion_id==14)break;
                    tick();
                }
                check(match.player_stats(0).ground_or_air==0&&
                      match.player_stats(0).motion_id==14,
                      "Mewtwo capture recipe did not settle to grounded Wait");
                std::cout<<"Mewtwo ground Confusion capture lifetime passed"<<std::endl;
            }
        if (cycle==0&&doctor) {
            /* Dr. Mario's up taunt is a fighter-owned source action.  It also
             * exercises ftDr_Init_80149910, which installs the shared Mario
             * action IDs 341/342 and the pill lifecycle callback. */
            bool taunt=false,pill_live=false;
            raw[0].stickX=raw[0].stickY=0;
            for(unsigned n=0;n<240&&!(taunt&&pill_live);n++){
                raw[0].button=n%8==0?PAD_BUTTON_UP:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                taunt=motion==ftMr_MS_AppealSR||motion==ftMr_MS_AppealSL;
                pill_live|=melee_web_test_item_count(It_Kind_DrMario_Vitamin)>0;
            }
            raw[0].button=0;
            check(taunt,"Dr. Mario up taunt did not enter its original source action");
            check(pill_live,"Dr. Mario up taunt did not create its original vitamin article");
            bool pill_cleared=false;
            for(unsigned n=0;n<300&&!pill_cleared;n++){
                const auto state=match.player_stats(0);
                pill_cleared=state.ground_or_air==0&&state.motion_id!=ftMr_MS_AppealSR&&
                   state.motion_id!=ftMr_MS_AppealSL&&
                   melee_web_test_item_count(It_Kind_DrMario_Vitamin)==0;
                tick();
            }
            check(pill_cleared,"Dr. Mario up taunt did not clear its original vitamin article");
        }
        const auto damage=match.player_stats(1).damage_percent;
        bool capsule=false,captain_family_punch=false,luigi_fireball_live=false;
        bool donkey_ground_n=false;
        bool koopa_ground_n=false,koopa_flame_live=false;
        bool mewtwo_ground_n=false,mewtwo_ball_live=false;
        bool pikachu_ground_n=false,pikachu_ground_article_live=false;
        const int pikachu_ground_kind=pichu?It_Kind_Pichu_TJolt_Ground:
            It_Kind_Pikachu_TJolt_Ground;
        const int pikachu_thunder_kind=pichu?It_Kind_Pichu_Thunder:
            It_Kind_Pikachu_Thunder;
        const float pikachu_damage_before_specials=match.player_stats(0).damage_percent;
        bool pichu_self_damage_seen=false;
        for(unsigned n=0;n<240&&match.player_stats(1).damage_percent==damage;n++){
            // Ness and Mewtwo hold B through the source charge gate
            // (Start+Loop reach LoopFull inside 130 ticks) and their balls
            // release from a fresh B edge, so the recipe re-presses B once;
            // Ness's flash fires from a B release after the charge loop.
            raw[0].button=ness?(n<120?PAD_BUTTON_B:0):
                mewtwo?((n<130||n==132)?PAD_BUTTON_B:0):
                koopa?PAD_BUTTON_B:
                (n%8==0?PAD_BUTTON_B:0);tick();
            if(ganon||captain)captain_family_punch|=match.player_stats(0).motion_id==ftCa_MS_SpecialN;
            if(koopa){
                koopa_ground_n|=match.player_stats(0).motion_id==ftKp_MS_SpecialN;
                koopa_flame_live|=melee_web_test_item_count(It_Kind_Koopa_Flame)>0;
            }
            if(mewtwo){
                const auto motion=match.player_stats(0).motion_id;
                mewtwo_ground_n|=motion>=ftMt_MS_SpecialNStart&&motion<=ftMt_MS_SpecialNEnd;
                mewtwo_ball_live|=melee_web_test_item_count(It_Kind_Mewtwo_ShadowBall)>0;
            }
            if(donkey)donkey_ground_n|=match.player_stats(0).motion_id>=ftDk_MS_SpecialNStart&&
                match.player_stats(0).motion_id<=ftDk_MS_SpecialNFull;
            if(doctor){
                const auto motion=match.player_stats(0).motion_id;
                capsule|=motion==ftMr_MS_SpecialN||motion==ftMr_MS_SpecialAirN;
            }
            if(luigi)luigi_fireball_live|=melee_web_test_item_count(It_Kind_Luigi_Fire)>0;
            if(pikachu_family){
                pikachu_ground_n|=match.player_stats(0).motion_id==ftPk_MS_SpecialN;
                pikachu_ground_article_live|=
                    melee_web_test_item_count(pikachu_ground_kind)>0;
            }
            if(pichu)
                pichu_self_damage_seen|=
                    match.player_stats(0).damage_percent>
                    pikachu_damage_before_specials+0.001f;
        }
        raw[0].button=0;
        if(ganon||captain)check(captain_family_punch,
            "Captain-family fighter did not enter its original grounded neutral special");
        if(donkey)check(donkey_ground_n,
            "Donkey did not enter an original grounded Giant Punch state");
        if(koopa){
            check(koopa_ground_n&&koopa_flame_live,
                  "Bowser ground neutral special did not create its original Flame Article");
            bool cleared=false;
            for(unsigned n=0;n<600&&!cleared;n++){
                tick();
                cleared=melee_web_test_item_count(It_Kind_Koopa_Flame)==0 &&
                        match.player_stats(0).motion_id<ftKp_MS_SpecialNStart;
            }
            check(cleared,"Bowser Flame did not end through its source lifetime");
        }
        if(mewtwo){
            check(mewtwo_ground_n&&mewtwo_ball_live,
                  "Mewtwo ground neutral special did not spawn its original Shadow Ball article");
            bool cleared=false;
            for(unsigned n=0;n<600&&!cleared;n++){
                tick();
                cleared=melee_web_test_item_count(It_Kind_Mewtwo_ShadowBall)==0 &&
                        match.player_stats(0).motion_id<ftMt_MS_SpecialNStart;
            }
            check(cleared,"Mewtwo Shadow Ball did not end through its source lifetime");
        }
        if(doctor)check(capsule,"Dr. Mario ground neutral special did not enter its original capsule action");
        if(luigi){
            check(luigi_fireball_live,"Luigi ground neutral special did not create its original fireball article");
            bool cleared=false;
            for(unsigned n=0;n<600&&!cleared;n++){
                tick();cleared=melee_web_test_item_count(It_Kind_Luigi_Fire)==0;
            }
            check(cleared,"Luigi ground neutral special did not destroy its original fireball article");
        }
        if(pikachu_family){
            check(pikachu_ground_n,
                  "Pikachu-family ground neutral special did not enter its original source state");
            check(pikachu_ground_article_live,
                  "Pikachu-family ground neutral special did not create its authored jolt article");
            bool cleared=false;
            for(unsigned n=0;n<600&&!cleared;n++){
                tick();
                if(pichu)
                    pichu_self_damage_seen|=
                        match.player_stats(0).damage_percent>
                        pikachu_damage_before_specials+0.001f;
                cleared=melee_web_test_item_count(pikachu_ground_kind)==0;
            }
            check(cleared,
                  "Pikachu-family ground neutral special did not destroy its authored jolt article");
        }
        std::cout<<fighter_content->name<<(fox_family?" laser damage=":" neutral-special damage=")
                 <<match.player_stats(1).damage_percent<<std::endl;
        if(cycle==0&&fox_family){
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
        }else if(cycle==0&&(doctor||mars_family)){
            for(unsigned n=0;n<300;n++){
                const auto motion=match.player_stats(0).motion_id;
                if(motion<ftMs_MS_SpecialNStart||motion>ftMs_MS_SpecialAirNEnd1)break;
                tick();
            }
            bool jab=false;
            for(unsigned n=0;n<120&&!jab;n++){
                raw[0].button=n%8==0?PAD_BUTTON_A:0;tick();
                jab=match.player_stats(0).motion_id==44;
            }
            raw[0].button=0;
            check(jab,doctor?"Dr. Mario jab did not enter and execute its original Attack11 script":
                  "Marth-family jab did not enter and execute its original Attack11 script");
            for(unsigned n=0;n<120&&match.player_stats(0).motion_id==44;n++)tick();
        }
        for(unsigned n=0;n<120&&match.player_stats(0).ground_or_air==0;n++){
            raw[0].button=n%8==0?PAD_BUTTON_X:0;tick();
        }
        raw[0].button=0;
        check(match.player_stats(0).ground_or_air==1,"Selected fighter did not jump");
        bool air_capsule=false,air_captain_family_punch=false;
        bool pikachu_air_n=false,pikachu_air_article_live=false;
        for(unsigned n=0;n<100;n++){
            // Mewtwo's held aerial charge also releases from a fresh B edge.
            raw[0].button=mewtwo?((n<20||(n>=21&&n<23))?PAD_BUTTON_B:0):
                (n<20?PAD_BUTTON_B:0);tick();
            if(ganon||captain)air_captain_family_punch|=match.player_stats(0).motion_id==ftCa_MS_SpecialAirN;
            if(doctor){
                const auto motion=match.player_stats(0).motion_id;
                air_capsule|=motion==ftMr_MS_SpecialAirN;
            }
            if(pikachu_family){
                const auto motion=match.player_stats(0).motion_id;
                pikachu_air_n|=motion==ftPk_MS_SpecialAirN;
                // ftpikachuspecialn.c passes specialn_itkind for both the
                // ground and air callbacks; retain that source identity here.
                pikachu_air_article_live|=
                    melee_web_test_item_count(pikachu_ground_kind)>0;
            }
            if(pichu)
                pichu_self_damage_seen|=
                    match.player_stats(0).damage_percent>
                    pikachu_damage_before_specials+0.001f;
        }
        if(doctor&&cycle==0)check(air_capsule,
            "Dr. Mario aerial neutral special did not enter its original capsule action");
        if(ganon||captain)check(air_captain_family_punch,
            "Captain-family fighter did not enter its original aerial neutral special");
        if(pikachu_family&&cycle==0){
            check(pikachu_air_n,
                  "Pikachu-family aerial neutral special did not enter its original source state");
            check(pikachu_air_article_live,
                  "Pikachu-family aerial neutral special did not create its source jolt article");
            bool cleared=false;
            for(unsigned n=0;n<600&&!cleared;n++){
                tick();
                if(pichu)
                    pichu_self_damage_seen|=
                        match.player_stats(0).damage_percent>
                        pikachu_damage_before_specials+0.001f;
                cleared=melee_web_test_item_count(pikachu_ground_kind)==0;
            }
            check(cleared,
                  "Pikachu-family aerial neutral special did not destroy its source jolt article");
        }
        if(cycle==0&&fox_family){
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
            bool fire_bird_launch=false,fire_bird_finished=false;
            for(unsigned n=0;n<360&&!fire_bird_finished;n++){
                tick();
                const auto state=match.player_stats(0);
                fire_bird_launch|=state.motion_id==356||state.motion_id==357;
                fire_bird_finished=fire_bird_launch&&
                    (state.motion_id<353||state.motion_id>359);
            }
            check(fire_bird_launch,
                  "Up special did not reach its original Fire Fox launch motion");
            check(fire_bird_finished,
                  "Up special did not finish its original Fire Fox launch sequence");
        }else if(cycle==0&&doctor){
            raw[0].button=0;raw[0].stickX=raw[0].stickY=0;
            for(unsigned n=0;n<420;n++){
                const auto state=match.player_stats(0);
                if(state.ground_or_air==0&&(state.motion_id<ftMr_MS_SpecialN||
                   state.motion_id>ftMr_MS_SpecialAirLw))break;
                tick();
            }
            check(match.player_stats(0).ground_or_air==0,
                  "Dr. Mario did not land after the aerial capsule");

            bool cape=false,sheet_live=false;
            for(unsigned n=0;n<180&&!(cape&&sheet_live);n++){
                raw[0].stickX=80;raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                cape=motion==ftMr_MS_SpecialS||motion==ftMr_MS_SpecialAirS;
                sheet_live|=melee_web_test_item_count(It_Kind_DrMario_Sheet)>0;
            }
            raw[0].stickX=0;raw[0].button=0;
            check(cape,"Dr. Mario side special did not enter its original cape action");
            check(sheet_live,"Dr. Mario side special did not create its original sheet article");
            for(unsigned n=0;n<300;n++){
                const auto state=match.player_stats(0);
                if(state.ground_or_air==0&&(state.motion_id<ftMr_MS_SpecialS||
                   state.motion_id>ftMr_MS_SpecialAirLw))break;
                tick();
            }

            bool up=false;
            for(unsigned n=0;n<180&&!up;n++){
                raw[0].stickY=80;raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                up=motion==ftMr_MS_SpecialHi||motion==ftMr_MS_SpecialAirHi;
            }
            raw[0].stickY=0;raw[0].button=0;
            check(up,"Dr. Mario up special did not enter its original source action");
            for(unsigned n=0;n<420;n++){
                const auto state=match.player_stats(0);
                if(state.ground_or_air==0&&(state.motion_id<ftMr_MS_SpecialN||
                   state.motion_id>ftMr_MS_SpecialAirLw))break;
                tick();
            }
            check(match.player_stats(0).ground_or_air==0,
                  "Dr. Mario did not land after the original up special");

            bool down=false;
            for(unsigned n=0;n<180&&!down;n++){
                raw[0].stickY=-80;raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                down=motion==ftMr_MS_SpecialLw||motion==ftMr_MS_SpecialAirLw;
            }
            raw[0].stickY=0;raw[0].button=0;
            check(down,"Dr. Mario down special did not enter its original source action");
        }else if(cycle==0&&(ganon||captain)){
            auto settle=[&](){
                raw[0].button=0;raw[0].stickX=raw[0].stickY=0;
                for(unsigned n=0;n<480;n++){
                    const auto state=match.player_stats(0);
                    if(state.ground_or_air==0&&state.motion_id<ftCa_MS_SwordSwing4){
                        raw[0].stickX=0;
                        return;
                    }
                    // An unassisted Falcon Kick can cross FD's ledge. Leave
                    // the original rebirth platform with ordinary movement
                    // rather than spending its complete neutral-input timer.
                    raw[0].stickX=state.motion_id==ftCo_MS_RebirthWait?
                        (state.position[0]>0?-40:40):0;
                    tick();
                }
                const auto failed=match.player_stats(0);
                std::cout<<"Captain-family settle failure x="<<failed.position[0]
                         <<" y="<<failed.position[1]<<" motion="<<failed.motion_id
                         <<" air="<<failed.ground_or_air<<std::endl;
                check(false,"Captain-family special did not return to grounded common motion");
            };
            auto center=[&](){
                settle();
                const auto before=match.player_stats(0);
                std::cout<<"Captain-family recenter entry x="<<before.position[0]
                         <<" y="<<before.position[1]<<" motion="<<before.motion_id<<std::endl;
                // Use the same feedback-driven walk recovery as the visible
                // sweep. Captain's authored run/brake momentum carried the
                // old full-stick dash past center after input was released.
                for(unsigned elapsed=0;elapsed<720;){
                    const auto state=match.player_stats(0);
                    if(state.ground_or_air==0&&state.motion_id==14&&
                       std::abs(state.position[0])<=12)break;
                    const bool can_walk=state.ground_or_air==0&&
                        ((state.motion_id>=14&&state.motion_id<=23)||
                         state.motion_id==245||state.motion_id==246);
                    const bool walk=can_walk&&std::abs(state.position[0])>12;
                    raw[0].stickX=walk?(state.position[0]>0?-40:40):0;
                    const unsigned duration=std::min(720-elapsed,walk?4U:can_walk?24U:120U);
                    for(unsigned n=0;n<duration;n++)tick();
                    elapsed+=duration;
                }
                raw[0].stickX=0;
                for(unsigned n=0;n<2;n++)tick();
                const auto after=match.player_stats(0);
                std::cout<<"Captain-family recenter result x="<<after.position[0]
                         <<" y="<<after.position[1]<<" motion="<<after.motion_id<<std::endl;
                check(after.ground_or_air==0&&after.motion_id==14&&std::abs(after.position[0])<18,
                      "Raw movement did not recenter the Captain-family fighter for special lifecycle check");
            };
            center();
            bool side=false;
            for(unsigned n=0;n<180&&!side;n++){
                raw[0].stickX=match.player_stats(0).position[0]>0?-80:80;
                raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                side=motion==ftCa_MS_SpecialSStart||motion==ftCa_MS_SpecialS;
            }
            check(side,"Captain-family Raptor Boost did not enter its original source state");
            center();
            bool up=false;
            for(unsigned n=0;n<180&&!up;n++){
                raw[0].stickY=80;raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                up=motion==ftCa_MS_SpecialHi||motion==ftCa_MS_SpecialAirHi;
            }
            check(up,"Captain-family up special did not enter its original source state");
            center();
            bool down=false;
            for(unsigned n=0;n<180&&!down;n++){
                raw[0].stickY=-80;raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                down=match.player_stats(0).motion_id==ftCa_MS_SpecialLw;
            }
            check(down,"Captain-family down special did not enter its original source state");
            const auto stocks_before_down_recovery=match.player_stats(0).stocks;
            settle();
            std::cout<<"Captain-family down recovery stocks="<<stocks_before_down_recovery
                     <<" -> "<<match.player_stats(0).stocks<<std::endl;
            std::cout<<fighter_content->name<<" original N/air-N/S/Hi/Lw lifecycle branches executed"<<std::endl;
        }else if(cycle==0&&koopa){
            auto neutral_koopa=[&](){
                raw[0].button=0;raw[0].stickX=raw[0].stickY=0;
            };
            auto settle_koopa=[&](){
                neutral_koopa();
                for(unsigned n=0;n<720;n++){
                    const auto state=match.player_stats(0);
                    if(state.ground_or_air==0&&state.motion_id==ftCo_MS_Wait)return;
                    tick();
                }
                const auto state=match.player_stats(0);
                std::cout<<"Bowser settle failure motion="<<state.motion_id
                         <<" air="<<state.ground_or_air<<" x="<<state.position[0]
                         <<" y="<<state.position[1]<<std::endl;
                check(false,"Bowser special did not return to grounded Wait");
            };
            auto jump_koopa=[&](){
                settle_koopa();
                // Bowser's authored jump squat is eight source ticks. Hold
                // X through that gate; no motion or velocity is injected.
                raw[0].button=PAD_BUTTON_X;
                for(unsigned n=0;n<12;n++)tick();
                raw[0].button=0;
                check(match.player_stats(0).ground_or_air!=0,
                      "Bowser full jump did not reach the source aerial state");
            };
            jump_koopa();
            bool air_n=false,air_flame=false,landed_n=false,ground_end=false;
            int last_motion=-1;
            for(unsigned n=0;n<240&&!air_flame;n++){
                raw[0].button=PAD_BUTTON_B;tick();
                const auto state=match.player_stats(0);
                if(state.motion_id!=last_motion)
                    std::cout<<"Bowser air Flame frame="<<n<<" motion="<<state.motion_id
                             <<" y="<<state.position[1]<<" items="
                             <<melee_web_test_item_count(It_Kind_Koopa_Flame)<<std::endl;
                last_motion=state.motion_id;
                air_n|=state.motion_id==ftKp_MS_SpecialAirN;
                air_flame=state.motion_id==ftKp_MS_SpecialAirN&&
                    melee_web_test_item_count(It_Kind_Koopa_Flame)>0;
                if(state.ground_or_air==0)break;
            }
            neutral_koopa();
            check(air_n&&air_flame,
                  "Bowser aerial Flame did not spawn before source landing");
            // This FD jump lands before the source minimum Flame hold ends.
            // Check the actual AirN_Coll -> ground N -> ground End path;
            // aerial End remains a separate, unexercised motion in this route.
            bool flame_clear=false;
            for(unsigned n=0;n<600&&!(ground_end&&flame_clear);n++){
                tick();
                const auto state=match.player_stats(0);
                if(state.motion_id!=last_motion)
                    std::cout<<"Bowser Flame release frame="<<n<<" motion="<<state.motion_id
                             <<" y="<<state.position[1]<<std::endl;
                last_motion=state.motion_id;
                landed_n|=state.motion_id==ftKp_MS_SpecialN&&state.ground_or_air==0;
                ground_end|=state.motion_id==ftKp_MS_SpecialNEnd;
                flame_clear=melee_web_test_item_count(It_Kind_Koopa_Flame)==0;
            }
            check(landed_n&&ground_end&&flame_clear,
                  "Bowser aerial Flame did not complete its source landing/end/Article teardown");
            settle_koopa();
            for(bool air:{false,true}){
                if(air)jump_koopa();else settle_koopa();
                bool entered=false;
                for(unsigned n=0;n<240&&!entered;n++){
                    raw[0].button=n==0?PAD_BUTTON_B:0;
                    raw[0].stickY=n==0?80:0;tick();
                    entered=match.player_stats(0).motion_id==
                        (air?ftKp_MS_SpecialAirHi:ftKp_MS_SpecialHi);
                }
                check(entered,"Bowser up special did not enter its ground/air source motion");
                settle_koopa();
                std::cout<<"Bowser "<<(air?"air":"ground")<<" Fortress lifetime passed"<<std::endl;
            }
            for(bool air:{false,true}){
                if(air)jump_koopa();else settle_koopa();
                bool ground_start=false,air_drop=false,landing=false;
                for(unsigned n=0;n<240&&!landing;n++){
                    raw[0].button=n==0?PAD_BUTTON_B:0;
                    raw[0].stickY=n==0?-80:0;tick();
                    const auto motion=match.player_stats(0).motion_id;
                    ground_start|=motion==ftKp_MS_SpecialLw;
                    air_drop|=motion==ftKp_MS_SpecialAirLw;
                    landing|=motion==ftKp_MS_SpecialLwLanding;
                }
                check((air||ground_start)&&air_drop&&landing,
                      "Bowser Bomb did not complete source descent and landing states");
                settle_koopa();
                std::cout<<"Bowser "<<(air?"air":"ground")<<" Bomb lifetime passed"<<std::endl;
            }
            for(bool air:{false,true}){
                settle_koopa();
                raw[1].button=0;raw[1].stickX=raw[1].stickY=0;
                // Let the previous throw finish before walking toward its
                // victim. Chasing an airborne victim can walk off the stage.
                bool victim_ready=false;
                for(unsigned n=0;n<900&&!victim_ready;n++){
                    const auto target=match.player_stats(1);
                    raw[1].stickX=target.motion_id==ftCo_MS_RebirthWait?
                        (target.position[0]>0?-40:40):0;
                    victim_ready=target.ground_or_air==0&&target.motion_id==ftCo_MS_Wait;
                    if(!victim_ready)tick();
                }
                raw[1].stickX=0;
                check(victim_ready,"Mario did not finish Bowser's preceding throw/respawn");
                for(unsigned n=0;n<600;n++){
                    const auto target=match.player_stats(1);
                    const float delta=target.position[0]-match.player_stats(0).position[0];
                    if(target.ground_or_air==0&&target.motion_id==ftCo_MS_Wait&&
                       std::fabs(delta)<16.0f)break;
                    raw[0].stickX=std::fabs(delta)<14.0f?0:(delta>0?40:-40);tick();
                }
                neutral_koopa();
                for(unsigned n=0;n<12;n++)tick();
                const float delta=match.player_stats(1).position[0]-match.player_stats(0).position[0];
                std::cout<<"Bowser catch approach air="<<air<<" delta="<<delta
                         <<" p1motion="<<match.player_stats(0).motion_id
                         <<" p2motion="<<match.player_stats(1).motion_id
                         <<" p1x="<<match.player_stats(0).position[0]
                         <<" p2x="<<match.player_stats(1).position[0]<<std::endl;
                check(std::fabs(delta)<18.0f&&match.player_stats(1).ground_or_air==0,
                      "Bowser raw approach did not place Mario in side-capture range");
                const int direction=delta>0?80:-80;
                if(air){
                    // Offset the two authored jump-squat durations so both
                    // fighters leave the floor together, using only PAD.
                    for(unsigned n=0;n<12;n++){
                        raw[0].button=PAD_BUTTON_X;
                        raw[1].button=n>=4?PAD_BUTTON_X:0;tick();
                    }
                    raw[0].button=raw[1].button=0;
                    check(match.player_stats(0).ground_or_air!=0&&
                          match.player_stats(1).ground_or_air!=0,
                          "Bowser/Mario capture setup did not reach both source jumps");
                }
                bool start=false,captured=false;
                for(unsigned n=0;n<240&&!captured;n++){
                    raw[0].button=n<12?PAD_BUTTON_B:0;
                    raw[0].stickX=n==0?direction:0;tick();
                    const auto motion=match.player_stats(0).motion_id;
                    start|=motion==(air?ftKp_MS_SpecialAirSStart:ftKp_MS_SpecialSStart);
                    captured=melee_web_test_koopa_capture(air?2:0,FTKIND_MARIO)!=0;
                    if(n<24||captured)
                        std::cout<<"Bowser "<<(air?"air":"ground")<<" catch frame="<<n
                                 <<" motion="<<motion<<" victim="<<match.player_stats(1).motion_id
                                 <<" captured="<<captured<<std::endl;
                }
                neutral_koopa();
                check(start&&captured,"Bowser side special did not capture its original Mario victim");
                if(!air){
                    bool waited=false;
                    for(unsigned n=0;n<240&&!waited;n++){
                        tick();
                        waited=match.player_stats(0).motion_id==ftKp_MS_SpecialSHit0_1&&
                               melee_web_test_koopa_capture(0,FTKIND_MARIO)!=0;
                    }
                    check(waited,"Bowser ground catch did not reach source held-victim Wait");
                }else tick();
                bool thrown=false;
                for(unsigned n=0;n<120&&!thrown;n++){
                    raw[0].stickX=n==0?(air?-direction:direction):0;tick();
                    thrown=melee_web_test_koopa_capture(air?3:1,FTKIND_MARIO)!=0;
                }
                neutral_koopa();
                check(thrown,"Bowser side special did not reach its source linked-victim throw");
                settle_koopa();
                std::cout<<"Bowser "<<(air?"air":"ground")<<" catch/throw lifetime passed"<<std::endl;
            }
        }else if(cycle==0&&mewtwo){
            auto neutral_mewtwo=[&](){
                raw[0].button=0;raw[0].stickX=raw[0].stickY=0;
            };
            auto settle_mewtwo=[&](){
                neutral_mewtwo();
                for(unsigned n=0;n<720;n++){
                    const auto state=match.player_stats(0);
                    if(state.ground_or_air==0&&state.motion_id==ftCo_MS_Wait)return;
                    tick();
                }
                const auto state=match.player_stats(0);
                std::cout<<"Mewtwo settle failure motion="<<state.motion_id
                         <<" air="<<state.ground_or_air<<" x="<<state.position[0]
                         <<" y="<<state.position[1]<<std::endl;
                check(false,"Mewtwo special did not return to grounded Wait");
            };
            auto jump_mewtwo=[&](){
                settle_mewtwo();
                raw[0].button=PAD_BUTTON_X;
                for(unsigned n=0;n<12;n++)tick();
                raw[0].button=0;
                check(match.player_stats(0).ground_or_air!=0,
                      "Mewtwo full jump did not reach the source aerial state");
            };
            jump_mewtwo();
            bool air_loop=false,air_ball=false;
            int last_motion=-1;
            for(unsigned n=0;n<240&&!air_loop;n++){
                raw[0].button=PAD_BUTTON_B;tick();
                const auto state=match.player_stats(0);
                if(state.motion_id!=last_motion)
                    std::cout<<"Mewtwo air charge frame="<<n<<" motion="<<state.motion_id
                             <<" y="<<state.position[1]<<" items="
                             <<melee_web_test_item_count(It_Kind_Mewtwo_ShadowBall)<<std::endl;
                last_motion=state.motion_id;
                air_loop|=state.motion_id==ftMt_MS_SpecialAirNLoop;
            }
            neutral_mewtwo();
            for(unsigned n=0;n<2;n++)tick();
            raw[0].button=PAD_BUTTON_B;tick();
            neutral_mewtwo();
            check(air_loop,"Mewtwo aerial charge did not reach its source Loop motion");
            bool air_end=false,air_ball_live=false,landed=false;
            for(unsigned n=0;n<600&&!(landed&&air_end&&air_ball_live);n++){
                tick();
                const auto state=match.player_stats(0);
                if(state.motion_id!=last_motion)
                    std::cout<<"Mewtwo air release frame="<<n<<" motion="<<state.motion_id
                             <<" y="<<state.position[1]<<" items="
                             <<melee_web_test_item_count(It_Kind_Mewtwo_ShadowBall)<<std::endl;
                last_motion=state.motion_id;
                air_end|=state.motion_id==ftMt_MS_SpecialAirNEnd;
                air_ball_live|=melee_web_test_item_count(It_Kind_Mewtwo_ShadowBall)>0;
                landed|=state.ground_or_air==0;
            }
            check(air_end&&air_ball_live,
                  "Mewtwo aerial release did not spawn its original Shadow Ball article");
            for(unsigned n=0;n<600;n++){
                tick();
                if(melee_web_test_item_count(It_Kind_Mewtwo_ShadowBall)==0)break;
            }
            check(melee_web_test_item_count(It_Kind_Mewtwo_ShadowBall)==0,
                  "Mewtwo aerial Shadow Ball did not end through its source lifetime");
            settle_mewtwo();
            for(bool air:{false,true}){
                if(air)jump_mewtwo();else settle_mewtwo();
                bool entered=false,lost=false;
                for(unsigned n=0;n<240&&!entered;n++){
                    raw[0].button=n==0?PAD_BUTTON_B:0;
                    raw[0].stickY=n==0?80:0;tick();
                    const auto motion=match.player_stats(0).motion_id;
                    entered=motion==(air?ftMt_MS_SpecialAirHiStart:ftMt_MS_SpecialHiStart);
                    lost|=motion==ftMt_MS_SpecialHiLost;
                }
                check(entered,"Mewtwo up special did not enter its ground/air source motion");
                settle_mewtwo();
                std::cout<<"Mewtwo "<<(air?"air":"ground")<<" Teleport lifetime passed"
                         <<(lost?" (HiLost observed)":"")<<std::endl;
            }
            for(bool air:{false,true}){
                if(air)jump_mewtwo();else settle_mewtwo();
                bool entered=false;
                for(unsigned n=0;n<240&&!entered;n++){
                    raw[0].button=n==0?PAD_BUTTON_B:0;
                    raw[0].stickX=n==0?(air?-80:80):0;tick();
                    entered=match.player_stats(0).motion_id==
                        (air?ftMt_MS_SpecialAirS:ftMt_MS_SpecialS);
                }
                check(entered,"Mewtwo side special did not enter its ground/air source motion");
                settle_mewtwo();
                std::cout<<"Mewtwo "<<(air?"air":"ground")<<" Confusion lifetime passed"<<std::endl;
            }
            for(bool air:{false,true}){
                if(air)jump_mewtwo();else settle_mewtwo();
                bool entered=false,disable_live=false;
                for(unsigned n=0;n<240&&!entered;n++){
                    raw[0].button=n==0?PAD_BUTTON_B:0;
                    raw[0].stickY=n==0?-80:0;tick();
                    entered=match.player_stats(0).motion_id==
                        (air?ftMt_MS_SpecialAirLw:ftMt_MS_SpecialLw);
                }
                check(entered,"Mewtwo down special did not enter its ground/air source motion");
                for(unsigned n=0;n<120&&!disable_live;n++){
                    tick();
                    disable_live=melee_web_test_item_count(It_Kind_Mewtwo_Disable)>0;
                }
                check(disable_live,
                      "Mewtwo down special did not create its original Disable article");
                bool cleared=false;
                for(unsigned n=0;n<600&&!cleared;n++){
                    tick();
                    cleared=melee_web_test_item_count(It_Kind_Mewtwo_Disable)==0;
                }
                check(cleared,"Mewtwo Disable did not end through its source lifetime");
                settle_mewtwo();
                std::cout<<"Mewtwo "<<(air?"air":"ground")<<" Disable lifetime passed"<<std::endl;
            }
            std::cout<<fighter_content->name<<" original N/air-N/S/Hi/Lw lifecycle branches executed"<<std::endl;


        }else if(cycle==0&&donkey){
            // The common neutral loop above already proves grounded Giant
            // Punch damage and an authored N state.  This branch drives the
            // remaining Donkey state machines with raw PAD edges and records
            // source motion IDs only; it never writes Fighter state.
            auto settle_donkey=[&](){
                raw[0].button=0;raw[0].stickX=raw[0].stickY=0;
                for(unsigned n=0;n<720;n++){
                    const auto state=match.player_stats(0);
                    if(state.ground_or_air==0&&state.motion_id<ftDk_MS_HeavyWait)
                        return;
                    tick();
                }
                const auto state=match.player_stats(0);
                std::cout<<"Donkey settle failure motion="<<state.motion_id
                         <<" ground="<<state.ground_or_air<<" x="<<state.position[0]
                         <<" y="<<state.position[1]<<std::endl;
                check(false,"Donkey special did not return to a grounded common motion");
            };
            auto jump_donkey=[&](){
                settle_donkey();
                raw[0].stickX=raw[0].stickY=0;
                // Hold through the original jump-squat gate for a full jump;
                // a one-tick pulse selects a short hop and can land before
                // the neutral-special start/loop/release sequence finishes.
                raw[0].button=PAD_BUTTON_X;
                for(unsigned n=0;n<12;n++)tick();
                raw[0].button=0;
                for(unsigned n=0;n<120&&match.player_stats(0).ground_or_air==0;n++)tick();
                check(match.player_stats(0).ground_or_air!=0,
                      "Donkey did not enter an authored aerial state");
            };
            auto run_special=[&](bool air,int ground_motion,int air_motion,
                                 int stick_x,int stick_y,const char* failure){
                if(air)jump_donkey();else settle_donkey();
                bool entered=false;
                int observed=-1;
                for(unsigned n=0;n<240&&!entered;n++){
                    raw[0].stickX=stick_x;raw[0].stickY=stick_y;
                    raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                    const auto state=match.player_stats(0);
                    if(observed!=state.motion_id)
                        std::cout<<"Donkey special transition frame="<<n
                                 <<" motion="<<state.motion_id<<" air="<<state.ground_or_air
                                 <<" y="<<state.position[1]<<std::endl;
                    observed=state.motion_id;
                    entered=observed==(air?air_motion:ground_motion);
                    // Earlier source input can leave a stored Giant Punch.
                    // Preserve that history: AirN_Enter selects Full at ten
                    // swings, otherwise Loop_IASA selects the partial punch.
                    if(air&&air_motion==ftDk_MS_SpecialAirN)
                        entered|=observed==ftDk_MS_SpecialAirNFull;
                }
                raw[0].button=0;raw[0].stickX=raw[0].stickY=0;
                check(entered,failure);
                std::cout<<"Donkey "<<(air?"air":"ground")
                         <<" special motion="<<observed<<std::endl;
                settle_donkey();
            };

            // Source ftdonkeyspecialn.c uses a separate air state table.  A
            // single raw jump followed by B edges reaches the AirN entry;
            // no animation/state is manufactured when falling short.
            run_special(true,ftDk_MS_SpecialN,ftDk_MS_SpecialAirN,0,0,
                        "Donkey aerial Giant Punch did not enter its original source state");
            run_special(false,ftDk_MS_SpecialS,ftDk_MS_SpecialAirS,80,0,
                        "Donkey grounded Headbutt did not enter its original source state");
            run_special(true,ftDk_MS_SpecialS,ftDk_MS_SpecialAirS,80,0,
                        "Donkey aerial Headbutt did not enter its original source state");
            run_special(false,ftDk_MS_SpecialHi,ftDk_MS_SpecialAirHi,0,80,
                        "Donkey grounded Spinning Kong did not enter its original source state");
            run_special(true,ftDk_MS_SpecialHi,ftDk_MS_SpecialAirHi,0,80,
                        "Donkey aerial Spinning Kong did not enter its original source state");
            run_special(false,ftDk_MS_SpecialLwStart,ftDk_MS_SpecialLwStart,0,-80,
                        "Donkey grounded Hand Slap did not enter its original source state");

            // The cargo victim can be any admitted fighter; running this
            // branch with P2=Ness dispatches Ness empty victim rows 267..283
            // from the thrower store and exercises his own empty rows.
            const int cargo_victim_kind=opponent_content->fighter_kind;
            // Approach with source movement, then neutralize the stick while
            // the ordinary Z edge enters Catch/CatchPull.
            // ftCo_CatchWait_IASA then requires a *new* stick threshold crossing
            // to select common ThrowF/ThrowB; holding the walk direction during
            // Z retries would never provide that source edge.
            settle_donkey();
            raw[1].button=0;raw[1].stickX=0;raw[1].stickY=-80;
            for(unsigned n=0;n<90;n++)tick();
            raw[1].stickY=0;
            bool catch_wait=false;
            for(unsigned n=0;n<300&&!catch_wait;n++){
                const auto player=match.player_stats(0),target=match.player_stats(1);
                const float delta=target.position[0]-player.position[0];
                const bool in_range=std::abs(delta)<18.0f && delta*player.facing_direction>0;
                raw[0].stickX=in_range?0:delta>0?80:-80;raw[0].stickY=0;
                raw[0].button=in_range&&n%20==0?PAD_TRIGGER_Z:0;tick();
                catch_wait=melee_web_test_donkey_cargo(4,cargo_victim_kind)!=0;
            }
            raw[0].button=0;raw[0].stickX=raw[0].stickY=0;
            check(catch_wait,"Donkey raw Z input did not reach source CatchWait with Mario as victim");
            // Establish the neutral previous stick, then cross the source
            // horizontal threshold toward the victim. The edge selects the
            // ordinary ThrowF/ThrowB callback; DK's x2222_b0 callback converts
            // that source throw into its 351.. cargo states at animation end.
            tick();
            const auto catch_p1=match.player_stats(0),catch_p2=match.player_stats(1);
            raw[0].stickX=catch_p2.position[0]>catch_p1.position[0]?80:-80;
            tick();
            const auto edge_motion=match.player_stats(0).motion_id;
            bool common_throw=edge_motion==ftCo_MS_ThrowF||edge_motion==ftCo_MS_ThrowB;
            bool cargo=false;
            for(unsigned n=0;n<240&&!(common_throw&&cargo);n++){
                raw[0].stickX=0;raw[0].button=0;tick();
                const auto motion=match.player_stats(0).motion_id;
                common_throw|=motion==ftCo_MS_ThrowF||motion==ftCo_MS_ThrowB;
                cargo|=melee_web_test_donkey_cargo(0,cargo_victim_kind)!=0;
            }
            raw[0].button=0;raw[0].stickX=0;
            check(common_throw,"Donkey CatchWait stick edge did not enter source ThrowF/ThrowB");
            check(cargo,"Donkey source ThrowF/ThrowB did not reach CargoWait with Mario as victim");
            bool cargo_walk=false;
            for(unsigned n=0;n<30;n++){
                const auto state=match.player_stats(0);
                raw[0].stickX=state.position[0]>0?-80:80;
                tick();
                cargo_walk|=melee_web_test_donkey_cargo(2,cargo_victim_kind)!=0;
            }
            raw[0].stickX=0;
            check(cargo_walk,"Donkey source CargoWait did not select authored CargoWalk 352..354");

            // ftCo_CargoThrow.c selects +10/+11/+12/+13 from an A/B press
            // plus stick direction. Use the stage-center direction after the
            // walk and accept any authored ground cargo throw state.
            bool cargo_throw=false;
            for(unsigned n=0;n<180&&!cargo_throw;n++){
                const auto state=match.player_stats(0);
                raw[0].stickX=state.position[0]>0?-80:80;
                raw[0].button=n==0?PAD_BUTTON_A:0;tick();
                cargo_throw=melee_web_test_donkey_cargo(1,cargo_victim_kind)!=0;
            }
            raw[0].button=0;raw[0].stickX=0;
            check(cargo_throw,
                  "Donkey source CargoWait did not select a raw-input cargo throw motion");
            settle_donkey();
            std::cout<<"Donkey original ground/air N/S/Hi, ground Lw, raw CargoWait/walk/throw passed"
                     <<std::endl;
        }else if(cycle==0&&ness){
            // Drives every Ness special family plus the Yo-Yo up smash from
            // raw PAD edges. Records source motion IDs and article lifetimes
            // only; no Fighter state is manufactured.
            auto settle_ness=[&](){
                raw[0].button=0;raw[0].stickX=raw[0].stickY=0;
                for(unsigned n=0;n<720;n++){
                    const auto state=match.player_stats(0);
                    if(state.ground_or_air==0&&state.motion_id<ftNs_MS_AttackS4)return;
                    tick();
                }
                const auto state=match.player_stats(0);
                std::cout<<"Ness settle failure motion="<<state.motion_id
                         <<" ground="<<state.ground_or_air<<" x="<<state.position[0]
                         <<" y="<<state.position[1]<<std::endl;
                check(false,"Ness special did not return to a grounded common motion");
            };
            auto jump_ness=[&](){
                settle_ness();
                raw[0].button=PAD_BUTTON_X;
                for(unsigned n=0;n<12;n++)tick();
                raw[0].button=0;
                check(match.player_stats(0).ground_or_air!=0,
                      "Ness full jump did not reach the source aerial state");
            };
            // Ground neutral: PK Flash Start+Loop charge, fresh-B-edge
            // release, original charge-loop article, teardown.
            settle_ness();
            bool n_start=false,n_article=false;
            for(unsigned n=0;n<240&&!n_article;n++){
                raw[0].button=PAD_BUTTON_B;tick();
                const auto motion=match.player_stats(0).motion_id;
                n_start|=motion>=ftNs_MS_SpecialNStart&&motion<=ftNs_MS_SpecialNEnd;
                n_article|=melee_web_test_item_count(It_Kind_Ness_PKFlush)>0;
            }
            check(n_start&&n_article,
                  "Ness ground neutral special did not spawn its original PK Flash article");
            raw[0].button=0;
            for(unsigned n=0;n<2;n++)tick();
            raw[0].button=PAD_BUTTON_B;tick();
            raw[0].button=0;
            bool n_cleared=false;
            for(unsigned n=0;n<600&&!n_cleared;n++){
                tick();
                n_cleared=melee_web_test_item_count(It_Kind_Ness_PKFlush)==0&&
                          match.player_stats(0).motion_id<ftNs_MS_AttackS4;
            }
            check(n_cleared,"Ness PK Flash did not end through its source lifetime");
            // Aerial neutral: charge in the air, release, land.
            jump_ness();
            bool air_n=false;
            for(unsigned n=0;n<200&&!air_n;n++){
                raw[0].button=PAD_BUTTON_B;tick();
                air_n|=match.player_stats(0).motion_id>=ftNs_MS_SpecialAirNStart&&
                       match.player_stats(0).motion_id<=ftNs_MS_SpecialAirNEnd;
            }
            check(air_n,"Ness aerial neutral special did not enter its source motion");
            for(unsigned n=0;n<600&&match.player_stats(0).ground_or_air!=0;n++)tick();
            settle_ness();
            std::cout<<"Ness ground/aerial PK Flash lifetimes passed"<<std::endl;
            for(bool air:{false,true}){
                if(air)jump_ness();else settle_ness();
                bool entered=false,fire_live=false;
                for(unsigned n=0;n<200&&!entered;n++){
                    raw[0].button=n%8==0?PAD_BUTTON_B:0;
                    raw[0].stickX=air?-80:80;raw[0].stickY=0;tick();
                    entered=match.player_stats(0).motion_id==
                        (air?ftNs_MS_SpecialAirS:ftNs_MS_SpecialS);
                }
                check(entered,"Ness side special did not enter its ground/air source motion");
                for(unsigned n=0;n<120&&!fire_live;n++){
                    tick();
                    fire_live=melee_web_test_item_count(It_Kind_Ness_PKFire)>0;
                }
                check(fire_live,"Ness side special did not create its original PK Fire article");
                for(unsigned n=0;n<600&&match.player_stats(0).ground_or_air!=0;n++)tick();
                for(unsigned n=0;n<600&&melee_web_test_item_count(It_Kind_Ness_PKFire)>0;n++)tick();
                check(melee_web_test_item_count(It_Kind_Ness_PKFire)==0,
                      "Ness PK Fire did not end through its source lifetime");
                settle_ness();
                std::cout<<"Ness "<<(air?"air":"ground")<<" PK Fire lifetime passed"<<std::endl;
            }
            for(bool air:{false,true}){
                if(air)jump_ness();else settle_ness();
                bool entered=false,thunder_live=false;
                for(unsigned n=0;n<200&&!entered;n++){
                    raw[0].button=n%8==0?PAD_BUTTON_B:0;
                    raw[0].stickX=0;raw[0].stickY=80;tick();
                    entered=match.player_stats(0).motion_id==
                        (air?ftNs_MS_SpecialAirHiStart:ftNs_MS_SpecialHiStart);
                }
                check(entered,"Ness up special did not enter its ground/air source motion");
                for(unsigned n=0;n<120&&!thunder_live;n++){
                    tick();
                    thunder_live=melee_web_test_item_count(It_Kind_Ness_PKThunder)>0;
                }
                check(thunder_live,"Ness up special did not create its original PK Thunder article");
                for(unsigned n=0;n<900&&match.player_stats(0).ground_or_air!=0;n++)tick();
                for(unsigned n=0;n<600&&melee_web_test_item_count(It_Kind_Ness_PKThunder)>0;n++)tick();
                check(melee_web_test_item_count(It_Kind_Ness_PKThunder)==0,
                      "Ness PK Thunder did not end through its source lifetime");
                settle_ness();
                std::cout<<"Ness "<<(air?"air":"ground")<<" PK Thunder lifetime passed"<<std::endl;
            }
            for(bool air:{false,true}){
                if(air)jump_ness();else settle_ness();
                bool entered=false;
                for(unsigned n=0;n<200&&!entered;n++){
                    raw[0].button=n%8==0?PAD_BUTTON_B:0;
                    raw[0].stickX=0;raw[0].stickY=-80;tick();
                    entered=match.player_stats(0).motion_id==
                        (air?ftNs_MS_SpecialAirLwStart:ftNs_MS_SpecialLwStart);
                }
                check(entered,"Ness down special did not enter its ground/air source motion");
                for(unsigned n=0;n<600&&match.player_stats(0).ground_or_air!=0;n++)tick();
                settle_ness();
                std::cout<<"Ness "<<(air?"air":"ground")<<" PSI Magnet lifetime passed"<<std::endl;
            }
            // Up smash enters the authored Yo-Yo charge states and spawns the
            // yoyo Article with its string/yoyo joints and material record.
            settle_ness();
            bool yoyo_start=false,yoyo_live=false;
            for(unsigned n=0;n<120&&!(yoyo_start&&yoyo_live);n++){
                raw[0].button=n%4==0?PAD_BUTTON_A:0;
                raw[0].stickX=0;raw[0].stickY=n<6?80:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                yoyo_start|=motion>=ftNs_MS_AttackHi4&&motion<=ftNs_MS_AttackLw4Release;
                yoyo_live|=melee_web_test_item_count(It_Kind_Ness_Yoyo)>0;
            }
            check(yoyo_start&&yoyo_live,
                  "Ness up smash did not spawn its original Yo-Yo article");
            raw[0].button=0;raw[0].stickY=0;
            for(unsigned n=0;n<600&&match.player_stats(0).ground_or_air!=0;n++)tick();
            for(unsigned n=0;n<600&&melee_web_test_item_count(It_Kind_Ness_Yoyo)>0;n++)tick();
            check(melee_web_test_item_count(It_Kind_Ness_Yoyo)==0,
                  "Ness Yo-Yo article did not end through its source lifetime");
            settle_ness();
            std::cout<<"Ness original N/air-N/S/Hi/Lw and Yo-Yo smash lifecycle branches executed"<<std::endl;
        }else if(cycle==0&&peach){
            // Drives every Peach special family plus her signature float from
            // raw PAD edges. Records source motion IDs and article lifetimes
            // only; no Fighter state is manufactured. Peach's Toad spore
            // generators need an opponent hit on the counter and stay out of
            // scope here.
            auto settle_peach=[&](){
                raw[0].button=0;raw[0].stickX=raw[0].stickY=0;
                for(unsigned n=0;n<720;n++){
                    const auto state=match.player_stats(0);
                    if(state.ground_or_air==0&&state.motion_id<ftPe_MS_Float)return;
                    tick();
                }
                const auto state=match.player_stats(0);
                std::cout<<"Peach settle failure motion="<<state.motion_id
                         <<" ground="<<state.ground_or_air<<" x="<<state.position[0]
                         <<" y="<<state.position[1]<<std::endl;
                check(false,"Peach special did not return to a grounded common motion");
            };
            auto jump_peach=[&](){
                settle_peach();
                // A landing or late IASA frame can swallow one X edge; keep
                // pressing on the source jump window until the authored
                // aerial state is reached.
                raw[0].button=0;
                for(unsigned n=0;n<90&&match.player_stats(0).ground_or_air==0;n++){
                    raw[0].button=n%12==0?PAD_BUTTON_X:0;tick();
                }
                raw[0].button=0;
                check(match.player_stats(0).ground_or_air!=0,
                      "Peach full jump did not reach the source aerial state");
            };
            // Ground neutral: the Toad counter spawns its original Toad
            // article, then tears it down through the source counter lifetime.
            settle_peach();
            bool toad_start=false,toad_live=false;
            for(unsigned n=0;n<240&&!(toad_start&&toad_live);n++){
                raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                toad_start|=motion>=ftPe_MS_SpecialN&&motion<=ftPe_MS_SpecialNHit;
                toad_live|=melee_web_test_item_count(It_Kind_Peach_Toad)>0;
            }
            raw[0].button=0;
            check(toad_start&&toad_live,
                  "Peach ground neutral special did not spawn its original Toad article");
            bool toad_cleared=false;
            for(unsigned n=0;n<600&&!toad_cleared;n++){
                tick();
                toad_cleared=melee_web_test_item_count(It_Kind_Peach_Toad)==0&&
                    match.player_stats(0).ground_or_air==0&&
                    match.player_stats(0).motion_id<ftPe_MS_Float;
            }
            check(toad_cleared,"Peach Toad did not end through its source lifetime");
            // Aerial neutral: the same counter in the air, then land.
            jump_peach();
            bool air_toad=false;
            for(unsigned n=0;n<200&&!air_toad;n++){
                raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                air_toad|=motion>=ftPe_MS_SpecialAirN&&motion<=ftPe_MS_SpecialAirNHit;
            }
            raw[0].button=0;
            check(air_toad,"Peach aerial neutral special did not enter its source motion");
            for(unsigned n=0;n<600&&match.player_stats(0).ground_or_air!=0;n++)tick();
            settle_peach();
            std::cout<<"Peach ground/aerial Toad counter lifetimes passed"<<std::endl;
            // Down special: the vegetable pull spawns the authored turnip
            // article with its model, material states and its own command
            // stream. The pull keeps the vegetable held; its lifetime tears
            // it down afterwards.
            for(bool air:{false,true}){
                if(air)jump_peach();else settle_peach();
                bool veg_entered=false,veg_live=false;
                for(unsigned n=0;n<240&&!(veg_entered&&veg_live);n++){
                    raw[0].button=n%8==0?PAD_BUTTON_B:0;
                    raw[0].stickX=0;raw[0].stickY=-80;tick();
                    const auto motion=match.player_stats(0).motion_id;
                    veg_entered|=motion==ftPe_MS_SpecialLw||motion==ftPe_MS_SpecialAirLw;
                    veg_live|=melee_web_test_item_count(It_Kind_Peach_Turnip)>0;
                }
                raw[0].button=0;raw[0].stickY=0;
                check(veg_entered&&veg_live,
                      "Peach down special did not pull its original vegetable article");
                for(unsigned n=0;n<600&&match.player_stats(0).ground_or_air!=0;n++)tick();
                settle_peach();
                // A held vegetable never expires; the source light-item throw
                // releases it and the authored turnip lifetime tears the free
                // item down.
                raw[0].button=PAD_BUTTON_A;tick();raw[0].button=0;
                for(unsigned n=0;n<900&&melee_web_test_item_count(It_Kind_Peach_Turnip)>0;n++)tick();
                check(melee_web_test_item_count(It_Kind_Peach_Turnip)==0,
                      "Peach vegetable did not end through its source lifetime");
                settle_peach();
                std::cout<<"Peach "<<(air?"air":"ground")<<" vegetable pull lifetime passed"<<std::endl;
            }
            // Side special: the Bomber ground/air states with no owned item.
            for(bool air:{false,true}){
                if(air)jump_peach();else settle_peach();
                bool entered=false;
                for(unsigned n=0;n<200&&!entered;n++){
                    raw[0].button=n%8==0?PAD_BUTTON_B:0;
                    raw[0].stickX=air?-80:80;raw[0].stickY=0;tick();
                    const auto motion=match.player_stats(0).motion_id;
                    entered=(motion>=ftPe_MS_SpecialSStart&&motion<=ftPe_MS_SpecialSJump)||
                            (motion>=ftPe_MS_SpecialAirSStart&&motion<=ftPe_MS_SpecialAirSJump);
                }
                raw[0].button=0;raw[0].stickX=0;
                check(entered,"Peach side special did not enter its ground/air source motion");
                for(unsigned n=0;n<600&&match.player_stats(0).ground_or_air!=0;n++)tick();
                settle_peach();
                std::cout<<"Peach "<<(air?"air":"ground")<<" Bomber state passed"<<std::endl;
            }
            // Up special: the Parasol opens its owned article for the rise and
            // tears it down through the source landing.
            for(bool air:{false,true}){
                if(air)jump_peach();else settle_peach();
                bool entered=false,parasol_live=false;
                for(unsigned n=0;n<240&&!(entered&&parasol_live);n++){
                    raw[0].button=n%8==0?PAD_BUTTON_B:0;
                    raw[0].stickX=0;raw[0].stickY=air?80:80;tick();
                    const auto motion=match.player_stats(0).motion_id;
                    entered=motion==ftPe_MS_SpecialHiStart||motion==ftPe_MS_SpecialHiEnd||
                            motion==ftPe_MS_SpecialAirHiStart||motion==ftPe_MS_SpecialAirHiEnd;
                    parasol_live|=melee_web_test_item_count(It_Kind_Peach_Parasol)>0;
                }
                raw[0].button=0;raw[0].stickY=0;
                check(entered,"Peach up special did not enter its ground/air source motion");
                check(parasol_live,"Peach up special did not open its original Parasol article");
                for(unsigned n=0;n<900&&match.player_stats(0).ground_or_air!=0;n++)tick();
                for(unsigned n=0;n<600&&melee_web_test_item_count(It_Kind_Peach_Parasol)>0;n++)tick();
                check(melee_web_test_item_count(It_Kind_Peach_Parasol)==0,
                      "Peach Parasol article did not end through its source lifetime");
                settle_peach();
                std::cout<<"Peach "<<(air?"air":"ground")<<" Parasol lifetime passed"<<std::endl;
            }
            // The float: checkStartFloatInput requires holding X/Y with the
            // stick beyond the down threshold, and ftCo_Jump/ftCo_Fall probe
            // it during ascent, so hold both through the jump.
            jump_peach();
            bool float_seen=false;
            for(unsigned n=0;n<300&&!float_seen;n++){
                raw[0].button=PAD_BUTTON_X;raw[0].stickY=-80;tick();
                const auto motion=match.player_stats(0).motion_id;
                float_seen|=motion>=ftPe_MS_Float&&motion<=ftPe_MS_FloatAttackAirLw;
            }
            raw[0].button=0;raw[0].stickY=0;
            check(float_seen,"Peach holding down+jump did not enter the authored Float state");
            for(unsigned n=0;n<600&&match.player_stats(0).ground_or_air!=0;n++)tick();
            settle_peach();
            std::cout<<"Peach original N/air-N/Float/Lw/S/Hi and vegetable/Toad/Parasol article lifetimes passed"<<std::endl;
        }else if(cycle==0&&luigi){
            auto settle_luigi=[&](){
                raw[0].button=0;raw[0].stickX=raw[0].stickY=0;
                for(unsigned n=0;n<600;n++){
                    const auto state=match.player_stats(0);
                    if(state.ground_or_air==0&&state.motion_id<ftLg_MS_SpecialN)return;
                    tick();
                }
                check(false,"Luigi special did not return to grounded common motion");
            };
            auto jump_luigi=[&](){
                raw[0].button=0;raw[0].stickX=raw[0].stickY=0;
                for(unsigned n=0;n<120&&match.player_stats(0).ground_or_air==0;n++){
                    raw[0].button=n==0?PAD_BUTTON_X:0;tick();
                }
                check(match.player_stats(0).ground_or_air!=0,
                      "Luigi did not enter the authored aerial state for air neutral special");
            };
            settle_luigi();
            jump_luigi();
            bool air_n=false,air_fireball_live=false;
            /* Entering the authored AirN motion precedes its animation command
             * that creates the fireball. Keep observing after state entry so
             * this fixture proves the source article lifecycle rather than
             * treating the state transition as the spawn boundary. */
            for(unsigned n=0;n<180&&!air_fireball_live;n++){
                raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                const auto state=match.player_stats(0);
                if(n<12)std::cout<<"Luigi air-N input tick="<<n<<" button="<<raw[0].button
                             <<" motion="<<state.motion_id<<" ground="<<state.ground_or_air<<std::endl;
                air_n|=state.motion_id==ftLg_MS_SpecialAirN;
                air_fireball_live|=melee_web_test_item_count(It_Kind_Luigi_Fire)>0;
            }
            raw[0].button=0;
            check(air_n,"Luigi aerial neutral special did not enter its original source state");
            check(air_fireball_live,"Luigi aerial neutral special did not create its original fireball article");
            bool air_fireball_cleared=false;
            for(unsigned n=0;n<600&&!air_fireball_cleared;n++){
                tick();air_fireball_cleared=melee_web_test_item_count(It_Kind_Luigi_Fire)==0;
            }
            check(air_fireball_cleared,"Luigi aerial neutral special did not destroy its original fireball article");
            settle_luigi();

            const unsigned no_state=static_cast<unsigned>(-1);
            unsigned side_state=no_state;
            for(unsigned n=0;n<240&&side_state==no_state;n++){
                raw[0].stickX=80;raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                if(motion==ftLg_MS_SpecialSStart||motion==ftLg_MS_SpecialSHold||
                   motion==ftLg_MS_SpecialS2||motion==ftLg_MS_SpecialSEnd||
                   motion==ftLg_MS_SpecialS||motion==ftLg_MS_SpecialSMisfire)
                    side_state=motion;
            }
            raw[0].stickX=0;raw[0].button=0;
            check(side_state!=no_state,
                  "Luigi side special did not enter its original normal or misfire state");
            for(unsigned n=0;n<600;n++){
                const auto motion=match.player_stats(0).motion_id;
                if(match.player_stats(0).ground_or_air==0&&motion<ftLg_MS_SpecialN)break;
                tick();
            }
            settle_luigi();

            bool up=false;
            for(unsigned n=0;n<180&&!up;n++){
                raw[0].stickY=80;raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                up=motion==ftLg_MS_SpecialHi||motion==ftLg_MS_SpecialAirHi;
            }
            raw[0].stickY=0;raw[0].button=0;
            check(up,"Luigi up special did not enter its original source state");
            settle_luigi();

            bool down=false;
            for(unsigned n=0;n<180&&!down;n++){
                raw[0].stickY=-80;raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                down=motion==ftLg_MS_SpecialLw||motion==ftLg_MS_SpecialAirLw;
            }
            raw[0].stickY=0;raw[0].button=0;
            check(down,"Luigi down special did not enter its original source state");
            settle_luigi();
            std::cout<<"Luigi ground/air N article lifecycle, RNG-preserving S state="
                     <<side_state<<", Hi and Lw source states passed"<<std::endl;
        }else if(cycle==0&&pikachu_family){
            auto pika_tick=[&](){
                tick();
                if(pichu)
                    pichu_self_damage_seen|=
                        match.player_stats(0).damage_percent>
                        pikachu_damage_before_specials+0.001f;
            };
            auto settle_pikachu=[&](){
                raw[0].button=0;raw[0].stickX=raw[0].stickY=0;
                for(unsigned n=0;n<720;n++){
                    const auto state=match.player_stats(0);
                    if(state.ground_or_air==0&&state.motion_id<ftPk_MS_SpecialN)
                        return;
                    pika_tick();
                }
                check(false,
                      "Pikachu-family special did not return to grounded common motion");
            };

            settle_pikachu();
            bool side=false;
            for(unsigned n=0;n<240&&!side;n++){
                raw[0].stickX=80;raw[0].button=n%8==0?PAD_BUTTON_B:0;
                pika_tick();
                const auto motion=match.player_stats(0).motion_id;
                // The source S state machine has authored charge/release
                // stages; accept any of its ground or air motion states.
                side=(motion>=ftPk_MS_SpecialSStart&&motion<=ftPk_MS_SpecialS0)||
                    (motion>=ftPk_MS_SpecialAirSStart&&motion<=ftPk_MS_SpecialAirS0);
            }
            raw[0].stickX=0;raw[0].button=0;
            check(side,
                  "Pikachu-family side special did not enter its original source state");
            settle_pikachu();

            bool up=false;
            for(unsigned n=0;n<240&&!up;n++){
                raw[0].stickY=80;raw[0].button=n%8==0?PAD_BUTTON_B:0;
                pika_tick();
                const auto motion=match.player_stats(0).motion_id;
                up=(motion>=ftPk_MS_SpecialHiStart0&&motion<=ftPk_MS_SpecialHiEnd)||
                    (motion>=ftPk_MS_SpecialAirHiStart0&&motion<=ftPk_MS_SpecialAirHiEnd);
            }
            raw[0].stickY=0;raw[0].button=0;
            check(up,
                  "Pikachu-family up special did not enter its original source state");
            settle_pikachu();

            bool down=false,thunder_live=false;
            for(unsigned n=0;n<720;n++){
                raw[0].stickY=-80;raw[0].button=n%8==0?PAD_BUTTON_B:0;
                pika_tick();
                const auto motion=match.player_stats(0).motion_id;
                down|=(motion>=ftPk_MS_SpecialLwStart&&motion<=ftPk_MS_SpecialLwEnd)||
                    (motion>=ftPk_MS_SpecialAirLwStart&&motion<=ftPk_MS_SpecialAirLwEnd);
                thunder_live|=melee_web_test_item_count(pikachu_thunder_kind)>0;
                if(down&&thunder_live&&n>120)break;
            }
            raw[0].stickY=0;raw[0].button=0;
            check(down,
                  "Pikachu-family down special did not enter its original source state");
            check(thunder_live,
                  "Pikachu-family down special did not create its authored Thunder article");
            for(unsigned n=0;n<720&&melee_web_test_item_count(pikachu_thunder_kind)>0;n++)
                pika_tick();
            check(melee_web_test_item_count(pikachu_thunder_kind)==0,
                  "Pikachu-family down special did not tear down its Thunder article");
            if(pichu)
                check(pichu_self_damage_seen,
                      "Pichu authored special command did not apply source self damage");
            std::cout<<fighter_content->name
                     <<" original N/air-N/S/Hi/Lw Article lifecycle passed"
                     <<(pichu?" with source self damage":"")<<std::endl;
        }else if(cycle==0&&mars_family){
            raw[0].button=0;raw[0].stickX=raw[0].stickY=0;
            for(unsigned n=0;n<420;n++){
                const auto state=match.player_stats(0);
                if(state.ground_or_air==0&&(state.motion_id<ftMs_MS_SpecialNStart||
                   state.motion_id>ftMs_MS_SpecialAirLwHit))break;
                tick();
            }
            check(match.player_stats(0).ground_or_air==0,
                  fighter_content->fighter_kind==FTKIND_EMBLEM?
                  "Roy did not land after the aerial neutral special":
                  "Marth did not land after the aerial neutral special");
            bool dancing_blade=false,dancing_blade_chain=false;
            for(unsigned n=0;n<180&&!dancing_blade_chain;n++){
                raw[0].stickX=80;raw[0].button=n%7==0?PAD_BUTTON_B:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                dancing_blade|=motion==ftMs_MS_SpecialS1;
                dancing_blade_chain|=motion>=ftMs_MS_SpecialS2Hi&&motion<=ftMs_MS_SpecialS4Lw;
            }
            raw[0].stickX=0;raw[0].button=0;
            check(dancing_blade&&dancing_blade_chain,
                  fighter_content->fighter_kind==FTKIND_EMBLEM?
                  "Roy Dancing Blade did not enter and chain its original shared Mars states":
                  "Marth Dancing Blade did not enter and chain its original source states");
            for(unsigned n=0;n<300;n++){
                const auto motion=match.player_stats(0).motion_id;
                if(motion<ftMs_MS_SpecialS1||motion>ftMs_MS_SpecialAirS4Lw)break;
                tick();
            }
            bool dolphin_slash=false;
            for(unsigned n=0;n<120&&!dolphin_slash;n++){
                raw[0].stickY=80;raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                const auto motion=match.player_stats(0).motion_id;
                dolphin_slash=motion==ftMs_MS_SpecialHi||motion==ftMs_MS_SpecialAirHi;
            }
            raw[0].stickY=0;raw[0].button=0;
            check(dolphin_slash,fighter_content->fighter_kind==FTKIND_EMBLEM?
                  "Roy Dolphin Slash did not enter its original shared Mars state":
                  "Marth Dolphin Slash did not enter its original source state");
            for(unsigned n=0;n<420;n++){
                const auto state=match.player_stats(0);
                if(state.ground_or_air==0&&(state.motion_id<ftMs_MS_SpecialNStart||
                   state.motion_id>ftMs_MS_SpecialAirLwHit))break;
                tick();
            }
            check(match.player_stats(0).ground_or_air==0,fighter_content->fighter_kind==FTKIND_EMBLEM?
                  "Roy did not land after Dolphin Slash":"Marth did not land after Dolphin Slash");
            bool counter=false;
            for(unsigned n=0;n<120&&!counter;n++){
                raw[0].stickY=-80;raw[0].button=n%8==0?PAD_BUTTON_B:0;tick();
                counter=match.player_stats(0).motion_id==ftMs_MS_SpecialLw;
            }
            raw[0].stickY=0;raw[0].button=0;
            check(counter,fighter_content->fighter_kind==FTKIND_EMBLEM?
                  "Roy Counter did not enter its original shared Mars state":
                  "Marth Counter did not enter its original source state");
        }
        }
        }
        check(melee_web_test_quake_start(2),"Original authored stage quake was not created");
        bool translated=false;
        for(unsigned n=0;n<8;n++){tick();translated|=melee_web_test_quake_translated()!=0;}
        check(translated,"Authored quake animation did not translate the original camera");
        raw[0].button=PAD_BUTTON_START;tick();raw[0].button=0;
        for(unsigned n=0;n<30&&!match.paused();n++)tick();
        check(match.paused(),"Source pause did not engage");
        for(unsigned n=0;n<15;n++)tick();
        raw[0].button=PAD_TRIGGER_L|PAD_TRIGGER_R|PAD_BUTTON_A|PAD_BUTTON_START;tick();raw[0].button=0;
        for(unsigned n=0;n<500&&!match.complete();n++)tick();
        check(match.complete(),"Original No Contest did not end the mixed match");
        if(fighter_content->fighter_kind==FTKIND_LUIGI)
            check(match.player_stats(0).stocks==4&&match.player_stats(1).stocks==4,
                       "No Contest changed Luigi source stocks before teardown");
        // Keep every original variant live across close, including the loop
        // owned by Camera::xA0. The next costume reconstructs the same world.
        for(int variant=1;variant<=4;variant++)
            check(melee_web_test_quake_start(variant),"Stage quake variant is missing");
        match.close();match.close();
    }
    std::cout<<(platform_pass?
        "Donkey platform Pass, costumes, pause and repeat teardown passed\n":entry_only?
        "Source content entry, costumes, stage lifecycle, pause and repeat teardown passed\n":
        "Mixed source content intro, costumes, stage lifecycle, combat, pause and repeat teardown passed\n");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
