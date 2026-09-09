#include "gameplay_match_session.hpp"
#include "runtime_archive_cache.hpp"
#include "gameplay_content.h"
#include "gameplay_audio_bank.hpp"
#include "gameplay_audio_stream_asset.hpp"
#include "gameplay_match_context.h"
#include "gameplay_match_rules.h"
#include "gameplay_render.h"
#include "gameplay_hud.h"
#include "gameplay_match_flow.h"
#include "gameplay_hud_assets.hpp"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <set>
extern "C" int lbAudioAx_80023F28(int);
extern "C" int melee_web_vs_mode_begin(void);
extern "C" int melee_web_vs_mode_end(void);
namespace melee_web {
namespace {void check(int value,const char* error){if(!value)throw std::runtime_error(error);}}
struct GameplayMatchSession::Storage {
    std::unique_ptr<GameplayWorld> world;
    std::unique_ptr<GameplayAudioBank> bank;
    std::unique_ptr<GameplayAudioStream> music;
    std::string music_path;
    MeleeWebMatchContext* match=nullptr;
    MeleeWebRender* render=nullptr;
    std::unique_ptr<GameplayHudAssets> hud_assets;
    MeleeWebHud* hud=nullptr;
    MeleeWebMatchFlow* flow=nullptr;
    bool mode_owned=false;
    ~Storage(){try{close();}catch(const std::exception& e){std::fprintf(stderr,"Match session teardown: %s\n",e.what());std::abort();}}
    void start(const RuntimeFiles& files,const MeleeWebMenuMatchSelection& selection,
               RuntimeArchiveCache* archive_cache){
        const auto* stage=melee_web_stage_content(selection.start.rules.stkind);
        check(stage!=nullptr,"Match stage has no source runtime owner");
        GameplayWorldSelection content{};content.ground_kind=stage->ground_kind;
        for(unsigned i=0;i<2;i++){
            const auto* fighter=melee_web_fighter_content(selection.start.players[i].ckind);
            check(fighter&&selection.players[i].controller==i&&selection.players[i].stocks==4&&
                  selection.players[i].costume<fighter->costumes&&selection.players[i].sub_color<=4,
                  "Match requires the supported original four-stock menu selection");
            content.fighter_kinds[i]=fighter->fighter_kind;
        }
        check(melee_web_vs_mode_begin(),"Original VS mode is already owned");mode_owned=true;
        world=archive_cache?std::make_unique<GameplayWorld>(files,content,*archive_cache):
                            std::make_unique<GameplayWorld>(files,content);
        char error[256]{};
        hud_assets=archive_cache?std::make_unique<GameplayHudAssets>(files,*archive_cache):
                                 std::make_unique<GameplayHudAssets>(files);
        std::vector<std::span<const uint8_t>> banks;
        for(const char* name:{"main.ssm","nr_select.ssm","nr_title.ssm",
                              "nr_name.ssm","pokemon.ssm","end.ssm"})banks.emplace_back(files.at(name));
        std::set<std::string_view> fighter_banks;
        for(const auto kind:content.fighter_kinds){
            const auto* dependency=melee_web_fighter_content_by_kind(kind);
            if(fighter_banks.insert(dependency->audio_bank).second)banks.emplace_back(files.at(dependency->audio_bank));
        }
        bank=std::make_unique<GameplayAudioBank>(files.at("smash2.sem"),banks,files.at("dsp_coef.bin"));
        check(melee_web_audio_enable_effects(bank->get(),error,sizeof(error)),error);
        music_path="/audio/"+std::string(stage->music);
        music=std::make_unique<GameplayAudioStream>(bank->get(),music_path.c_str(),files.at(stage->music));
        check(lbAudioAx_80023F28(stage->music_id)==0,"Original selected stage music did not start");
        MeleeWebPlayerSettings players[2]{};
        for(unsigned i=0;i<2;i++){
            const auto spawn=world->player_spawn(i);const auto& selected=selection.players[i];
            players[i]={i,selected.controller,selected.stocks,{spawn[0],spawn[1],spawn[2]},spawn[0]<0?1.0f:-1.0f,
                        selected.costume,selected.sub_color,content.fighter_kinds[i]};
        }
        match=melee_web_match_begin_players(players,2,70,selection.random_seed,world->collision(),error,sizeof(error));check(match!=nullptr,error);
        world->enable_full_stage(true);
        world->initialize_match(selection.start);
        check(melee_web_match_create_fighters_intro(match,error,sizeof(error)),error);
        MeleeWebRenderSettings settings{640,480,{0,25,180},{0,15,0},30,1,1000,(uint64_t(1)<<5)|(uint64_t(1)<<3)};
        render=melee_web_render_begin_match(&settings,error,sizeof(error));check(render!=nullptr,error);
        check(melee_web_render_use_match_passes(render,error,sizeof(error)),error);
        hud=melee_web_hud_begin(selection.hud_layout,error,sizeof(error));check(hud!=nullptr,error);
        check(melee_web_render_use_scene_cameras(render,error,sizeof(error)),error);
        flow=melee_web_match_flow_begin(error,sizeof(error));check(flow!=nullptr,error);
    }
    void close(){
        char error[256]{};
        if(flow){check(melee_web_match_flow_end(flow,error,sizeof(error)),error);flow=nullptr;}
        if(hud){check(melee_web_hud_end(hud,error,sizeof(error)),error);hud=nullptr;}
        if(world)world->end_stage();
        if(render){check(melee_web_render_end(render,error,sizeof(error)),error);render=nullptr;}
        if(match){check(melee_web_match_end(match,error,sizeof(error)),error);match=nullptr;}
        music.reset();
        if(world){world->verify_immutable_archives();world->close();world.reset();}
        if(hud_assets){hud_assets->close();hud_assets.reset();}
        bank.reset();
        if(mode_owned){check(melee_web_vs_mode_end(),"Original VS mode lost ownership");mode_owned=false;}
    }
};
GameplayMatchSession::GameplayMatchSession(const RuntimeFiles& files,const MeleeWebMenuMatchSelection& selection)
    :storage_(std::make_unique<Storage>()){storage_->start(files,selection,nullptr);}
GameplayMatchSession::GameplayMatchSession(const RuntimeFiles& files,
                                           const MeleeWebMenuMatchSelection& selection,
                                           RuntimeArchiveCache& archive_cache)
    :storage_(std::make_unique<Storage>()){storage_->start(files,selection,&archive_cache);}
GameplayMatchSession::~GameplayMatchSession()=default;
void GameplayMatchSession::close(){if(storage_){storage_->close();storage_.reset();}}
void GameplayMatchSession::tick(const PADStatus raw[4]){
    check(storage_&&storage_->match,"Match session is closed");char error[256]{};
    check(melee_web_match_step_raw_phased(storage_->match,raw,melee_web_match_flow_renew,melee_web_match_flow_pre,melee_web_match_flow_post,storage_->flow,error,sizeof(error)),error);
}
void GameplayMatchSession::draw(){
    check(storage_&&storage_->render,"Match render session is closed");char error[256]{};
    check(melee_web_render_draw(storage_->render,error,sizeof(error)),error);
    check(melee_web_match_flow_present(storage_->flow,error,sizeof(error)),error);
}
bool GameplayMatchSession::ending()const{return storage_&&melee_web_match_flow_ending(storage_->flow);}
bool GameplayMatchSession::complete()const{return storage_&&melee_web_match_flow_complete(storage_->flow);}
bool GameplayMatchSession::paused()const{return storage_&&melee_web_match_flow_paused(storage_->flow);}
uint32_t GameplayMatchSession::source_frames()const{return storage_?melee_web_match_flow_frames(storage_->flow):0;}
int GameplayMatchSession::hud_damage(unsigned player)const{return storage_?melee_web_hud_damage(storage_->hud,player):-1;}
bool GameplayMatchSession::ready()const{return storage_&&melee_web_hud_ready(storage_->hud);}
int GameplayMatchSession::outcome(int& winner)const{
    check(storage_&&storage_->match,"Match session is closed");
    const int result=melee_web_match_flow_result(storage_->flow);
    if(result==OUTCOME_NO_CONTEST){winner=-1;return result;}
    return melee_web_match_rules_outcome(&winner);
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
