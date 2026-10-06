// Diagnostics and raw PAD exports: Start confirmation, queued PAD samples and
// the human-readable diagnostic line.
#include "gameplay_menu_browser_state.hpp"
using namespace melee_web_menu_browser;
namespace {
constexpr unsigned kDiagnosticPadButtons=PAD_BUTTON_LEFT|PAD_BUTTON_RIGHT|PAD_BUTTON_DOWN|PAD_BUTTON_UP|
 PAD_TRIGGER_Z|PAD_TRIGGER_R|PAD_TRIGGER_L|PAD_BUTTON_A|PAD_BUTTON_B|PAD_BUTTON_X|PAD_BUTTON_Y|PAD_BUTTON_START;
}
extern "C" {
void melee_web_native_menu_confirm_check(){
 if(!replay&&(host_entered||match)&&!faulted&&!preparation.busy()&&!pending&&stock_check!=-1&&diagnostic_pad_remaining==0)
  diagnostic_start_ticks=3;
}
int melee_web_native_menu_pad_sample_full(unsigned port,unsigned buttons,int stick_x,int stick_y,
                                          int cstick_x,int cstick_y,unsigned trigger_l,
                                          unsigned trigger_r,unsigned duration){try{
 if(replay||faulted||preparation.busy()||pending||stock_check==-1||diagnostic_start_ticks!=0||!running||(!host_entered&&!match&&!results&&!prize))
  throw std::runtime_error("Raw PAD samples require an active, non-diagnostic scene");
 if(port>1||buttons>0xffffU||(buttons&~kDiagnosticPadButtons)||stick_x<-80||stick_x>80||stick_y<-80||stick_y>80||
    cstick_x<-80||cstick_x>80||cstick_y<-80||cstick_y>80||trigger_l>255||trigger_r>255||duration<1||duration>120)
  throw std::runtime_error("Raw PAD sample is outside the supported port, button, axis or duration bounds");
 if(diagnostic_pad_remaining)throw std::runtime_error("A raw PAD sample is already queued");
 diagnostic_pad={};diagnostic_pad.err=PAD_ERR_NONE;diagnostic_pad.button=static_cast<u16>(buttons);
 diagnostic_pad.stickX=static_cast<s8>(stick_x);diagnostic_pad.stickY=static_cast<s8>(stick_y);
 diagnostic_pad.substickX=static_cast<s8>(cstick_x);diagnostic_pad.substickY=static_cast<s8>(cstick_y);
 diagnostic_pad.triggerLeft=static_cast<u8>(trigger_l);diagnostic_pad.triggerRight=static_cast<u8>(trigger_r);
 diagnostic_pad_port=port;diagnostic_pad_remaining=duration;message="Raw PAD sample queued at the next source tick.";return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_pad_sample(unsigned port,unsigned buttons,int stick_x,int stick_y,unsigned duration){
 return melee_web_native_menu_pad_sample_full(port,buttons,stick_x,stick_y,0,0,0,0,duration);
}
const char* melee_web_native_menu_diagnostics(){
 static char text[640];
 std::snprintf(text,sizeof(text),"Completed matches: %u · stock check: %d · ticks: %u · stocks: %d · respawns: %d",
  completed_matches,stock_check,stock_tick,stock_count,stock_respawns);
 if(!match_observer_error.empty()){
  const auto length=std::char_traits<char>::length(text);
  std::snprintf(text+length,sizeof(text)-length," · match observer error: %.320s",match_observer_error.c_str());
  return text;
 }
 const auto length=std::char_traits<char>::length(text);
 if(diagnostic_pad_remaining)
  std::snprintf(text+length,sizeof(text)-length," · raw PAD: port %u buttons 0x%04x stick [%d,%d] remaining %u",
                diagnostic_pad_port,static_cast<unsigned>(diagnostic_pad.button),diagnostic_pad.stickX,diagnostic_pad.stickY,diagnostic_pad_remaining);
 else
  std::snprintf(text+length,sizeof(text)-length," · raw PAD: none");
 // Ready/Go is already source gameplay, even before the HUD allows controls.
 if(match&&match->construction_complete()){
  const auto player=match->player_stats(0);
  const auto length=std::char_traits<char>::length(text);
  std::snprintf(text+length,sizeof(text)-length,
   " · source frame: %u · P1 motion: %d · anim: %.3f · ground/air: %d · position: [%.3f,%.3f] · source pause: %d · ready: %d",
   match->source_frames(),player.motion_id,player.animation_frame,player.ground_or_air,
   player.position[0],player.position[1],match->paused(),match->ready());
 }
 else if(match){
  const auto length=std::char_traits<char>::length(text);
  std::snprintf(text+length,sizeof(text)-length," · match preparing · ready: 0");
 }
 else if(results){
  const auto length=std::char_traits<char>::length(text);
  std::snprintf(text+length,sizeof(text)-length," · Results source frame: %u",results->source_frames());
 }
 else if(prize){
  const auto length=std::char_traits<char>::length(text);
  std::snprintf(text+length,sizeof(text)-length," · Prize source frame: %u",prize->source_frames());
 }
 else if(world){
  uint32_t completed=0,revisited=0;
  if(melee_web_audio_stream_progress(world->audio(),&completed,&revisited)){
   const auto length=std::char_traits<char>::length(text);
   std::snprintf(text+length,sizeof(text)-length," · menu audio blocks: %u · revisits: %u",
                 completed,revisited);
  }
 }
 return text;
}
}
