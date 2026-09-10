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
#include <aurora/gfx.h>
#include <aurora/main.h>
#include <dolphin/gx.h>
#include <dolphin/vi.h>
#include <emscripten.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_video.h>
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
FixedTickClock simulation_clock{FixedTickClock::OverrunPolicy::CatchUp};
std::string message="Import local runtime assets to launch.";
bool running=false,ready=false,exiting=false;
unsigned frames=0;
unsigned catchup_frames=0,max_backlog_ticks=0;
SDL_Window* runtime_window=nullptr;
int render_scale=1;
uint32_t selected_stocks=4;
bool finished=false;
int winner=-1;
int combat_check=-1,combat_kind=0;
unsigned combat_seen=0;
int stock_check_tick=-1,stock_check_total_ticks=0,stock_check_stocks=4,stock_check_respawns=0,stock_check_result=-1;
bool stock_check_lost=false,stock_check_jump=false;
alignas(32) unsigned char fifo_buffer[64*1024];
constexpr std::array<std::string_view,16> required={"PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","ItCo.usd","EfMrData.dat","EfCoData.dat","PdPm.dat","LbRb.dat","sislib_font.bin","smash2.sem","main.ssm","mario.ssm","dsp_coef.bin","sp_end.hps"};
void require(int value,const char* error){if(!value)throw std::runtime_error(error);}
void close_game(){
    running=false;finished=false;winner=-1;catchup_frames=0;max_backlog_ticks=0;combat_check=-1;stock_check_tick=-1;stock_check_total_ticks=0;stock_check_stocks=4;stock_check_respawns=0;stock_check_result=-1;stock_check_lost=false;stock_check_jump=false;simulation_clock.reset();char error[256];
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
    AuroraStats stats_before{};
    if(const AuroraStats* stats=aurora_get_stats())stats_before=*stats;
    double input_done=started,simulation_done=started,
           begin_done=started,draw_done=started,end_done=started;
    int began=0,drawn=1,timing_valid=1;
    for(const AuroraEvent* event=aurora_update();event&&event->type!=AURORA_NONE;++event){
        if(event->type==AURORA_EXIT)exiting=true;
        if(event->type==AURORA_WINDOW_RESIZED)EM_ASM({
            if(window.runtimeRendererSize)window.runtimeRendererSize($0,$1);
        },event->windowSize.fb_width,event->windowSize.fb_height);
    }
    EM_ASM({window.runtimeBoundary="input";});
    const auto* input=melee_web_input_poll();
    input_done=emscripten_get_now();
    if(exiting){close_game();melee_web_input_shutdown();aurora_shutdown();emscripten_cancel_main_loop();return;}
    try{
        EM_ASM({window.runtimeBoundary="simulation";});
        const auto elapsed=simulation_clock.tick(started,running&&match&&input->visible);
        // Keep source audio ticking, but re-prime browser output after catch-up
        // instead of queuing an entire stalled interval of late sound.
        EM_ASM({if(window.runtimeAudioCatchup)window.runtimeAudioCatchup($0);},elapsed.pending_steps!=0);
        if(elapsed.pending_steps){++catchup_frames;max_backlog_ticks=std::max(max_backlog_ticks,elapsed.pending_steps);}
        if(elapsed.stalled){running=false;message="Paused after prolonged timing disruption (over one second of simulation debt or invalid clock). Resume to continue.";}
        char error[256];
        for(unsigned step=0;step<elapsed.steps;++step){
            PADStatus scripted[4]{};
            const PADStatus* sample=input->raw;
            if(stock_check_tick>=0){
                // Diagnostic fixture only. This is the validated stock-trace
                // recipe: raw sustained right input, then one source X press
                // after each grounded respawn near the edge.
                if(stock_check_tick>=20&&!stock_check_lost)scripted[0].stickX=80;
                if(stock_check_jump)scripted[0].button=PAD_BUTTON_X;
                sample=scripted;
            }else if(combat_check>=0){
                // Diagnostic fixture only. Original PAD processing, actions,
                // item simulation and drawing remain the production path.
                if(combat_check>=30&&combat_check<33)scripted[0].button=PAD_BUTTON_B;
                const int jump_tick=combat_kind?210:105,air_tick=combat_kind?220:115;
                if(combat_check>=jump_tick&&combat_check<jump_tick+8)scripted[0].button=PAD_BUTTON_X;
                if(combat_check>=air_tick&&combat_check<air_tick+3)scripted[0].button=PAD_BUTTON_B;
                if(scripted[0].button==PAD_BUTTON_B){
                    if(combat_kind==1)scripted[0].stickX=80;
                    if(combat_kind==2)scripted[0].stickY=80;
                    if(combat_kind==3)scripted[0].stickY=-80;
                }
                sample=scripted;
            }
            require(melee_web_match_step_raw(match,sample,error,sizeof(error)),error);
            if(stock_check_tick>=0){
                MeleeWebMatchStats players[2]{};
                require(melee_web_match_player_stats(match,0,&players[0],error,sizeof(error)),error);
                require(melee_web_match_player_stats(match,1,&players[1],error,sizeof(error)),error);
                if(players[1].stocks!=4)throw std::runtime_error("Stock check failed: stationary opponent lost a stock");
                stock_check_jump=!stock_check_lost&&stock_check_stocks<4&&players[0].ground_or_air==0&&players[0].position[0]>65;
                if(players[0].stocks<stock_check_stocks){stock_check_lost=true;stock_check_stocks=players[0].stocks;}
                if(stock_check_lost&&players[0].motion_id==14&&players[0].ground_or_air==0){stock_check_lost=false;++stock_check_respawns;}
                ++stock_check_tick;
                stock_check_total_ticks=stock_check_tick;
            }
            if(combat_check>=0&&combat_kind){
                MeleeWebMatchStats actor{};
                require(melee_web_match_player_stats(match,0,&actor,error,sizeof(error)),error);
                const int ground_motion=343+2*combat_kind,air_motion=ground_motion+1;
                if(combat_check<210&&actor.motion_id==ground_motion)combat_seen|=1;
                if(combat_check>=220&&actor.motion_id==air_motion)combat_seen|=2;
            }
            if(combat_check>=0&&++combat_check==(combat_kind?480:240)){
                MeleeWebMatchStats opponent{};
                require(melee_web_match_player_stats(match,1,&opponent,error,sizeof(error)),error);
                if(combat_kind)require(combat_seen==3,"Special check did not observe both ground and air source actions");
                message="Combat check complete: opponent damage "+std::to_string(opponent.damage_percent)+"%. "+(combat_kind?"Ground and air source actions observed. ":"")+"Drawing and impact still require verification.";
                combat_check=-1;
            }
            audio_phase+=32000;const unsigned samples=audio_phase/60;audio_phase%=60;
            require(melee_web_audio_render(audio_bank->get(),audio_pcm.data(),samples,error,sizeof(error)),error);
            EM_ASM({if(window.runtimeAudio)window.runtimeAudio(HEAPF32.slice($0>>2,($0>>2)+$1*2));},audio_pcm.data(),samples);
            if(melee_web_match_rules_outcome(&winner)){
                finished=true;running=false;simulation_clock.reset();
                message=winner>=0?"Game! Player "+std::to_string(winner+1)+" wins. Restart to play again.":"Game! Restart to play again.";
                if(stock_check_tick>=0){
                    const bool passed=winner==1&&stock_check_stocks==0&&stock_check_respawns==3;
                    stock_check_result=passed?1:2;
                    message+=(passed?" Stock check passed: original four-stock elimination and three respawns completed.":" Stock check failed: original stock outcome or respawn count was incorrect.");
                    stock_check_tick=-1;
                }else message=winner>=0?"Game! Player "+std::to_string(winner+1)+" wins. Restart to play again.":"Game! Restart to play again.";
                break;
            }
            if(stock_check_tick>=4000){
                stock_check_result=2;stock_check_tick=-1;finished=true;running=false;simulation_clock.reset();
                message="Stock check failed: no source elimination outcome after 4000 simulation ticks (respawns "+std::to_string(stock_check_respawns)+").";
                break;
            }
        }
        simulation_done=emscripten_get_now();
        EM_ASM({window.runtimeBoundary="begin frame";});
        const int frame_began=aurora_begin_frame();
        begin_done=emscripten_get_now();
        if(frame_began){
            began=1;
            EM_ASM({window.runtimeBoundary="draw";});
            GXSetCopyClear(GXColor{16,20,30,255},GX_MAX_Z24);
            // Aurora may yield through Asyncify while submitting. Never carry
            // a C++ exception across that suspension boundary.
            if(render)drawn=melee_web_render_draw(render,error,sizeof(error));
            draw_done=emscripten_get_now();
            EM_ASM({window.runtimeBoundary="end frame";});
            aurora_end_frame();end_done=emscripten_get_now();++frames;
            require(drawn,error);
        }else{
            draw_done=begin_done;end_done=begin_done;
        }
    }catch(const std::exception& e){
        running=false;simulation_clock.reset();message=e.what();timing_valid=0;
        if(stock_check_tick>=0){stock_check_result=2;stock_check_tick=-1;}
        const double failed_at=emscripten_get_now();
        if(simulation_done<input_done)simulation_done=failed_at;
        if(begin_done<simulation_done)begin_done=simulation_done;
        if(draw_done<begin_done)draw_done=begin_done;
        if(end_done<draw_done)end_done=draw_done;
    }
    const double finished_at=emscripten_get_now();
    AuroraStats stats_after{};
    if(const AuroraStats* stats=aurora_get_stats())stats_after=*stats;
    const int queued_delta=int32_t(stats_after.queuedPipelines)-int32_t(stats_before.queuedPipelines);
    const int created_delta=int32_t(stats_after.createdPipelines)-int32_t(stats_before.createdPipelines);
    // Optional diagnostics only: no-op unless the page installs this callback.
    // Keep this after all native work so the callback observes the complete
    // synchronous WebGPU/pipeline-cache cost of this browser tick.
    EM_ASM({
        if(window.runtimeRenderTiming)window.runtimeRenderTiming({
            frame:$0,started:$1,valid:$2,update_input_ms:$3,simulation_audio_ms:$4,
            begin_ms:$5,draw_ms:$6,end_ms:$7,total_ms:$8,began:$9,drawn:$10,
            queued_delta:$11,created_delta:$12,draw_calls:$13,
            texture_upload_bytes:$14
        });
    },frames,started,timing_valid,input_done-started,simulation_done-input_done,
       begin_done-simulation_done,draw_done-begin_done,end_done-draw_done,
       finished_at-started,began,drawn,queued_delta,created_delta,
       stats_after.drawCallCount,
       stats_after.lastTextureUploadSize);
    EM_ASM({if(window.runtimeFrame)window.runtimeFrame($0,$1,$2);},frames,started,finished_at-started);
}
}
extern "C" {
int melee_web_game_combat_check(int kind){
    if(!match||!running||finished||stock_check_tick>=0||kind<0||kind>3)return 0;
    combat_check=0;combat_kind=kind;combat_seen=0;message="Combat check: scripted ground and air special inputs (controllers temporarily overridden).";return 1;
}
int melee_web_game_stock_check(){
    if(!match||!running||finished||stock_check_tick>=0||combat_check>=0)return 0;
    stock_check_tick=0;stock_check_total_ticks=0;stock_check_stocks=4;stock_check_respawns=0;stock_check_result=0;stock_check_lost=false;stock_check_jump=false;
    message="Stock check: raw sustained right input with source respawns (diagnostic only).";return 1;
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
int melee_web_game_set_stocks(int stocks){
    if(stocks<1||stocks>99){message="Stocks must be between 1 and 99.";return 0;}
    selected_stocks=stocks;return 1;
}
int melee_web_game_set_render_scale(int scale){
    if(scale!=1&&scale!=2){message="Render scale must be 1 or 2.";return 0;}
    render_scale=scale;return 1;
}
static int launch_game(bool fixture){
    try{
        if(!ready)throw std::runtime_error("WebGPU is still initializing.");
        close_game();
        // Scale the internal EFB, not the high-DPI presentation window. A 1x
        // setting must not silently become 2x on a Retina display.
        VISetFrameBufferScale(float(render_scale));
        world=std::make_unique<GameplayWorld>(files);char error[256];
        audio_bank=std::make_unique<GameplayAudioBank>(files.at("smash2.sem"),
            std::vector<std::span<const uint8_t>>{files.at("main.ssm"),files.at("mario.ssm")},files.at("dsp_coef.bin"));
        require(melee_web_audio_enable_effects(audio_bank->get(),error,sizeof(error)),error);
        music=std::make_unique<GameplayAudioStream>(audio_bank->get(),"/audio/sp_end.hps",files.at("sp_end.hps"));
        require(lbAudioAx_80023F28(78)==0,"Original Final Destination music did not start");
        MeleeWebPlayerSettings players[2]{};
        for(unsigned i=0;i<2;++i){
            auto position=world->player_spawn(i);
            if(fixture){position={i?20.0f:-20.0f,0,0};position[1]=world->floor_height(position[0])+1;}
            players[i]={i,i,fixture?4U:selected_stocks,{position[0],position[1],position[2]},position[0]<0?1.0f:-1.0f};
        }
        match=melee_web_match_begin_players(players,2,70,0x13579bdf,world->collision(),error,sizeof(error));require(match!=nullptr,error);
        require(melee_web_match_create_fighters(match,error,sizeof(error)),error);
        MeleeWebRenderSettings settings{640,480,{0,25,180},{0,15,0},30,1,1000,(uint64_t(1)<<5)|(uint64_t(1)<<3)};
        render=melee_web_render_begin_match(&settings,error,sizeof(error));require(render!=nullptr,error);
        world->enable_full_stage();
        require(melee_web_render_use_match_passes(render,error,sizeof(error)),error);
        running=true;simulation_clock.reset();message="Original two-player runtime running.";return 1;
    }catch(const std::exception& e){message=e.what();running=false;return 0;}
}
int melee_web_game_launch(){return launch_game(false);}
int melee_web_game_launch_fixture(){return launch_game(true);}
int melee_web_game_unload(){
    try{close_game();message="Unloaded. Imported assets are retained for restart.";return 1;}
    catch(const std::exception& e){message=e.what();return 0;}
}
void melee_web_game_pause(int paused){if(finished)return;running=match&&render&&!paused;simulation_clock.reset();message=running?"Original two-player runtime running.":"Paused.";}
const char* melee_web_game_message(){return message.c_str();}
int melee_web_game_running(){return running;}
int melee_web_game_cache_idle(){
    const AuroraStats* stats=aurora_get_stats();
    return match==nullptr&&stats!=nullptr&&stats->queuedPipelines==0;
}
const char* melee_web_game_stats(){
    static char output[1024];MeleeWebMatchStats players[2]{};char error[256];
    if(!match)return "{\"loaded\":false}";
    for(unsigned i=0;i<2;++i)if(!melee_web_match_player_stats(match,i,&players[i],error,sizeof(error))){message=error;return "{\"loaded\":true,\"error\":true}";}
    std::snprintf(output,sizeof(output),"{\"loaded\":true,\"ticks\":%llu,\"catchup_frames\":%u,\"max_backlog_ticks\":%u,\"finished\":%s,\"winner\":%d,\"stock_check_active\":%s,\"stock_check_result\":%d,\"stock_check_ticks\":%d,\"stock_check_stocks\":%d,\"stock_check_respawns\":%d,\"players\":[{\"action\":%d,\"x\":%.9g,\"y\":%.9g,\"percent\":%.9g,\"stocks\":%d,\"shield\":%.9g},{\"action\":%d,\"x\":%.9g,\"y\":%.9g,\"percent\":%.9g,\"stocks\":%d,\"shield\":%.9g}]}",
        (unsigned long long)players[0].ticks,catchup_frames,max_backlog_ticks,finished?"true":"false",winner,
        stock_check_tick>=0?"true":"false",stock_check_result,stock_check_total_ticks,stock_check_stocks,stock_check_respawns,
        players[0].motion_id,players[0].position[0],players[0].position[1],players[0].damage_percent,players[0].stocks,players[0].shield_health,
        players[1].motion_id,players[1].position[0],players[1].position[1],players[1].damage_percent,players[1].stocks,players[1].shield_health);return output;
}
}
int main(int argc,char** argv){
    AuroraConfig config{};config.appName="Melee source runtime";config.desiredBackend=BACKEND_WEBGPU;
    config.cachePath="/melee-render-cache";
    config.windowWidth=640;config.windowHeight=480;config.msaa=1;config.vsync=true;
    config.logCallback=log_message;config.logLevel=LOG_INFO;
    if(!SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT,"#canvas"))return 1;
    runtime_window=aurora_initialize(argc,argv,&config).window;GXInit(fifo_buffer,sizeof(fifo_buffer));
    if(!melee_web_input_startup())return 1;
    ready=true;emscripten_set_main_loop(tick,0,1);return 0;
}
