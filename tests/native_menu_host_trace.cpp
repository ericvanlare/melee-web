#include "gameplay_menu_world.hpp"
#include "gameplay_menu_host.h"
#include "gameplay_match_session.hpp"
#include "gameplay_match_rules.h"
#include "gameplay_audio_stream.h"
#include "native_menu_fighter_input.h"
#include "native_menu_stage_input.h"
#include <melee/ft/forward.h>
#include <melee/gm/forward.h>
#include <melee/gr/forward.h>
#include <sysdolphin/baselib/random.h>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
static void check(int value,const char* error){if(!value){std::cerr<<"Check failed before teardown: "<<error<<"\n";throw std::runtime_error(error);}}

namespace {
std::string hex32(uint32_t value){std::ostringstream out;out<<std::hex<<std::setfill('0')<<std::setw(8)<<value;return out.str();}
std::string hex64(uint64_t value){std::ostringstream out;out<<std::hex<<std::setfill('0')<<std::setw(16)<<value;return out.str();}
std::string stream_name(MeleeWebAudio* audio){
 const char* path=melee_web_audio_stream_path(audio);if(!path)return {};
 std::string result(path);const auto slash=result.find_last_of("/\\");return slash==std::string::npos?result:result.substr(slash+1);
}
void write_player(std::ostream& out,const PlayerInitData& player){
 const unsigned flags_c=(unsigned(player.rumble_enabled)<<7)|(unsigned(player.xC_b1)<<6)|
  (unsigned(player.xC_b2)<<5)|(unsigned(player.xC_b3)<<4)|(unsigned(player.vs_invisible)<<3)|
  (unsigned(player.xC_b5)<<2)|(unsigned(player.xC_b6)<<1)|unsigned(player.xC_b7);
 const unsigned flags_d=(unsigned(player.xD_b0)<<7)|(unsigned(player.xD_b1)<<6)|
  (unsigned(player.xD_b2)<<5)|(unsigned(player.xD_b3)<<4)|(unsigned(player.xD_b4)<<3)|
  (unsigned(player.xD_b5)<<2)|(unsigned(player.xD_b6)<<1)|unsigned(player.xD_b7);
 out<<"{\"ckind\":"<<int(player.ckind)<<",\"slot_type\":"<<unsigned(player.slot_type)
    <<",\"stocks\":"<<int(player.stocks)<<",\"color\":"<<unsigned(player.color)
    <<",\"slot\":"<<unsigned(player.slot)<<",\"spawn\":"<<int(player.x5)
    <<",\"spawn_direction\":"<<int(player.spawn_dir)<<",\"sub_color\":"<<unsigned(player.sub_color)
    <<",\"handicap\":"<<int(player.handicap)<<",\"team\":"<<unsigned(player.team)
    <<",\"nametag\":"<<unsigned(player.nametag)<<",\"flags_c\":"<<flags_c
    <<",\"flags_d\":"<<flags_d<<",\"cpu_kind\":"<<unsigned(player.cpu_kind)
    <<",\"cpu_level\":"<<unsigned(player.cpu_level)<<",\"damage_10\":"<<player.x10
    <<",\"damage_12\":"<<player.x12<<",\"hp\":"<<player.hp
    <<",\"attack_ratio_bits\":\""<<hex32(std::bit_cast<uint32_t>(player.attack_ratio))
    <<"\",\"defense_ratio_bits\":\""<<hex32(std::bit_cast<uint32_t>(player.defense_ratio))
    <<"\",\"model_scale_bits\":\""<<hex32(std::bit_cast<uint32_t>(player.model_scale))<<"\"}";
}
void write_selection(std::ostream& out,const MeleeWebMenuMatchSelection& selection){
 const auto& rules=selection.start.rules;
 out<<"{\"rules\":{\"match_kind\":"<<unsigned(rules.match_kind)
    <<",\"hud_layout\":"<<unsigned(rules.x0_3)
    <<",\"timer_enabled\":"<<(rules.timer_enabled?"true":"false")
    <<",\"timer_counts_up\":"<<(rules.timer_counts_up?"true":"false")
    <<",\"friendly_fire\":"<<(rules.friendly_fire?"true":"false")
    <<",\"is_stock\":"<<(rules.is_stock?"true":"false")
    <<",\"single_button\":"<<(rules.single_button?"true":"false")
    <<",\"disable_pausing\":"<<(rules.disable_pausing?"true":"false")
    <<",\"is_vs\":"<<(rules.is_vs?"true":"false")
    <<",\"is_teams\":"<<unsigned(rules.is_teams)<<",\"item_frequency\":"<<int(rules.xB)
    <<",\"stage_kind\":"<<rules.stkind<<",\"time_limit\":"<<rules.time_limit
    <<",\"item_mask\":\""<<hex64(rules.x20)<<"\",\"damage_ratio_bits\":\""
    <<hex32(std::bit_cast<uint32_t>(rules.x30))<<"\",\"game_speed_bits\":\""
    <<hex32(std::bit_cast<uint32_t>(rules.game_speed))<<"\"},\"players\":[";
 for(unsigned i=0;i<4;++i){if(i)out<<',';write_player(out,selection.start.players[i]);}
 out<<"]}";
}
class TransitionTrace {
 std::ofstream output;unsigned run_=0,index_=0;std::map<uint64_t,unsigned> epochs;
 unsigned epoch(MeleeWebAudio* audio){
  const uint64_t generation=melee_web_audio_generation(audio);auto found=epochs.find(generation);
  if(found!=epochs.end())return found->second;const unsigned result=epochs.size();epochs[generation]=result;return result;
 }
public:
 explicit TransitionTrace(const char* path,const char* source_revision,
                          const char* input_recipe){
  if(!path)return;const std::string revision=source_revision?source_revision:"";
  if(revision.size()!=40||revision.find_first_not_of("0123456789abcdef")!=std::string::npos)
   throw std::runtime_error("Transition trace requires a full lowercase source revision");
  output.open(path,std::ios::trunc);if(!output)throw std::runtime_error("Cannot open transition trace output");
  output<<"{\"record\":\"header\",\"schema\":\"melee-web-transition-trace\",\"version\":1,"
          "\"producer\":\"port\",\"game_revision\":\"GALE01r2\",\"source_revision\":\""
        <<revision<<"\",\"build_configuration\":\"browser-release\"";
  if(input_recipe)output<<",\"input_recipe\":\""<<input_recipe<<"\"";
  output<<"}\n";
 }
 void begin_run(unsigned run){run_=run;index_=0;epochs.clear();}
 void event(const char* name,MeleeWebAudio* audio,const char* route=nullptr,
            const MeleeWebMenuMatchSelection* selection=nullptr,const uint32_t* rng=nullptr){
  if(!output)return;const auto stream=stream_name(audio);
  output<<"{\"record\":\"event\",\"run\":"<<run_<<",\"index\":"<<index_++
        <<",\"event\":\""<<name<<"\",\"audio\":{\"active\":"
        <<(!stream.empty()?"true":"false")<<",\"owner_epoch\":"<<epoch(audio)
        <<",\"stream\":\""<<stream<<"\"}";
  if(route)output<<",\"route\":\""<<route<<"\"";
  const uint32_t* event_rng=rng?rng:seed_ptr;
  if(event_rng)output<<",\"rng\":"<<*event_rng;
  if(selection){
   output<<",\"selection\":";
   write_selection(output,*selection);
  }
  output<<"}\n";output.flush();
 }
};
}
int main(int argc,char** argv){try{
 if(argc<3||argc>7)throw std::runtime_error("Expected menu/audio directories, optional stage kind, transition trace path, source revision and input recipe");
 const int stage_kind=argc>=4?std::stoi(argv[3]):St_Kind_Last;
 const char* trace_path=argc>=5?argv[4]:nullptr;
 const char* source_revision=argc>=6?argv[5]:nullptr;
 const char* input_recipe=argc==7?argv[6]:nullptr;
 const bool retail_fd_recipe=input_recipe&&std::string(input_recipe)=="retail-stock-fd-v1";
 if(input_recipe&&!retail_fd_recipe)throw std::runtime_error("Unknown transition input recipe");
 if(retail_fd_recipe&&stage_kind!=St_Kind_Last)throw std::runtime_error("Retail FD recipe requires Final Destination");
 TransitionTrace trace(trace_path,source_revision,input_recipe);
 melee_web::RuntimeFiles files;
 for(const char* key:{"LbBf.dat","GmPause.usd","IfAll.usd","IfCoGet.dat","SdIntro.dat","PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat","PlFc.dat","PlFcAJ.dat","PlFcNr.dat","PlFcRe.dat","PlFcBu.dat","PlFcGr.dat","PlFx.dat","PlFxAJ.dat","PlFxNr.dat","PlFxOr.dat","PlFxLa.dat","PlFxGr.dat","GrNLa.dat","GrNBa.dat","GrSt.dat","hyaku.hps","hyaku2.hps","sp_zako.hps","ystory.hps","ItCo.usd","EfMrData.dat","EfFxData.dat","EfCoData.dat","PdPm.dat","LbRb.dat","sp_end.hps","PlMrYe.dat","PlMrBk.dat","PlMrBu.dat","PlMrGr.dat","MnSlChr.usd","MnSlMap.usd","SdSlChr.usd","MnExtAll.usd","LbMcGame.usd","NtMemAc.usd","menu01.hps","nr_select.ssm","nr_title.ssm","nr_name.ssm","pokemon.ssm","end.ssm","smash2.sem","main.ssm","mario.ssm","fox.ssm","falco.ssm","mars.ssm","drmario.ssm","emblem.ssm","pupupu.ssm","dsp_coef.bin","sislib_font.bin"}){
  const auto root=std::filesystem::exists(std::filesystem::path(argv[1])/key)?argv[1]:argv[2];
  std::ifstream input(std::filesystem::path(root)/key,std::ios::binary);if(!input)throw std::runtime_error("Missing owned menu host fixture");
  files[key]={(std::istreambuf_iterator<char>(input)),{}};
 }
 for(unsigned cycle=0;cycle<2;cycle++){
  trace.begin_run(cycle);
  if(retail_fd_recipe&&cycle==0)*seed_ptr=1840631306u;
  char error[256]{};auto* host=melee_web_menu_host_create(error,sizeof(error));check(host!=nullptr,error);
  auto world=std::make_unique<melee_web::GameplayMenuWorld>(files);
  check(melee_web_menu_host_enter(host,world->audio(),error,sizeof(error)),error);
  trace.event("capture_begin",world->audio());
  PADStatus raw[4]{};raw[2].err=raw[3].err=-1;float pcm[1068];unsigned audio_phase=0;
  auto tick=[&](){
   int result=melee_web_menu_host_tick(host,raw,error,sizeof(error));
   check(result==1||result==3,error);
   audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
   check(melee_web_audio_render(world->audio(),pcm,count,error,sizeof(error)),error);
   return result;
  };
  auto transition=[&](u16 button=PAD_BUTTON_START){
   raw[0].button=button;
   int result=tick();raw[0].button=0;
   for(unsigned wait=0;result!=3&&wait<120;wait++)result=tick();
   check(result==3,"Original menu input did not complete its transition");
   check(melee_web_menu_host_leave(host,0,error,sizeof(error)),error);
   world->verify_immutable_archives();
  };
  auto rebuild_menu_scene=[&](){
   MeleeWebAudio* retained=world->audio();
   const uint64_t generation=melee_web_audio_generation(retained);
   uint32_t completed=0,revisited=0,after_completed=0,after_revisited=0;
   check(generation!=0,"Original menu audio lifetime is unavailable");
   check(melee_web_audio_stream_progress(retained,&completed,&revisited),
         "Original menu HPS progress is unavailable before scene rebuild");
   const auto scene=melee_web_menu_host_phase(host)==2?
       melee_web::GameplayMenuScene::Stages:melee_web::GameplayMenuScene::Characters;
   world->rebuild_scene(scene);
   check(world->audio()==retained&&melee_web_audio_generation(world->audio())==generation,
         "CSS/SSS scene rebuild replaced the original menu audio lifetime");
   check(melee_web_audio_stream_progress(world->audio(),&after_completed,&after_revisited)&&
         after_completed==completed&&after_revisited==revisited,
         "CSS/SSS scene rebuild reset or advanced menu music outside an audio tick");
   check(melee_web_menu_host_enter(host,world->audio(),error,sizeof(error)),error);
  };
  const unsigned first_css_neutral=retail_fd_recipe&&cycle==0?187:120;
  for(unsigned t=0;t<first_css_neutral;t++)check(tick()==1,"Unexpected CSS transition");
  uint32_t initial_music_completed=0,initial_music_revisited=0;
  check(melee_web_audio_stream_progress(world->audio(),&initial_music_completed,
                                         &initial_music_revisited)&&
        initial_music_completed>0,
        "Original CSS music did not load an HPS payload");
  if(cycle==1){
   bool falco_selected=false;
   for(unsigned t=0;t<180;t++){
    MeleeWebFighterInputObservation observed{};
    check(melee_web_fighter_input_observe(CKIND_FALCO,&observed),
          "Original CSS fighter observation unavailable");
    const int state=melee_web_fighter_input_drive(raw,&observed,CKIND_FALCO);
    check(state!=MELEE_WEB_FIGHTER_INPUT_INVALID,
          "Original CSS Falco target is invalid");
    if(state==MELEE_WEB_FIGHTER_INPUT_ALREADY_SELECTED){
     falco_selected=true;break;
    }
    if(state==MELEE_WEB_FIGHTER_INPUT_PICKUP_READY||
       state==MELEE_WEB_FIGHTER_INPUT_TARGET_READY)
      melee_web_fighter_input_button(raw,PAD_BUTTON_A);
    check(tick()==1,"CSS cursor input unexpectedly transitioned");
    melee_web_fighter_input_neutral(raw);
    check(tick()==1,"CSS button release unexpectedly transitioned");
   }
   check(falco_selected,"Original CSS did not commit Falco through raw PAD input");
   // The source keeps the door/model confirmation animation active briefly
   // after the drop.  Give that original process time to reach its ordinary
   // Start-accepting state before requesting the scene transition.
   melee_web_fighter_input_neutral(raw);
   for(unsigned settle=0;settle<30;++settle)
    check(tick()==1,"CSS transitioned during Falco confirmation settle");
  }
  transition();check(melee_web_menu_host_phase(host)==2,"CSS did not choose original SSS");
  trace.event("css_exit_complete",world->audio());
  rebuild_menu_scene();
  trace.event("sss_enter_complete",world->audio());
  for(unsigned t=0;t<120;t++)check(tick()==1,"Unexpected SSS transition");
  // Exercise the real B cancellation before committing the match, with new
  // owned worlds for both directions and no direct source selection writes.
  transition(PAD_BUTTON_B);check(melee_web_menu_host_phase(host)==4,"Original SSS B did not return toward CSS");
  trace.event("sss_exit_complete",world->audio(),"css");
  rebuild_menu_scene();
  trace.event("css_enter_complete",world->audio());
  const unsigned second_css_neutral=retail_fd_recipe&&cycle==0?138:120;
  for(unsigned t=0;t<second_css_neutral;t++)check(tick()==1,"Unexpected cancelled CSS transition");
  transition();check(melee_web_menu_host_phase(host)==2,"Returned CSS did not choose SSS");
  trace.event("css_exit_complete",world->audio());
  rebuild_menu_scene();
  trace.event("sss_enter_complete",world->audio());
  for(unsigned t=0;t<120;t++)check(tick()==1,"Unexpected second SSS transition");
  // Move the original SSS cursor with raw PAD input. Random is deliberately
  // not used: adding an available stage must not change the FD regression.
  bool at_target=false;
  for(unsigned t=0;t<120;t++){
   MeleeWebStageInputObservation observed{};
   check(melee_web_stage_input_observe(stage_kind,&observed),"Original SSS cursor observation unavailable");
   const int state=melee_web_stage_input_drive(raw,&observed,stage_kind);
   check(state!=MELEE_WEB_STAGE_INPUT_INVALID,"Original SSS target is invalid");
   if(state==MELEE_WEB_STAGE_INPUT_AT_TARGET){
    check(observed.selected_stage_kind==stage_kind,"Cursor target and source selected tile differ");
    at_target=true;break;
   }
   check(tick()==1,"SSS cursor input unexpectedly transitioned");
  }
  check(at_target,"Original SSS cursor did not reach requested stage");
  uint32_t continued_music_completed=0,continued_music_revisited=0;
  check(melee_web_audio_stream_progress(world->audio(),&continued_music_completed,
                                         &continued_music_revisited)&&
        continued_music_completed>initial_music_completed,
        "Original menu music did not continue loading across CSS/SSS scenes");
  transition();check(melee_web_menu_host_phase(host)==5,"SSS did not complete original selection");
  StartMeleeData raw_start{};
  check(melee_web_menu_host_raw_selection(host,&raw_start,error,sizeof(error)),error);
  MeleeWebMenuMatchSelection selection{};check(melee_web_menu_host_selection(host,&selection,error,sizeof(error)),error);
  check(selection.start.rules.stkind==stage_kind,"Source SSS committed another stage");
  check(selection.start.players[0].ckind==(cycle==1?CKIND_FALCO:CKIND_MARIO),
        "Source CSS committed another P1 character");
  const uint32_t selection_rng=selection.random_seed;
  MeleeWebMenuMatchSelection raw_selection{};raw_selection.start=raw_start;
  raw_selection.random_seed=selection_rng;
  trace.event("sss_exit_complete",world->audio(),"match",&raw_selection,&selection_rng);
  world->close();world.reset();audio_phase=0;
  bool match_entry_recorded=false;
  if(cycle==0)for(unsigned stop:{0u,60u,100u}){
   // Unload both before and after Ready's stage-start callback, then rebuild
   // the full SDK world from the same immutable native selection.
   melee_web::GameplayMatchSession interrupted(files,selection);unsigned phase=0;
   if(!match_entry_recorded){const uint32_t rng=interrupted.random_seed();trace.event("match_enter_complete",interrupted.audio(),nullptr,&selection,&rng);match_entry_recorded=true;}
   for(unsigned t=0;t<stop;++t){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;interrupted.tick(pads);
    phase+=32000;unsigned count=phase/60;phase%=60;
    check(melee_web_audio_render(interrupted.audio(),pcm,count,error,sizeof(error)),error);
   }
   interrupted.close();interrupted.close();
  }
  if(cycle==0){
   // Exercise the source-owned pause/no-contest path from the same committed
   // menu payload before the ordinary stock run.  Every source tick still
   // drains the resident audio stream.
   melee_web::GameplayMatchSession no_contest(files,selection);unsigned phase=0;
   for(unsigned t=0;t<600&&!no_contest.ready();++t){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;no_contest.tick(pads);
    phase+=32000;unsigned count=phase/60;phase%=60;
    check(melee_web_audio_render(no_contest.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(no_contest.ready(),"Original Ready/Go did not complete for No Contest test");
   PADStatus pause[4]{};pause[2].err=pause[3].err=-1;pause[0].button=PAD_BUTTON_START;
   no_contest.tick(pause);phase+=32000;unsigned count=phase/60;phase%=60;
   check(melee_web_audio_render(no_contest.audio(),pcm,count,error,sizeof(error)),error);
   pause[0].button=0;
   for(unsigned t=0;t<20&&!no_contest.paused();++t){
    no_contest.tick(pause);phase+=32000;count=phase/60;phase%=60;
    check(melee_web_audio_render(no_contest.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(no_contest.paused(),"Original P1 Start did not pause the match");
   for(unsigned t=0;t<12;t++){
    no_contest.tick(pause);phase+=32000;count=phase/60;phase%=60;
    check(melee_web_audio_render(no_contest.audio(),pcm,count,error,sizeof(error)),error);
   }
   pause[0].button=PAD_TRIGGER_L|PAD_TRIGGER_R|PAD_BUTTON_A|PAD_BUTTON_START;
   no_contest.tick(pause);phase+=32000;count=phase/60;phase%=60;
   check(melee_web_audio_render(no_contest.audio(),pcm,count,error,sizeof(error)),error);
   pause[0].button=0;
   for(unsigned t=0;t<500&&!no_contest.complete();++t){
    no_contest.tick(pause);phase+=32000;count=phase/60;phase%=60;
    check(melee_web_audio_render(no_contest.audio(),pcm,count,error,sizeof(error)),error);
   }
   int no_contest_winner=-1;
   check(no_contest.complete(),"Original No Contest did not complete its source ending");
   check(no_contest.outcome(no_contest_winner)==OUTCOME_NO_CONTEST&&no_contest_winner==-1,
         "Original No Contest outcome or winner was incorrect");
   no_contest.close();
  }
  {
   melee_web::GameplayMatchSession match(files,selection);audio_phase=0;
   if(!match_entry_recorded){const uint32_t rng=match.random_seed();trace.event("match_enter_complete",match.audio(),nullptr,&selection,&rng);match_entry_recorded=true;}
   check(!match.ready(),"Original match intro was bypassed");
   const auto entry_stats=match.player_stats(0);
   for(unsigned eye=0;eye<2;eye++)check(entry_stats.eyes[eye].image_is_base&&
      entry_stats.eyes[eye].image_index==UINT32_MAX&&entry_stats.eyes[eye].palette_is_base&&
      entry_stats.eyes[eye].palette_index==UINT32_MAX,
      "Original Entry eye telemetry did not retain owned base image/palette");
   const float ready_start_x=match.player_stats(0).position[0];
   unsigned intro_ticks=0;
   for(;intro_ticks<600&&!match.ready();++intro_ticks){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
    if(intro_ticks<60)pads[0].stickX=80;
    match.tick(pads);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
    if(intro_ticks<60)check(match.player_stats(0).position[0]==ready_start_x,
                           "Fighter accepted movement before original Ready completion");
   }
   check(match.ready(),"Original Ready/Go did not reach gameplay");
   check(!match.paused(),"Original match entered gameplay already paused");
   const auto active_frame=match.source_frames();
   for(unsigned t=0;t<3;t++){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;match.tick(pads);
    audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(match.source_frames()>active_frame,"Original active source frame did not advance");
   const auto wait_stats=match.player_stats(0);
   for(unsigned eye=0;eye<2;eye++){
    check(wait_stats.eyes[eye].image_count>0,
          "Original Wait eye telemetry lost its owned animation bounds");
    check((wait_stats.eyes[eye].image_is_base&&wait_stats.eyes[eye].image_index==UINT32_MAX)||
          (!wait_stats.eyes[eye].image_is_base&&wait_stats.eyes[eye].image_index<wait_stats.eyes[eye].image_count),
          "Original Wait eye image state is outside its owned base/table representation");
    check((wait_stats.eyes[eye].palette_is_base&&wait_stats.eyes[eye].palette_index==UINT32_MAX)||
          (!wait_stats.eyes[eye].palette_is_base&&wait_stats.eyes[eye].palette_index<wait_stats.eyes[eye].palette_count),
          "Original Wait eye palette state is outside its owned base/table representation");
   }

   // Start pauses through the original pauser path.  The source scheduler and
   // audio continue to tick, but fighter actions/animation/positions and the
   // match frame must remain held while the pause is debounced.
   PADStatus pause[4]{};pause[2].err=pause[3].err=-1;pause[0].button=PAD_BUTTON_START;
   match.tick(pause);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
   check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   pause[0].button=0;
   for(unsigned t=0;t<20&&!match.paused();++t){
    match.tick(pause);audio_phase+=32000;count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(match.paused(),"Original P1 Start did not enter pause");
   const auto paused_frame=match.source_frames();
   const auto paused_player0=match.player_stats(0);
   const auto paused_player1=match.player_stats(1);
   for(unsigned t=0;t<20;t++){
    pause[1].button=t==15?PAD_BUTTON_START:0;
    match.tick(pause);audio_phase+=32000;count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
    pause[1].button=0;
    const auto held0=match.player_stats(0);
    const auto held1=match.player_stats(1);
    check(match.paused()&&match.source_frames()==paused_frame&&
          held0.motion_id==paused_player0.motion_id&&
          held0.animation_frame==paused_player0.animation_frame&&
          held0.position[0]==paused_player0.position[0]&&
          held0.position[1]==paused_player0.position[1]&&held0.stocks==paused_player0.stocks&&
          held1.motion_id==paused_player1.motion_id&&
          held1.animation_frame==paused_player1.animation_frame&&
          held1.position[0]==paused_player1.position[0]&&
          held1.position[1]==paused_player1.position[1]&&held1.stocks==paused_player1.stocks,
          "Wrong-port Start or paused source tick changed the match");
   }
   pause[0].button=PAD_BUTTON_START;
   match.tick(pause);audio_phase+=32000;count=audio_phase/60;audio_phase%=60;
   check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   pause[0].button=0;
   auto resumed_frame=match.source_frames();
   for(unsigned t=0;t<30&&match.paused();++t){
    match.tick(pause);audio_phase+=32000;count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
    resumed_frame=match.source_frames();
   }
   check(!match.paused()&&resumed_frame>paused_frame,
         "Original P1 Start did not resume after pause debounce");
   std::cout<<"Original Ready/Go completed at "<<intro_ticks<<" ticks\n";
   check(match.hud_damage(0)==0&&match.hud_damage(1)==0,"Original player damage HUD did not initialize");
   // Battlefield's authored spawn puts P2 on the upper platform. Use source
   // input to reach the main floor before testing a horizontal projectile.
   for(unsigned t=0;t<180&&
       std::abs(match.player_stats(1).position[1]-
                match.player_stats(0).position[1])>=5.0f;++t){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;pads[1].stickY=-80;
    match.tick(pads);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(std::abs(match.player_stats(1).position[1]-
                  match.player_stats(0).position[1])<5.0f,
         "Original input did not bring both fighters to the same stage level");
   // Approach with source input so the opponent lies inside the projectile's
   // actual lifetime and range.
   for(unsigned t=0;t<120&&
       std::abs(match.player_stats(1).position[0]-
                match.player_stats(0).position[0])>=35.0f;++t){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
    pads[0].stickX=match.player_stats(1).position[0]>
                           match.player_stats(0).position[0]?80:-80;
    match.tick(pads);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(std::abs(match.player_stats(1).position[0]-
                  match.player_stats(0).position[0])<35.0f,
         "Original movement did not reach projectile test range");
   // Let the actual selected fighter projectile hit the opponent, then
   // observe the original HUD consumer catching up to source player damage.
   const float projectile_start_damage=match.player_stats(1).damage_percent;
   for(unsigned t=0;t<240&&
       match.player_stats(1).damage_percent==projectile_start_damage;++t){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
    if(t%8==0)pads[0].button=PAD_BUTTON_B;
    match.tick(pads);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   }
   for(unsigned t=0;t<30&&
       match.hud_damage(1)!=int(match.player_stats(1).damage_percent);++t){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
    match.tick(pads);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(match.player_stats(1).damage_percent>projectile_start_damage&&
         match.hud_damage(1)==int(match.player_stats(1).damage_percent),
         "Original HUD did not display actual projectile damage");
   bool lost=false,jump=false;int stocks=4,respawns=0,winner=-1;unsigned t=0;
   unsigned ending_ticks=0;MeleeWebMatchStats held_players[2]{};
   for(;t<4000;t++){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
    if(t>=20&&!lost)pads[0].stickX=80;if(jump)pads[0].button=PAD_BUTTON_X;
    match.tick(pads);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
    auto player=match.player_stats(0);check(match.player_stats(1).stocks==4,"Stationary match opponent lost a stock");
    jump=!lost&&stocks<4&&player.ground_or_air==0&&player.position[0]>65;
    if(player.stocks<stocks){lost=true;stocks=player.stocks;}
    if(lost&&player.motion_id==14&&player.ground_or_air==0){lost=false;++respawns;}
    const int outcome=match.outcome(winner);
    if(match.ending()){
     check(outcome!=0,"Original ending started before the source outcome");
     for(unsigned slot=0;slot<2;++slot){
      const auto current=match.player_stats(slot);
      if(ending_ticks){
       check(current.motion_id==held_players[slot].motion_id&&
             current.animation_frame==held_players[slot].animation_frame&&
             current.position[0]==held_players[slot].position[0]&&
             current.position[1]==held_players[slot].position[1]&&
             current.stocks==held_players[slot].stocks,
             "Fighter processes advanced during original GAME freeze");
      }else held_players[slot]=current;
     }
     ++ending_ticks;
    }
    if(match.complete()){check(outcome!=0&&ending_ticks>0,"Source exit skipped the original ending");break;}
   }
   check(t<4000&&stocks==0&&respawns==3,"Native menu match did not complete original four-stock outcome");
   std::cout<<"Original GAME ending and source transition completed across "<<ending_ticks<<" frozen ticks\n";
   const uint32_t seed=match.random_seed();match.close();
   int terminal_outcome=0,winner_count=0,winners[6]{};
   check(melee_web_match_rules_terminal_result(&terminal_outcome,&winner_count,winners)&&
         terminal_outcome==OUTCOME_ELIMINATION&&winner_count==1&&winners[0]==1,
         "Native menu match did not publish the original elimination winner at close");
   check(melee_web_menu_host_match_finished(host,seed,error,sizeof(error)),error);
  }
  world=std::make_unique<melee_web::GameplayMenuWorld>(files);audio_phase=0;
  check(melee_web_menu_host_enter(host,world->audio(),error,sizeof(error)),error);
  for(unsigned t=0;t<120;t++)check(tick()==1,"Unexpected return CSS transition");
  check(melee_web_menu_host_leave(host,1,error,sizeof(error)),error);
  world->verify_immutable_archives();world->close();world->close();world.reset();
  check(melee_web_menu_host_destroy(host,error,sizeof(error)),error);
 }
 std::cout<<"Native original CSS Mario/Falco to SSS to four-stock match to CSS passed twice; no browser or equivalence claim\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
