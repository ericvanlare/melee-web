// Synthetic native endpoint inputs to actual production POD book and verbatim exports.
// No game, GPU, clock scheduling, async/native-cancel or restore claim.
#include "natural_one_tick_enrollment.hpp"
#include <cstdio>
#include <memory>
#include <string>
using Book=melee_web::detail::NaturalOneTickEnrollment;
#define REQUIRE(x) do{if(!(x)){std::fprintf(stderr,"natural enrollment control line %d\n",__LINE__);return 1;}}while(false)
struct Match {unsigned source_frames()const{return 180;}};
std::unique_ptr<Match> match=std::make_unique<Match>();unsigned asset_generation=9,audio_phase=0;bool running=true;
bool ready(){return true;}
struct {bool requested=false,terminal=false,active=false;Match* owner=nullptr;unsigned owner_generation=0,requested_frame=0;} observation;
Book natural_one;
int melee_web_native_menu_frame_effect_arm(unsigned expected_frame){
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
 if(natural_one.requested())return 0;
#endif
 if(observation.requested||observation.terminal||!running||!ready()||
    match->source_frames()!=expected_frame)return 0;
 observation.owner=match.get();observation.owner_generation=asset_generation;
 observation.requested=true;observation.requested_frame=expected_frame;
 return 1;
}

int melee_web_native_menu_frame_effect_request_natural_one(unsigned expected_frame){
 if(observation.requested||observation.terminal||!running||!ready()||match->source_frames()!=expected_frame)return 0;
 return natural_one.request(reinterpret_cast<uintptr_t>(match.get()),asset_generation,expected_frame,audio_phase)?1:0;
}

const char* melee_web_native_menu_frame_effect_natural_one_enrollment(){
 if(natural_one.facts.callback_open||observation.active||!observation.terminal)return "{\"busy\":true}";
 static std::string out;const auto& f=natural_one.facts;
 out="{\"schema\":\"melee-web-natural-one-tick-enrollment-v1\",\"copied\":true";
 const auto number=[&](const char* name,auto v){out+=",\"";out+=name;out+="\":";out+=std::to_string(v);};
 const auto wide=[&](const char* name,uint64_t v){out+=",\"";out+=name;out+="\":\"";out+=std::to_string(v);out+='\"';};
 number("state",f.state);number("minimum_frame",natural_one.MinimumFrame);number("maximum_frame",natural_one.MaximumFrame);
 number("origin_frame",f.origin_frame);number("selected_frame",f.selected_frame);number("owner_generation",f.owner_generation);number("origin_audio_phase",f.origin_audio_phase);
 number("row_capacity",natural_one.RowCapacity);number("row_count",f.row_count);
 wide("callback_sequence",f.callback_sequence);wide("selection_callback",f.selection_callback);wide("skipped_callbacks",f.skipped_callbacks);
 wide("skipped_ticks",f.skipped_ticks);wide("skipped_draws",f.skipped_draws);wide("skipped_audio",f.skipped_audio);wide("skipped_pcm_frames",f.skipped_pcm_frames);
 wide("source_free_callbacks",f.source_free_callbacks);wide("source_free_audio",f.source_free_audio);wide("source_free_pcm_frames",f.source_free_pcm_frames);
 out+=",\"callback_open\":";out+=f.callback_open?"true":"false";out+=",\"error\":\"";out+=f.error;out+="\",\"rows\":[";
 for(uint32_t i=0;i<f.row_count;++i){const auto& row=f.rows[i];if(i)out+=',';out+='{';
  out+="\"callback_sequence\":\""+std::to_string(row.callback_sequence)+"\"";
  number("source_before",row.source_before);number("source_after",row.source_after);number("planned_steps",row.planned_steps);number("planned_audio_steps",row.planned_audio_steps);
  number("tick_returns",row.tick_returns);number("draw_returns",row.draw_returns);number("audio_returns",row.audio_returns);number("pcm_frames",row.pcm_frames);
  number("audio_phase_before",row.audio_phase_before);number("audio_phase_after",row.audio_phase_after);out+=",\"selected\":";out+=row.selected?"true":"false";out+='}';
 }
 out+="]}";return out.c_str();
}
int main(){
 REQUIRE(melee_web_native_menu_frame_effect_request_natural_one(180)==1);
 REQUIRE(!observation.requested);REQUIRE(melee_web_native_menu_frame_effect_arm(180)==0);REQUIRE(!observation.requested);
 REQUIRE(melee_web_native_menu_frame_effect_request_natural_one(180)==0);
 Book b;REQUIRE(b.request(91,9,180,0));
 b.begin_callback(180,0);b.plan(0,1,false,false);b.audio_returned(533);b.end_callback(91,9,180,20,0,0,true);
 REQUIRE(b.pending());REQUIRE(b.facts.source_free_callbacks==1&&b.facts.source_free_audio==1&&b.facts.source_free_pcm_frames==533);REQUIRE(b.facts.row_count==0);
 b.begin_callback(180,20);b.plan(2,1,false,false);REQUIRE(!b.before_tick(91,9,180,0,2,1,true));
 b.tick_returned();b.draw_returned();REQUIRE(!b.before_tick(91,9,181,1,2,1,true));b.tick_returned();b.draw_returned();b.audio_returned(533);b.end_callback(91,9,182,40,2,2,true);
 REQUIRE(b.pending());REQUIRE(b.facts.skipped_ticks==2&&b.facts.skipped_draws==2&&b.facts.skipped_audio==2&&b.facts.skipped_pcm_frames==1066);REQUIRE(b.facts.row_count==1);
 b.begin_callback(182,40);b.plan(1,1,false,false);REQUIRE(b.before_tick(91,9,182,0,1,1,true));REQUIRE(b.facts.state==Book::Selected&&b.facts.selected_frame==182);
 b.tick_returned();b.draw_returned();b.audio_returned(534);b.end_callback(91,9,183,0,1,1,true);
 REQUIRE(b.facts.state==Book::Completed);REQUIRE(b.facts.callback_sequence==3&&b.facts.selection_callback==3);REQUIRE(b.facts.row_count==2&&b.facts.rows[1].selected);
 const auto positive=b;unsigned refused=0;
 for(unsigned mode=0;mode<10;++mode){
  Book x;REQUIRE(x.request(91,9,mode==3?240:180,0));
  if(mode==0){REQUIRE(!x.request(91,9,180,0));++refused;continue;}
  if(mode==1){x.facts.callback_sequence=UINT64_MAX;x.begin_callback(180,0);}
  else{
   const unsigned frame=mode==3?240:180;x.begin_callback(frame,0);x.plan(mode==2?9:mode==3||mode==8?2:1,0,mode==4,mode==5);
   if(mode==6)REQUIRE(!x.before_tick(92,9,frame,0,1,0,true));
   else if(mode==7)REQUIRE(!x.before_tick(91,10,frame,0,1,0,true));
   else if(mode==3||mode==8){REQUIRE(!x.before_tick(91,9,frame,0,2,0,true));x.tick_returned();x.draw_returned();x.tick_returned();x.draw_returned();if(mode==8)x.facts.row_count=Book::RowCapacity;x.end_callback(91,9,frame+2,0,2,2,true);}
   else if(mode==9){REQUIRE(x.before_tick(91,9,frame,0,1,0,true));x.tick_returned();x.draw_returned();x.tick_returned();x.draw_returned();x.end_callback(91,9,frame+2,0,2,2,true);}
  }
  REQUIRE(x.facts.state==Book::Failed);++refused;
 }
 Book aborted;REQUIRE(aborted.request(91,9,180,0));aborted.begin_callback(180,0);aborted.aborted();REQUIRE(aborted.facts.state==Book::Failed&&!aborted.facts.callback_open);++refused;
 natural_one=positive;observation.terminal=true;
 std::puts(melee_web_native_menu_frame_effect_natural_one_enrollment());
 std::fprintf(stderr,"actual shared book positive0/2/1 + %u refusals + actual legacy overlap/duplicate controls PASS\n",refused);
 return 0;
}
