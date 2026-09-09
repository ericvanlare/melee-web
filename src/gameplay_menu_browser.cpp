#include "gameplay_menu_world.hpp"
#include "gameplay_menu_host.h"
#include "gameplay_match_session.hpp"
#include "browser_input.h"
#include "animation_clock.hpp"
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <aurora/gfx.h>
#include <dolphin/gx.h>
#include <dolphin/vi.h>
#include <emscripten.h>
#include <SDL3/SDL_hints.h>
#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
namespace {
melee_web::RuntimeFiles files;
std::unique_ptr<melee_web::GameplayMenuWorld> world;
std::unique_ptr<melee_web::GameplayMatchSession> match;
MeleeWebMenuHost* host=nullptr;
melee_web::FixedTickClock menu_clock;
std::string message="Choose your local Melee disc image.";
bool running=false,pending=false,host_entered=false,faulted=false;
unsigned audio_phase=0,diagnostic_start_ticks=0;
int stock_check=0,stock_count=4,stock_respawns=0;
unsigned stock_tick=0,completed_matches=0;
bool stock_lost=false,stock_jump=false;
std::array<float,1068> pcm;
alignas(32) unsigned char fifo[64*1024];
constexpr std::array<std::string_view,34> keys={"IfAll.usd","IfCoGet.dat","SdIntro.dat","PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","ItCo.usd","EfMrData.dat","EfCoData.dat","PdPm.dat","sp_end.hps","PlMrYe.dat","PlMrBk.dat","PlMrBu.dat","PlMrGr.dat","MnSlChr.usd","MnSlMap.usd","SdSlChr.usd","MnExtAll.usd","LbMcGame.usd","NtMemAc.usd","menu01.hps","nr_select.ssm","nr_title.ssm","nr_name.ssm","pokemon.ssm","end.ssm","smash2.sem","main.ssm","mario.ssm","dsp_coef.bin","sislib_font.bin"};
void check(int value,const char* error){if(!value)throw std::runtime_error(error);}
void close(){
 char error[256]{};running=false;pending=false;menu_clock.reset();
 if(match){match->close();match.reset();}
 if(world){if(host_entered){check(melee_web_menu_host_leave(host,1,error,sizeof(error)),error);host_entered=false;}world->close();world.reset();}
 if(host){check(melee_web_menu_host_destroy(host,error,sizeof(error)),error);host=nullptr;}
 audio_phase=0;faulted=false;diagnostic_start_ticks=0;stock_check=0;stock_tick=0;
}
void enter_world(){
 char error[256]{};world=std::make_unique<melee_web::GameplayMenuWorld>(files);
 check(melee_web_menu_host_enter(host,world->audio(),error,sizeof(error)),error);host_entered=true;
 menu_clock.reset();audio_phase=0;running=true;
 message=melee_web_menu_host_phase(host)==1?"Original character select":"Original stage select";
}
void advance(){
 char error[256]{};
 if(match){
  const uint32_t seed=match->random_seed();match->close();match.reset();++completed_matches;
  check(melee_web_menu_host_match_finished(host,seed,error,sizeof(error)),error);
  pending=false;enter_world();return;
 }
 check(melee_web_menu_host_leave(host,0,error,sizeof(error)),error);host_entered=false;
 world->close();world.reset();pending=false;menu_clock.reset();audio_phase=0;
 const int phase=melee_web_menu_host_phase(host);
 if(phase==5){
  MeleeWebMenuMatchSelection selection{};check(melee_web_menu_host_selection(host,&selection,error,sizeof(error)),error);
  match=std::make_unique<melee_web::GameplayMatchSession>(files,selection);
  running=true;message="Original four-stock Mario match on Final Destination";return;
 }
 if(phase==6){running=false;message="Original menu closed.";return;}
 enter_world();
}

void log_message(AuroraLogLevel level,const char* module,const char* text,unsigned length){
 std::fprintf(level>=LOG_ERROR?stderr:stdout,"[%s] %.*s\n",module,int(length),text);
 if(level==LOG_FATAL)std::abort();
}
void tick(){
 EM_ASM({window.menuServiceCommands?.();});
 try{
  for(const AuroraEvent* event=aurora_update();event&&event->type!=AURORA_NONE;++event){
   if(event->type==AURORA_EXIT){close();melee_web_input_shutdown();aurora_shutdown();emscripten_cancel_main_loop();return;}
  }
  const auto* input=melee_web_input_poll();
  if(pending)advance();
  const auto elapsed=menu_clock.tick(emscripten_get_now(),running&&(world||match)&&input->visible);
  if(elapsed.stalled){running=false;message="Paused after a timing disruption. Resume to continue.";}
  char error[256]{};
  for(unsigned step=0;step<elapsed.steps;step++){
   PADStatus checked_input[4];const PADStatus* sample=input->raw;
   if(diagnostic_start_ticks){
    std::copy(input->raw,input->raw+4,checked_input);checked_input[0].button|=PAD_BUTTON_START;
    sample=checked_input;--diagnostic_start_ticks;
   }
   if(match&&stock_check==-1){
    std::fill(checked_input,checked_input+4,PADStatus{});checked_input[2].err=checked_input[3].err=PAD_ERR_NO_CONTROLLER;
    if(stock_tick>=20&&!stock_lost)checked_input[0].stickX=80;
    if(stock_jump)checked_input[0].button=PAD_BUTTON_X;
    sample=checked_input;
   }
   int result=1;
   if(match){
    match->tick(sample);int winner=-1;const int outcome=match->outcome(winner);
    if(stock_check==-1){
     const auto player=match->player_stats(0);
     check(match->player_stats(1).stocks==4,"Stock diagnostic: stationary opponent lost a stock");
     stock_jump=!stock_lost&&stock_count<4&&player.ground_or_air==0&&player.position[0]>65;
     if(player.stocks<stock_count){stock_lost=true;stock_count=player.stocks;}
     if(stock_lost&&player.motion_id==14&&player.ground_or_air==0){stock_lost=false;++stock_respawns;}
     ++stock_tick;
     if(outcome){check(winner==1&&stock_count==0&&stock_respawns==3,"Stock diagnostic: unexpected source outcome");if(match->complete())stock_check=1;}
     if(!match->complete())check(stock_tick<4000,"Stock diagnostic: no source exit after 4000 ticks");
    }
    if(match->complete()){check(outcome,"Original match transitioned without an outcome");pending=true;result=3;}
   }
   else{result=melee_web_menu_host_tick(host,sample,error,sizeof(error));check(result==1||result==3,error);}
   audio_phase+=32000;const unsigned count=audio_phase/60;audio_phase%=60;
   check(melee_web_audio_render(match?match->audio():world->audio(),pcm.data(),count,error,sizeof(error)),error);
   EM_ASM({window.menuAudio?.(HEAPF32.slice($0>>2,($0>>2)+$1*2));},pcm.data(),count);
   if(result==3){pending=true;break;}
  }
  if(aurora_begin_frame()){
   GXSetCopyClear(GXColor{0,0,0,255},GX_MAX_Z24);
   int drawn=1;
   if(!faulted){if(match)match->draw();else if(world&&host_entered)drawn=melee_web_menu_host_draw(host,error,sizeof(error));}
   aurora_end_frame();check(drawn,error);
  }
 }catch(const std::exception& e){running=false;faulted=true;menu_clock.reset();message=e.what();std::fprintf(stderr,"Native menu: %s\n",e.what());}
 EM_ASM({window.menuFrame?.();});
}
}
extern "C" {
int melee_web_native_menu_file(const char* name,const uint8_t* data,unsigned size){try{
 if(world||match||!name||!data||!size||size>64*1024*1024)throw std::runtime_error("Unload before importing valid local files");
 bool known=false;for(auto key:keys)known|=key==name;if(!known)throw std::runtime_error("Unknown native menu file");
 files[name]={data,data+size};return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_launch(){try{
 close();char error[256]{};host=melee_web_menu_host_create(error,sizeof(error));check(host!=nullptr,error);
 VISetFrameBufferScale(1);enter_world();return 1;
}catch(const std::exception& e){message=e.what();running=false;return 0;}}
int melee_web_native_menu_unload(){try{close();message="Native menus unloaded.";return 1;}catch(const std::exception& e){message=e.what();return 0;}}
void melee_web_native_menu_pause(int paused){
 if(faulted||(!host_entered&&!match))return;
 running=(world||match)&&!paused;menu_clock.reset();
 message=running?(match?"Original four-stock Mario match on Final Destination":melee_web_menu_host_phase(host)==1?"Original character select":"Original stage select"):"Paused.";
}
void melee_web_native_menu_confirm_check(){if(host_entered&&!faulted)diagnostic_start_ticks=3;}
int melee_web_native_menu_stock_check(){
 if(!match||faulted||match->player_stats(0).stocks!=4||match->player_stats(1).stocks!=4)return 0;
 stock_check=-1;stock_count=4;stock_respawns=0;stock_tick=0;stock_lost=stock_jump=false;
 running=true;menu_clock.reset();return 1;
}
const char* melee_web_native_menu_diagnostics(){
 static char text[512];
 std::snprintf(text,sizeof(text),"Completed matches: %u · stock check: %d · ticks: %u · stocks: %d · respawns: %d",
  completed_matches,stock_check,stock_tick,stock_count,stock_respawns);return text;
}
const char* melee_web_native_menu_message(){return message.c_str();}
int melee_web_native_menu_running(){return running;}
int melee_web_native_menu_cache_idle(){
 const AuroraStats* stats=aurora_get_stats();
 return !world&&!match&&stats&&stats->queuedPipelines==0;
}
int melee_web_native_menu_phase(){return match?7:host?melee_web_menu_host_phase(host):0;}
}
int main(int argc,char** argv){
 AuroraConfig config{};config.appName="Melee native menus";config.desiredBackend=BACKEND_WEBGPU;
 config.cachePath="/melee-render-cache";
 config.windowWidth=640;config.windowHeight=480;config.msaa=1;config.vsync=true;
 config.logCallback=log_message;config.logLevel=LOG_INFO;
 if(!SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT,"#canvas"))return 1;
 aurora_initialize(argc,argv,&config);GXInit(fifo,sizeof(fifo));
 if(!melee_web_input_startup())return 1;
 emscripten_set_main_loop(tick,0,1);return 0;
}
