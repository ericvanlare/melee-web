"""Focused extraction of the real pause guards; lifecycle leaves are modeled."""
from pathlib import Path
import os
from test_stadium_current_sss_exit import run
from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(os.environ.get("MELEE_STADIUM_LONG_PAUSE_CHECKOUT", Path(__file__).resolve().parents[1])).resolve()


def function(text, signature):
    assert text.count(signature) == 1, signature
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise ValueError(signature)


def fixture_source():
    source_path = Path(os.environ.get("MELEE_STADIUM_LONG_PAUSE_SOURCE", ROOT / "src/gameplay_menu_browser.cpp")).resolve()
    browser = source_path.read_text()
    header = (ROOT / "src/gameplay_match_flow.h").read_text()
    observation_start = header.index("typedef struct MeleeWebMatchPauseObservation {")
    observation_end = header.index("} MeleeWebMatchPauseObservation;", observation_start) + len("} MeleeWebMatchPauseObservation;")
    observation = header[observation_start:observation_end]
    helpers = "\n".join(function(browser, signature) for signature in (
        "bool later_sss_pause_pads(",
        "bool later_sss_pause_before(",
        "void later_sss_pause_after(",
    ))
    context = function(browser, "const melee_web::stadium_later_reference::Context& later_sss_reference_context(){")
    return '#include "'+str(ROOT / 'src/stadium_later_reference.hpp')+'"\n' + r'''
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>
'''+observation+r'''
enum { OUTCOME_NONE=0, OUTCOME_NO_CONTEST=7 };
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct PADStatus {unsigned button=0;int stickX=0,stickY=0,substickX=0,substickY=0;unsigned triggerLeft=0,triggerRight=0,analogA=0,analogB=0;int err=0;};
struct Frame {std::array<PADStatus,4> pads{};};
struct Recipe {std::vector<Frame> frames;};
struct ModelMatch {
 MeleeWebMatchPauseObservation observed{};bool getter_ok=true;
 bool diagnostic_pause_observation(MeleeWebMatchPauseObservation& out){if(!getter_ok)return false;out=observed;return true;}
};
struct LaterSssState {
 const melee_web::stadium_later_reference::Context* reference=&melee_web::stadium_later_reference::legacy;
 bool armed=true;unsigned stage=2;const ModelMatch* pause_match_owner=nullptr;
 uint64_t pause_generation=0;size_t pause_next_cursor=450;bool pause_finished=false;
} later_sss;
std::unique_ptr<ModelMatch> match;
std::unique_ptr<Recipe> replay;
size_t replay_cursor=450;
unsigned completed_matches=0;
bool context_ok=true,topology_ok=true;
'''+context+r'''
bool later_sss_context_owned(){return context_ok&&match&&match.get()==later_sss.pause_match_owner&&replay;}
bool later_sss_recipe_valid(const Recipe&){return topology_ok;}
'''+helpers+r'''
void initialize(const melee_web::stadium_later_reference::Context& reference){
 later_sss={};later_sss.reference=&reference;later_sss.armed=true;later_sss.stage=2;
 later_sss.pause_next_cursor=reference.pause_first;later_sss.pause_finished=false;
 replay=std::make_unique<Recipe>();replay->frames.resize(reference.frames);
 for(auto& frame:replay->frames){frame.pads[2].err=-1;frame.pads[3].err=-1;}
 for(size_t cursor=reference.pause_first;cursor<=reference.pause_last;++cursor){
  if(cursor==reference.pause_first||cursor==reference.pause_last){
   auto& port0=replay->frames[cursor].pads[0];port0.button=0x1160;port0.triggerLeft=port0.triggerRight=255;
  }
 }
 match=std::make_unique<ModelMatch>();later_sss.pause_match_owner=match.get();
 replay_cursor=reference.pause_first;completed_matches=0;context_ok=topology_ok=true;
}
void run_window(const melee_web::stadium_later_reference::Context& reference,
                const unsigned* expected_before,const unsigned* expected_after,
                const unsigned* source_tick,const unsigned* source_sequence,unsigned count){
 initialize(reference);
 for(unsigned i=0;i<count;++i){
  const size_t cursor=reference.pause_first+i;replay_cursor=cursor;
  assert(source_tick[i]==(reference.profile==4?180U+i:i));
  assert(source_sequence[i]==(reference.profile==4?2599U+5U*i:0U));
  match->observed={9,reference.pause_frame,expected_before[i],cursor!=reference.pause_first,OUTCOME_NONE,0};
  later_sss_pause_before(replay->frames[cursor].pads.data());
  const bool terminal=cursor==reference.pause_last;
  match->observed={9,reference.pause_frame,expected_after[i],1,terminal?OUTCOME_NO_CONTEST:OUTCOME_NONE,terminal?1:0};
  later_sss_pause_after();
 }
 assert(later_sss.pause_finished&&later_sss.pause_next_cursor==reference.pause_last+1);
}
int main(){
 const unsigned legacy_before[]={0,10,9,8,7,6,5,4,3,2,1,0};
 const unsigned legacy_after[]={10,9,8,7,6,5,4,3,2,1,0,0};
 const unsigned legacy_ticks[]={0,1,2,3,4,5,6,7,8,9,10,11};
 const unsigned legacy_sequences[]={0,0,0,0,0,0,0,0,0,0,0,0};
 run_window(melee_web::stadium_later_reference::legacy,legacy_before,legacy_after,legacy_ticks,legacy_sequences,12);
 const unsigned long_before[]={0,10,9,8,7,6,5,4,3,2,1,0,0};
 const unsigned long_after[]={10,9,8,7,6,5,4,3,2,1,0,0,0};
 const unsigned long_ticks[]={180,181,182,183,184,185,186,187,188,189,190,191,192};
 const unsigned long_sequences[]={2599,2604,2609,2614,2619,2624,2629,2634,2639,2644,2649,2654,2659};
 run_window(melee_web::stadium_later_reference::observed_b_long,long_before,long_after,long_ticks,long_sequences,13);
 // The retained cursor-468 boundary is one before the native timer reaches zero.
 initialize(melee_web::stadium_later_reference::observed_b_long);replay_cursor=468;later_sss.pause_next_cursor=468;later_sss.pause_generation=9;
 match->observed={9,56,0,1,OUTCOME_NONE,0};bool refused=false;
 try{later_sss_pause_before(replay->frames[468].pads.data());}catch(const std::runtime_error&){refused=true;}
 assert(refused);
}
'''


class LongBPauseGuardActualFunctions(OwnedWorkspaceTests):
    def test_long_literal_boundary_and_legacy_window(self):
        source = fixture_source()
        run(source, self.new_workspace(ROOT, "long-b-pause-guard-"))
