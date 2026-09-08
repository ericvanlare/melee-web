#include "gameplay_match_session.hpp"
#include "gameplay_audio_bank.hpp"
#include "gameplay_audio_stream_asset.hpp"
#include "gameplay_match_context.h"
#include "gameplay_match_rules.h"
#include "gameplay_render.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
extern "C" int lbAudioAx_80023F28(int);
namespace melee_web {
namespace {void check(int value,const char* error){if(!value)throw std::runtime_error(error);}}
struct GameplayMatchSession::Storage {
    std::unique_ptr<GameplayWorld> world;
    std::unique_ptr<GameplayAudioBank> bank;
    std::unique_ptr<GameplayAudioStream> music;
    MeleeWebMatchContext* match=nullptr;
    MeleeWebRender* render=nullptr;
    ~Storage(){try{close();}catch(const std::exception& e){std::fprintf(stderr,"Match session teardown: %s\n",e.what());std::abort();}}
    void start(const RuntimeFiles& files,const MeleeWebMenuMatchSelection& selection){
        for(unsigned i=0;i<2;i++)
            check(selection.players[i].controller==i&&selection.players[i].stocks==4&&
                  selection.players[i].costume<5&&selection.players[i].sub_color<=4,
                  "Match requires the supported original four-stock menu selection");
        world=std::make_unique<GameplayWorld>(files);char error[256]{};
        bank=std::make_unique<GameplayAudioBank>(files.at("smash2.sem"),
            std::vector<std::span<const uint8_t>>{files.at("main.ssm"),files.at("mario.ssm")},files.at("dsp_coef.bin"));
        check(melee_web_audio_enable_effects(bank->get(),error,sizeof(error)),error);
        music=std::make_unique<GameplayAudioStream>(bank->get(),"/audio/sp_end.hps",files.at("sp_end.hps"));
        check(lbAudioAx_80023F28(78)==0,"Original Final Destination music did not start");
        MeleeWebPlayerSettings players[2]{};
        for(unsigned i=0;i<2;i++){
            const auto spawn=world->player_spawn(i);const auto& selected=selection.players[i];
            players[i]={i,selected.controller,selected.stocks,{spawn[0],spawn[1],spawn[2]},spawn[0]<0?1.0f:-1.0f,
                        selected.costume,selected.sub_color};
        }
        match=melee_web_match_begin_players(players,2,70,selection.random_seed,world->collision(),error,sizeof(error));check(match!=nullptr,error);
        check(melee_web_match_create_fighters(match,error,sizeof(error)),error);
        MeleeWebRenderSettings settings{640,480,{0,25,180},{0,15,0},30,1,1000,(uint64_t(1)<<5)|(uint64_t(1)<<3)};
        render=melee_web_render_begin_match(&settings,error,sizeof(error));check(render!=nullptr,error);
        world->enable_full_stage();check(melee_web_render_use_match_passes(render,error,sizeof(error)),error);
    }
    void close(){
        char error[256]{};
        if(world)world->end_stage();
        if(render){check(melee_web_render_end(render,error,sizeof(error)),error);render=nullptr;}
        if(match){check(melee_web_match_end(match,error,sizeof(error)),error);match=nullptr;}
        music.reset();
        if(world){world->verify_immutable_archives();world->close();world.reset();}
        bank.reset();
    }
};
GameplayMatchSession::GameplayMatchSession(const RuntimeFiles& files,const MeleeWebMenuMatchSelection& selection)
    :storage_(std::make_unique<Storage>()){storage_->start(files,selection);}
GameplayMatchSession::~GameplayMatchSession()=default;
void GameplayMatchSession::close(){if(storage_){storage_->close();storage_.reset();}}
void GameplayMatchSession::tick(const PADStatus raw[4]){
    check(storage_&&storage_->match,"Match session is closed");char error[256]{};
    check(melee_web_match_step_raw(storage_->match,raw,error,sizeof(error)),error);
}
void GameplayMatchSession::draw(){
    check(storage_&&storage_->render,"Match render session is closed");char error[256]{};
    check(melee_web_render_draw(storage_->render,error,sizeof(error)),error);
}
int GameplayMatchSession::outcome(int& winner)const{
    check(storage_&&storage_->match,"Match session is closed");return melee_web_match_rules_outcome(&winner);
}
uint32_t GameplayMatchSession::random_seed()const{
    check(storage_&&storage_->match,"Match session is closed");char error[256]{};MeleeWebMatchStats stats{};
    check(melee_web_match_stats(storage_->match,&stats,error,sizeof(error)),error);return stats.random_seed;
}
MeleeWebMatchStats GameplayMatchSession::player_stats(unsigned index)const{
    check(storage_&&storage_->match,"Match session is closed");char error[256]{};MeleeWebMatchStats stats{};
    check(melee_web_match_player_stats(storage_->match,index,&stats,error,sizeof(error)),error);return stats;
}
MeleeWebAudio* GameplayMatchSession::audio()const{return storage_&&storage_->bank?storage_->bank->get():nullptr;}
}
