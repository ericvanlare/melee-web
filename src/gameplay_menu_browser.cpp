#include "gameplay_menu_world.hpp"
#include "gameplay_menu_host.h"
#include "gameplay_match_session.hpp"
#include "gameplay_content.h"
#include "menu_preparation_state.hpp"
#include "browser_input.h"
#include "animation_clock.hpp"
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <aurora/gfx.h>
#include <dolphin/gx.h>
#include <dolphin/vi.h>
#include <emscripten.h>
#include <emscripten/heap.h>
#include <SDL3/SDL_hints.h>
#include <array>
#include <algorithm>
#include <cstdint>
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
std::string match_message="Original four-stock source match";
bool running=false,pending=false,host_entered=false,faulted=false;
melee_web::MenuPreparationState preparation;
unsigned audio_phase=0,diagnostic_start_ticks=0;
int stock_check=0,stock_count=4,stock_respawns=0;
unsigned stock_tick=0,completed_matches=0;
bool stock_lost=false,stock_jump=false;
bool first_use_draw_pending=false;
unsigned render_frame=0;
PADStatus diagnostic_pad{};
unsigned diagnostic_pad_port=0,diagnostic_pad_remaining=0;
std::array<float,1068> pcm;
alignas(32) unsigned char fifo[64*1024];
constexpr std::array<std::string_view,54> keys={"GmPause.usd","IfAll.usd","IfCoGet.dat","SdIntro.dat","PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","ItCo.usd","EfMrData.dat","EfCoData.dat","PdPm.dat","sp_end.hps","PlMrYe.dat","PlMrBk.dat","PlMrBu.dat","PlMrGr.dat","PlFc.dat","PlFcAJ.dat","PlFcNr.dat","PlFcRe.dat","PlFcBu.dat","PlFcGr.dat","EfFxData.dat","falco.ssm","GrNBa.dat","sp_zako.hps","PlFx.dat","PlFxAJ.dat","PlFxNr.dat","PlFxOr.dat","PlFxLa.dat","PlFxGr.dat","fox.ssm","GrSt.dat","ystory.hps","MnSlChr.usd","MnSlMap.usd","SdSlChr.usd","MnExtAll.usd","LbMcGame.usd","NtMemAc.usd","menu01.hps","nr_select.ssm","nr_title.ssm","nr_name.ssm","pokemon.ssm","end.ssm","smash2.sem","main.ssm","mario.ssm","dsp_coef.bin","sislib_font.bin"};
constexpr unsigned kDiagnosticPadButtons=PAD_BUTTON_LEFT|PAD_BUTTON_RIGHT|PAD_BUTTON_DOWN|PAD_BUTTON_UP|
 PAD_TRIGGER_Z|PAD_TRIGGER_R|PAD_TRIGGER_L|PAD_BUTTON_A|PAD_BUTTON_B|PAD_BUTTON_X|PAD_BUTTON_Y|PAD_BUTTON_START;
void check(int value,const char* error){if(!value)throw std::runtime_error(error);}
void clear_diagnostic_pad(){diagnostic_pad={};diagnostic_pad_port=0;diagnostic_pad_remaining=0;}
AuroraStats aurora_stats_snapshot(){
 AuroraStats result{};if(const AuroraStats* stats=aurora_get_stats())result=*stats;return result;
}
int32_t stat_delta(uint64_t after,uint64_t before){
 return static_cast<int32_t>(after)-static_cast<int32_t>(before);
}
void report_construction(const char* kind,double started,double constructed,double entered,
                         const AuroraStats& before,const AuroraStats& after){
 EM_ASM({if(window.menuConstructionTiming)window.menuConstructionTiming({
   kind:UTF8ToString($0),total_ms:$1,construct_ms:$2,entry_ms:$3,
   queued_delta:$4,created_delta:$5,draw_calls:$6,texture_upload_bytes:$7,
   queued_total:$8,created_total:$9,wasm_heap_bytes:$10
 });},kind,entered-started,constructed-started,entered-constructed,
        stat_delta(after.queuedPipelines,before.queuedPipelines),
        stat_delta(after.createdPipelines,before.createdPipelines),
        after.drawCallCount,after.lastTextureUploadSize,after.queuedPipelines,
        after.createdPipelines,emscripten_get_heap_size());
}
void begin_preparation(){
 if(!preparation.request())return;
 clear_diagnostic_pad();pending=false;running=false;menu_clock.reset();
 message=match?"Preparing original character select...":"Preparing original next scene...";
 EM_ASM({if(window.menuPreparation)window.menuPreparation(UTF8ToString($0));},message.c_str());
}
bool audio_ready_for_preparation(){
 return EM_ASM_INT({return window.menuAudioReadyForPreparation?
    (window.menuAudioReadyForPreparation()?1:0):1;})!=0;
}
void preparation_failed(const char* error){
 EM_ASM({window.menuPreparationFailed?.(UTF8ToString($0));},error?error:"Native preparation failed");
}
std::string selected_match_message(const MeleeWebMenuMatchSelection& selection){
 const auto* first=melee_web_fighter_content(selection.start.players[0].ckind);
 const auto* second=melee_web_fighter_content(selection.start.players[1].ckind);
 const auto* stage=melee_web_stage_content(selection.start.rules.stkind);
 std::string result="Original four-stock ";
 if(first&&second){result+=first->name;result+=" vs ";result+=second->name;}
 else result+="source fighters";
 result+=" match on ";result+=stage?stage->name:"source stage";
 return result;
}
void close(){
 const bool had_lifetime=world||match||host;
 const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
 char error[256]{};running=false;pending=false;preparation.reset();menu_clock.reset();
 if(match){match->close();match.reset();}
 if(world){if(host_entered){check(melee_web_menu_host_leave(host,1,error,sizeof(error)),error);host_entered=false;world->close();}else world->close_prepared();world.reset();}
 if(host){check(melee_web_menu_host_destroy(host,error,sizeof(error)),error);host=nullptr;}
 audio_phase=0;faulted=false;diagnostic_start_ticks=0;stock_check=0;stock_tick=0;render_frame=0;first_use_draw_pending=false;clear_diagnostic_pad();
 match_message="Original four-stock source match";
 if(had_lifetime){
  const double finished=emscripten_get_now();
  report_construction("lifecycle-close",started,finished,finished,before,
                      aurora_stats_snapshot());
 }
}
void enter_world(){
 const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
 char error[256]{};const bool prepared=world!=nullptr;
 if(!prepared)world=std::make_unique<melee_web::GameplayMenuWorld>(files);
 const double constructed=emscripten_get_now();
 check(melee_web_menu_host_enter(host,world->audio(),error,sizeof(error)),error);host_entered=true;
 const double entered=emscripten_get_now();
 report_construction("scene-enter",started,constructed,entered,before,aurora_stats_snapshot());
 first_use_draw_pending=true;
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
  match_message=selected_match_message(selection);
  const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
  match=std::make_unique<melee_web::GameplayMatchSession>(files,selection);
  const double constructed=emscripten_get_now();
  report_construction("match-enter",started,constructed,constructed,before,aurora_stats_snapshot());
  first_use_draw_pending=true;
  running=true;message=match_message;return;
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
 const double started=emscripten_get_now();
 const AuroraStats stats_before=aurora_stats_snapshot();
 double input_done=started,simulation_done=started,begin_done=started,draw_done=started,end_done=started;
 double preparation_ms=0,preparation_started=0;
 int began=0,drawn=1,timing_valid=1,first_use=0;
 // A transition request owns the whole callback in which it is observed.
 // Keep the source presenter out of both the request and audio-ack waits.
 int suppress_draw=preparation.suppress_source_draw()||pending;
 bool actual_source_draw=false;
 try{
  for(const AuroraEvent* event=aurora_update();event&&event->type!=AURORA_NONE;++event){
   if(event->type==AURORA_EXIT){close();melee_web_input_shutdown();aurora_shutdown();emscripten_cancel_main_loop();return;}
  }
  const auto* input=melee_web_input_poll();
  input_done=emscripten_get_now();
  if(preparation.waiting_for_audio()){
   if(preparation.begin_construction(audio_ready_for_preparation())){
    preparation_started=emscripten_get_now();advance();
    preparation_ms=emscripten_get_now()-preparation_started;preparation_started=0;
    EM_ASM({window.menuPreparationDone?.();});
    preparation.finish_construction(running);
    if(running)running=false;
   }
  }else if(preparation.arming()){
   preparation.arm();running=true;menu_clock.reset();suppress_draw=0;
  }else if(pending)begin_preparation();
  const auto elapsed=menu_clock.tick(emscripten_get_now(),running&&(world||match)&&input->visible);
  if(elapsed.stalled){running=false;message="Paused after a timing disruption. Resume to continue.";}
  char error[256]{};
  for(unsigned step=0;step<elapsed.steps;step++){
   PADStatus checked_input[4];const PADStatus* sample=input->raw;bool copied_input=false;
   bool diagnostic_start_pulse=false;
   if(diagnostic_start_ticks){
    std::copy(input->raw,input->raw+4,checked_input);checked_input[0].button|=PAD_BUTTON_START;
    sample=checked_input;copied_input=true;diagnostic_start_pulse=true;--diagnostic_start_ticks;
   }
   if(match&&stock_check==-1){
    std::fill(checked_input,checked_input+4,PADStatus{});checked_input[2].err=checked_input[3].err=PAD_ERR_NO_CONTROLLER;
    if(stock_tick>=20&&!stock_lost)checked_input[0].stickX=80;
    if(stock_jump)checked_input[0].button=PAD_BUTTON_X;
    sample=checked_input;copied_input=true;
   }
   if(diagnostic_pad_remaining){
    if(!copied_input)std::copy(input->raw,input->raw+4,checked_input);
    // Keep a queued diagnostic sample scoped to its selected port.  If an
    // older command has already put a Start pulse in flight, preserve that
    // pulse when the selected port is replaced.
    const PADStatus queued_sample=diagnostic_pad;
    checked_input[diagnostic_pad_port]=queued_sample;
    if(diagnostic_start_pulse)checked_input[diagnostic_pad_port].button|=PAD_BUTTON_START;
    sample=checked_input;
    --diagnostic_pad_remaining;
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
   if(result==3){pending=true;clear_diagnostic_pad();break;}
  }
  simulation_done=emscripten_get_now();
  begin_done=simulation_done;
  if(aurora_begin_frame()){
   began=1;begin_done=emscripten_get_now();
   GXSetCopyClear(GXColor{0,0,0,255},GX_MAX_Z24);
   if(!suppress_draw){
    if(!faulted){
     if(match){match->draw();actual_source_draw=true;}
     else if(world&&host_entered){drawn=melee_web_menu_host_draw(host,error,sizeof(error));actual_source_draw=drawn!=0;}
    }
   }
   draw_done=emscripten_get_now();
   aurora_end_frame();end_done=emscripten_get_now();check(drawn,error);
   if(actual_source_draw){
    first_use=first_use_draw_pending?1:0;
    first_use_draw_pending=false;
   }
  }
  else{draw_done=begin_done;end_done=begin_done;}
 }catch(const std::exception& e){running=false;faulted=true;preparation.reset();pending=false;clear_diagnostic_pad();menu_clock.reset();message=e.what();if(preparation_started)preparation_ms=emscripten_get_now()-preparation_started;preparation_failed(e.what());timing_valid=0;std::fprintf(stderr,"Native menu: %s\n",e.what());
  const double failed=emscripten_get_now();
  if(input_done<started)input_done=failed;
  if(simulation_done<input_done)simulation_done=failed;
  if(begin_done<simulation_done)begin_done=simulation_done;
  if(draw_done<begin_done)draw_done=begin_done;
  if(end_done<draw_done)end_done=draw_done;
 }
 const double finished=emscripten_get_now();
 const AuroraStats stats_after=aurora_stats_snapshot();
 char timing[1024];
 std::snprintf(timing,sizeof(timing),
  "{\"frame\":%u,\"started\":%.3f,\"valid\":%d,\"first_use\":%d,"
  "\"input_ms\":%.3f,\"simulation_audio_ms\":%.3f,\"preparation_ms\":%.3f,"
  "\"begin_ms\":%.3f,\"draw_ms\":%.3f,\"end_ms\":%.3f,\"total_ms\":%.3f,"
  "\"began\":%d,\"drawn\":%d,\"queued_delta\":%d,\"created_delta\":%d,"
  "\"queued_total\":%u,\"created_total\":%u,\"draw_calls\":%u,"
  "\"texture_upload_bytes\":%u,\"wasm_heap_bytes\":%zu,\"draw_suppressed\":%d}",
  ++render_frame,started,timing_valid,first_use,input_done-started,
  std::max(0.0,simulation_done-input_done-preparation_ms),preparation_ms,
  begin_done-simulation_done,draw_done-begin_done,end_done-draw_done,
  finished-started,began,drawn,
  stat_delta(stats_after.queuedPipelines,stats_before.queuedPipelines),
  stat_delta(stats_after.createdPipelines,stats_before.createdPipelines),
  stats_after.queuedPipelines,stats_after.createdPipelines,
  stats_after.drawCallCount,stats_after.lastTextureUploadSize,
  emscripten_get_heap_size(),suppress_draw);
 EM_ASM({if(window.menuRuntimeTiming)window.menuRuntimeTiming(JSON.parse(UTF8ToString($0)));},timing);
 EM_ASM({window.menuFrame?.();});
}
}
extern "C" {
int melee_web_native_menu_file(const char* name,const uint8_t* data,unsigned size){try{
 if(world||match||!name||!data||!size||size>64*1024*1024)throw std::runtime_error("Unload before importing valid local files");
 bool known=false;for(auto key:keys)known|=key==name;
 if(!known)throw std::runtime_error("Unknown native menu file: "+std::string(name));
 files[name]={data,data+size};return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_prepare(){try{
 if(match||host_entered)throw std::runtime_error("Unload before preparing native menu resources");
 if(world&&host){message="Native menu resources already prepared.";return 1;}
 if(world||host)close();
 char error[256]{};const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
 // Preserve the source ownership order used by launch: the menu host claims
 // RNG/session ownership before the SDK world is started. No source scene or
 // simulation callback runs during this preparation phase.
 host=melee_web_menu_host_create(error,sizeof(error));check(host!=nullptr,error);
 world=std::make_unique<melee_web::GameplayMenuWorld>(files);
 const double constructed=emscripten_get_now();
 report_construction("scene-prepare",started,constructed,constructed,before,aurora_stats_snapshot());
 message="Native menu resources prepared.";return 1;
}catch(const std::exception& e){
 if(world&&!host_entered){try{world->close_prepared();}catch(...){}}
 world.reset();
 if(host&&!host_entered){char ignored[256]{};melee_web_menu_host_destroy(host,ignored,sizeof(ignored));host=nullptr;}
 message=e.what();return 0;
}}
int melee_web_native_menu_launch(){try{
 if(preparation.busy()||pending)
  throw std::runtime_error("Native menu transition is still preparing");
 if(match||host_entered||(host&&!world))close();
 char error[256]{};if(!host){host=melee_web_menu_host_create(error,sizeof(error));check(host!=nullptr,error);}
 VISetFrameBufferScale(1);enter_world();return 1;
}catch(const std::exception& e){message=e.what();running=false;return 0;}}
int melee_web_native_menu_unload(){try{close();message="Native menus unloaded.";return 1;}catch(const std::exception& e){message=e.what();return 0;}}
void melee_web_native_menu_pause(int paused){
 if(faulted||preparation.busy()||pending||(!host_entered&&!match))return;
 running=(world||match)&&!paused;menu_clock.reset();
 message=running?(match?match_message:melee_web_menu_host_phase(host)==1?"Original character select":"Original stage select"):"Paused.";
}
void melee_web_native_menu_confirm_check(){
 if((host_entered||match)&&!faulted&&!preparation.busy()&&!pending&&stock_check!=-1&&diagnostic_pad_remaining==0)
  diagnostic_start_ticks=3;
}
int melee_web_native_menu_pad_sample(unsigned port,unsigned buttons,int stick_x,int stick_y,unsigned duration){try{
 if(faulted||preparation.busy()||pending||stock_check==-1||diagnostic_start_ticks!=0||!running||(!host_entered&&!match))
  throw std::runtime_error("Raw PAD samples require an active, non-diagnostic scene");
 if(port>1||buttons>0xffffU||(buttons&~kDiagnosticPadButtons)||stick_x<-80||stick_x>80||stick_y<-80||stick_y>80||duration<1||duration>120)
  throw std::runtime_error("Raw PAD sample is outside the supported port, button, axis or duration bounds");
 if(diagnostic_pad_remaining)throw std::runtime_error("A raw PAD sample is already queued");
 diagnostic_pad={};diagnostic_pad.err=PAD_ERR_NONE;diagnostic_pad.button=static_cast<u16>(buttons);
 diagnostic_pad.stickX=static_cast<s8>(stick_x);diagnostic_pad.stickY=static_cast<s8>(stick_y);
 diagnostic_pad_port=port;diagnostic_pad_remaining=duration;message="Raw PAD sample queued at the next source tick.";return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_stock_check_ready(){
 return match&&!faulted&&running&&match->ready()&&!match->paused()&&
        !match->ending()&&stock_check!=-1&&diagnostic_start_ticks==0&&diagnostic_pad_remaining==0;
}
int melee_web_native_menu_stock_check(){
 if(!melee_web_native_menu_stock_check_ready()||
    match->player_stats(0).stocks!=4||match->player_stats(1).stocks!=4)return 0;
 stock_check=-1;stock_count=4;stock_respawns=0;stock_tick=0;stock_lost=stock_jump=false;
 running=true;menu_clock.reset();return 1;
}
const char* melee_web_native_menu_diagnostics(){
 static char text[512];
 std::snprintf(text,sizeof(text),"Completed matches: %u · stock check: %d · ticks: %u · stocks: %d · respawns: %d",
  completed_matches,stock_check,stock_tick,stock_count,stock_respawns);
 const auto length=std::char_traits<char>::length(text);
 if(diagnostic_pad_remaining)
  std::snprintf(text+length,sizeof(text)-length," · raw PAD: port %u buttons 0x%04x stick [%d,%d] remaining %u",
                diagnostic_pad_port,static_cast<unsigned>(diagnostic_pad.button),diagnostic_pad.stickX,diagnostic_pad.stickY,diagnostic_pad_remaining);
 else
  std::snprintf(text+length,sizeof(text)-length," · raw PAD: none");
 if(match){const auto length=std::char_traits<char>::length(text);
  std::snprintf(text+length,sizeof(text)-length," · source pause: %d · ready: %d",match->paused(),match->ready());}
 return text;
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
