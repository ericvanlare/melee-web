#include "gameplay_world.hpp"
#include "gameplay_audio_bank.hpp"
#include "gameplay_audio_stream_asset.hpp"
extern "C" int lbAudioAx_80023F28(int);
#include "gameplay_match_context.h"
#include "gameplay_match_rules.h"
#include "gameplay_render.h"
#include "browser_input.h"
#include "animation_clock.hpp"
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <dolphin/gx.h>
#include <emscripten.h>
#include <SDL3/SDL_hints.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
using namespace melee_web;
namespace {
RuntimeFiles files;
std::unique_ptr<GameplayWorld> world;
std::unique_ptr<GameplayAudioBank> audio_bank;
std::unique_ptr<GameplayAudioStream> music;
unsigned audio_phase=0;
std::array<float,1068> audio_pcm;
MeleeWebMatchContext* match=nullptr;
MeleeWebRender* render=nullptr;
FixedTickClock simulation_clock;
std::string message="Import local runtime assets to launch.";
bool running=false,ready=false,exiting=false;
unsigned frames=0;
bool finished=false;
int winner=-1;
int combat_check=-1;
alignas(32) unsigned char fifo_buffer[64*1024];
constexpr std::array<std::string_view,15> required={"PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","ItCo.usd","EfMrData.dat","EfCoData.dat","PdPm.dat","sislib_font.bin","smash2.sem","main.ssm","mario.ssm","dsp_coef.bin","sp_end.hps"};
void require(int value,const char* error){if(!value)throw std::runtime_error(error);}
void close_game(){
    running=false;finished=false;winner=-1;combat_check=-1;simulation_clock.reset();char error[256];
    if(world)world->end_stage();
    if(render){require(melee_web_render_end(render,error,sizeof(error)),error);render=nullptr;}
    if(match){require(melee_web_match_end(match,error,sizeof(error)),error);match=nullptr;}
    music.reset();
    if(world){world->close();world.reset();}
    audio_bank.reset();audio_phase=0;
}
void log_message(AuroraLogLevel level,const char* module,const char* text,unsigned length){
    std::fprintf(level>=LOG_ERROR?stderr:stdout,"[%s] %.*s\n",module,int(length),text);
    if(level==LOG_FATAL)std::abort();
}
void tick(){
    EM_ASM({if(window.runtimeServiceCommands)window.runtimeServiceCommands();});
    EM_ASM({window.runtimeBoundary="update";});
    const double started=emscripten_get_now();
    for(const AuroraEvent* event=aurora_update();event&&event->type!=AURORA_NONE;++event)
        if(event->type==AURORA_EXIT)exiting=true;
    EM_ASM({window.runtimeBoundary="input";});
    const auto* input=melee_web_input_poll();
    if(exiting){close_game();melee_web_input_shutdown();aurora_shutdown();emscripten_cancel_main_loop();return;}
    try{
        EM_ASM({window.runtimeBoundary="simulation";});
        const auto elapsed=simulation_clock.tick(started,running&&match&&input->visible);
        if(elapsed.stalled){running=false;message="Paused after a long frame. Resume to continue without skipping simulation ticks.";}
        char error[256];
        for(unsigned step=0;step<elapsed.steps;++step){
            PADStatus scripted[4]{};
            const PADStatus* sample=input->raw;
            if(combat_check>=0){
                // Diagnostic fixture only. Original PAD processing, actions,
                // item simulation and drawing remain the production path.
                if(combat_check>=30&&combat_check<33)scripted[0].button=PAD_BUTTON_B;
                if(combat_check>=105&&combat_check<113)scripted[0].button=PAD_BUTTON_X;
                if(combat_check>=115&&combat_check<118)scripted[0].button=PAD_BUTTON_B;
                sample=scripted;
            }
            require(melee_web_match_step_raw(match,sample,error,sizeof(error)),error);
            if(combat_check>=0&&++combat_check==240){
                MeleeWebMatchStats opponent{};
                require(melee_web_match_player_stats(match,1,&opponent,error,sizeof(error)),error);
                message="Combat check complete: opponent damage "+std::to_string(opponent.damage_percent)+"%. Drawing and impact still require verification.";
                combat_check=-1;
            }
            audio_phase+=32000;const unsigned samples=audio_phase/60;audio_phase%=60;
            require(melee_web_audio_render(audio_bank->get(),audio_pcm.data(),samples,error,sizeof(error)),error);
            EM_ASM({if(window.runtimeAudio)window.runtimeAudio(HEAPF32.slice($0>>2,($0>>2)+$1*2));},audio_pcm.data(),samples);
            if(melee_web_match_rules_outcome(&winner)){
                finished=true;running=false;simulation_clock.reset();
                message=winner>=0?"Game! Player "+std::to_string(winner+1)+" wins. Restart to play again.":"Game! Restart to play again.";
                break;
            }
        }
        EM_ASM({window.runtimeBoundary="begin frame";});
        if(aurora_begin_frame()){
            EM_ASM({window.runtimeBoundary="draw";});
            GXSetCopyClear(GXColor{16,20,30,255},GX_MAX_Z24);
            // Aurora may yield through Asyncify while submitting. Never carry
            // a C++ exception across that suspension boundary.
            int drawn=1;
            if(render)drawn=melee_web_render_draw(render,error,sizeof(error));
            EM_ASM({window.runtimeBoundary="end frame";});
            aurora_end_frame();++frames;
            require(drawn,error);
        }
    }catch(const std::exception& e){running=false;simulation_clock.reset();message=e.what();}
    EM_ASM({if(window.runtimeFrame)window.runtimeFrame($0,$1,$2);},frames,started,emscripten_get_now()-started);
}
}
extern "C" {
int melee_web_game_combat_check(){
    if(!match||!running||finished)return 0;
    combat_check=0;message="Combat check: scripted ground and air B inputs (controllers temporarily overridden).";return 1;
}
int melee_web_game_file(const char* name,const uint8_t* data,uint32_t size){
    try{
        if(world||!name||!data||!size||size>64*1024*1024)throw std::runtime_error("Unload before importing valid runtime files.");
        bool known=false;for(auto required_name:required)known|=required_name==name;
        if(!known)throw std::runtime_error("File is not part of the current runtime bundle.");
        files[std::string(name)]={data,data+size};
        message="Imported "+std::to_string(files.size())+" of "+std::to_string(required.size())+" required files.";return 1;
    }catch(const std::exception& e){message=e.what();return 0;}
}
int melee_web_game_launch(){
    try{
        if(!ready)throw std::runtime_error("WebGPU is still initializing.");
        close_game();world=std::make_unique<GameplayWorld>(files);char error[256];
        audio_bank=std::make_unique<GameplayAudioBank>(files.at("smash2.sem"),
            std::vector<std::span<const uint8_t>>{files.at("main.ssm"),files.at("mario.ssm")},files.at("dsp_coef.bin"));
        require(melee_web_audio_enable_effects(audio_bank->get(),error,sizeof(error)),error);
        music=std::make_unique<GameplayAudioStream>(audio_bank->get(),"/audio/sp_end.hps",files.at("sp_end.hps"));
        require(lbAudioAx_80023F28(78)==0,"Original Final Destination music did not start");
        MeleeWebPlayerSettings players[2]={{0,0,4,{-20,world->floor_height(-20)+1,0},1},{1,1,4,{20,world->floor_height(20)+1,0},-1}};
        match=melee_web_match_begin_players(players,2,70,0x13579bdf,world->collision(),error,sizeof(error));require(match!=nullptr,error);
        require(melee_web_match_create_fighters(match,error,sizeof(error)),error);
        MeleeWebRenderSettings settings{640,480,{0,25,180},{0,15,0},30,1,1000,(uint64_t(1)<<5)|(uint64_t(1)<<3)};
        render=melee_web_render_begin_match(&settings,error,sizeof(error));require(render!=nullptr,error);
        world->enable_full_stage();
        require(melee_web_render_use_match_passes(render,error,sizeof(error)),error);
        running=true;simulation_clock.reset();message="Original two-player runtime running.";return 1;
    }catch(const std::exception& e){message=e.what();running=false;return 0;}
}
int melee_web_game_unload(){
    try{close_game();message="Unloaded. Imported assets are retained for restart.";return 1;}
    catch(const std::exception& e){message=e.what();return 0;}
}
void melee_web_game_pause(int paused){if(finished)return;running=match&&render&&!paused;simulation_clock.reset();message=running?"Original two-player runtime running.":"Paused.";}
const char* melee_web_game_message(){return message.c_str();}
int melee_web_game_running(){return running;}
const char* melee_web_game_stats(){
    static char output[1024];MeleeWebMatchStats players[2]{};char error[256];
    if(!match)return "{\"loaded\":false}";
    for(unsigned i=0;i<2;++i)if(!melee_web_match_player_stats(match,i,&players[i],error,sizeof(error))){message=error;return "{\"loaded\":true,\"error\":true}";}
    std::snprintf(output,sizeof(output),"{\"loaded\":true,\"ticks\":%llu,\"finished\":%s,\"winner\":%d,\"players\":[{\"action\":%d,\"x\":%.9g,\"y\":%.9g,\"percent\":%.9g,\"stocks\":%d,\"shield\":%.9g},{\"action\":%d,\"x\":%.9g,\"y\":%.9g,\"percent\":%.9g,\"stocks\":%d,\"shield\":%.9g}]}",
        (unsigned long long)players[0].ticks,finished?"true":"false",winner,
        players[0].motion_id,players[0].position[0],players[0].position[1],players[0].damage_percent,players[0].stocks,players[0].shield_health,
        players[1].motion_id,players[1].position[0],players[1].position[1],players[1].damage_percent,players[1].stocks,players[1].shield_health);return output;
}
}
int main(int argc,char** argv){
    AuroraConfig config{};config.appName="Melee source runtime";config.desiredBackend=BACKEND_WEBGPU;
    config.windowWidth=640;config.windowHeight=480;config.msaa=1;config.vsync=true;
    config.logCallback=log_message;config.logLevel=LOG_INFO;
    if(!SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT,"#canvas"))return 1;
    aurora_initialize(argc,argv,&config);GXInit(fifo_buffer,sizeof(fifo_buffer));
    if(!melee_web_input_startup())return 1;
    ready=true;emscripten_set_main_loop(tick,0,1);return 0;
}
