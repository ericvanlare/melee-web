# SPDX-License-Identifier: GPL-3.0-or-later
"""Focused extracted controls for the private native paced-history owner."""
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT
sys.path.insert(0, str(SOURCE_ROOT / 'scripts'))
sys.path.insert(0, str(SOURCE_ROOT / 'tests'))
from owned_test_workspace import OwnedWorkspaceTests


class RuntimeMatchHistoryPacingControls(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.work = cls.new_workspace(ROOT, 'runtime-match-history-pacing-')

    def test_extracted_native_pacing_and_actual_fixed_clock(self):
        history = (ROOT / 'src/slippi_profile/runtime_match_history_menu.inc').read_text()
        native_header = (SOURCE_ROOT / 'src/slippi_profile/runtime_match_history.hpp').read_text()

        def section(text, start, end):
            begin = text.index(start)
            return text[begin:text.index(end, begin)]

        types = '\n'.join([
            section(native_header, 'enum class RuntimeMatchHistoryPhase',
                    'enum class RuntimeMatchHistoryFailure'),
            section(native_header, 'enum class RuntimeMatchHistoryGatePoint',
                    'using RuntimeMatchHistoryAudioStep'),
            section(native_header, 'enum class RuntimeMatchHistoryFailure',
                    'struct RuntimeMatchHistoryMetadata'),
            section(native_header, 'struct RuntimeMatchHistoryMetadata {',
                    '/* Separate private terminal evidence.'),
            section(native_header, 'struct RuntimeMatchHistoryTerminalTailTicket {',
                    '/* Private optional episode receipts.'),
            section(native_header, 'struct RuntimeMatchHistoryPollTicket {',
                    'struct RuntimeMatchHistoryInputReceipt'),
        ])
        sample_type = section(history, 'struct MatchHistoryPacingSample {',
                              'struct MatchHistoryInput {')
        start = section(history, 'int melee_web_native_menu_match_history_pacing_start()',
                        'const MatchHistoryPacingSample* history_pacing_sample_at(')
        sample = section(history,
            'const MatchHistoryPacingSample* history_pacing_sample_at(unsigned max_steps,double actual_now) noexcept {',
            'unsigned melee_web_native_menu_match_history_pacing_sample_bytes()')
        wait_helpers = section(history, 'bool history_pacing_metadata_equal(',
                               'int melee_web_native_menu_match_history_pacing_start()')
        request = section(history, 'int history_request(unsigned command,',
                          'bool history_terminal_tail_final_receipt()')

        fixture = r'''#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <limits>
#include "animation_clock.hpp"
#include "slippi_profile/runtime_match_history_terminal_cursor.h"
namespace melee_web {
@TYPES@
}
namespace melee_web_menu_browser {
using Phase=melee_web::RuntimeMatchHistoryPhase;
using Gate=melee_web::RuntimeMatchHistoryGatePoint;
using Result=melee_web::RuntimeMatchHistoryGateResult;
@SAMPLE_TYPE@
using MatchHistoryPollTicket=melee_web::RuntimeMatchHistoryPollTicket;
struct MatchHistoryStatus {
  uint32_t owner_state=1,first_failure=0,pending_command=0,last_completed_command=18;
  uint32_t request_seq=18,complete_seq=18,local_port=0,remote_port=1;
  melee_web::RuntimeMatchHistoryMetadata history{};
};
struct Capsule {
  Phase phase_=Phase::Armed;
  uint32_t frame_=0,pre_=0,forward_=0,poll_=0,restore_=0;
  bool endpoint_=false,ticket_ok=true;
  uint32_t tail_cursor=0,tail_count=0;
  melee_web::RuntimeMatchHistoryMetadata metadata_{};
  MeleeWebRuntimeMatchHistoryTerminalCursor cursor_{};
  melee_web::RuntimeMatchHistoryPollTicket latest_{};
  bool latest_valid=false;
  melee_web::RuntimeMatchHistoryTerminalTailTicket tail_{};
  bool tail_valid=false;
  Phase phase() const {return phase_;}
  melee_web::RuntimeMatchHistoryMetadata metadata() const {return metadata_;}
  melee_web::RuntimeMatchHistoryFailure first_failure() const {return melee_web::RuntimeMatchHistoryFailure::None;}
  const MeleeWebRuntimeMatchHistoryTerminalCursor& terminal_cursor() const {return cursor_;}
  const melee_web::RuntimeMatchHistoryPollTicket* latest_ticket() const {return latest_valid?&latest_:nullptr;}
  const melee_web::RuntimeMatchHistoryTerminalTailTicket* terminal_tail_ticket() const {return tail_valid?&tail_:nullptr;}
  bool terminal_selected_at(uint32_t frame) const {
    return cursor_.selected_terminal_valid&&cursor_.selected_terminal_source_frame_after==frame;
  }
  uint32_t current_frame() const {return frame_;}
  uint32_t pre_frame() const {return pre_;}
  uint32_t forward_steps() const {return forward_;}
  uint64_t poll_serial() const {return poll_;}
  uint32_t restore_count() const {return restore_;}
  bool endpoint_active() const {return endpoint_;}
  bool can_admit_poll_ticket(uint32_t,unsigned,unsigned) const {return ticket_ok;}
  bool can_admit_terminal_tail_poll_ticket(uint32_t,unsigned,unsigned) const {
    return ticket_ok&&tail_cursor==tail_count;
  }
  uint32_t terminal_tail_suffix_cursor()const {return tail_cursor;}
  uint32_t terminal_tail_suffix_count()const {return tail_count;}
  uint32_t terminal_tail_source_frame()const {return tail_.source_frame;}
};
struct PrefixHandoff {
  bool enabled=false;
  uint32_t request=0,cursor=0,count=0;
  bool active() const {return enabled;}
  uint32_t rebase_request_sequence() const {return request;}
  uint32_t suffix_cursor() const {return cursor;}
  uint32_t suffix_count() const {return count;}
};
struct Match {
  uint32_t frame=0;
  unsigned source_reads=0;
  uint32_t source_frames() {++source_reads;return frame;}
};
using RuntimeMatchHistory=Capsule;
using GameplayMatchSession=Match;
struct MatchHistoryPacingWait {
 bool active{};
 uint32_t kind{},input_ordinal{},source_frame{},phase{},request_sequence{},complete_sequence{},last_completed_command{};
 uint32_t local_port{},remote_port{};
 double started_raw_ms{},last_observed_raw_ms{},effective_hold_ms{},excluded_ms{},total_excluded_ms{};
 RuntimeMatchHistory* capsule{};
 GameplayMatchSession* match_owner{};
 melee_web::RuntimeMatchHistoryMetadata history{};
 MeleeWebRuntimeMatchHistoryTerminalCursor terminal_cursor{};
 melee_web::RuntimeMatchHistoryPollTicket latest_ticket{};
 bool latest_ticket_valid{};
 melee_web::RuntimeMatchHistoryTerminalTailTicket tail_ticket{};
 bool tail_ticket_valid{};
};
unsigned match_handle_accesses=0,match_dereferences=0;
struct MatchOwner {
  Match* value=nullptr;
  explicit operator bool() const {++match_handle_accesses;return value!=nullptr;}
  Match* get() const {++match_handle_accesses;return value;}
  Match* operator->() const {++match_handle_accesses;return value;}
  Match& operator*() const {++match_handle_accesses;++match_dereferences;return *value;}
};
struct CapsuleHandle {
  Capsule* value=nullptr;
  Capsule* get() const {return value;}
  Capsule* operator->() const {return value;}
  explicit operator bool() const {return value!=nullptr;}
  CapsuleHandle& operator=(Capsule* p){value=p;return *this;}
};
struct MatchHistoryControl {
  MatchHistoryStatus status{};
  CapsuleHandle capsule{};
  Match* match_owner=nullptr;
  bool dispatching=false,identity_ready=true,episode_enabled=true,arena_enabled=true;
  bool pacing_started=false,pacing_failed=false;
  bool terminal_tail_handoff_ready=false,remote_ready=false,terminal_tail_remote_ready=false;
  bool inline_dispatch=true;
  uint64_t submitted_frame=1,presentation_serial=1;
  uint32_t presentation_command=19;
  uint32_t requested_rebase_end=0;
  PrefixHandoff prefix_handoff{};
  MatchHistoryPacingSample pacing{};
  MatchHistoryPacingWait pacing_wait{};
  std::array<char,512> pacing_wait_json{};
  double pacing_raw_anchor_ms=0,pacing_effective_anchor_ms=0,pacing_total_excluded_ms=0;
  bool pacing_clock_anchor_valid=false;
  std::array<char,256> error{};
};
struct Match session{};
Capsule capsule{};
MatchOwner match{};
MatchHistoryControl match_history{};
bool match_history_terminal=true,match_history_in_tick=false;
bool runtime_idle=true,renderer_idle=true,boundary_admitted=true;
bool hps_pending=false;
unsigned boundary_calls=0,refusal_calls=0;
Gate last_boundary_gate=Gate::CapturePre;
double now_ms=0,boundary_advance_ms=0;
melee_web::FixedTickClock menu_clock{melee_web::FixedTickClock::OverrunPolicy::Pause};
bool history_refuse(const char* text) noexcept {
  ++refusal_calls;
  if(!match_history.status.first_failure){
    match_history.status.first_failure=1;
    std::snprintf(match_history.error.data(),match_history.error.size(),"%s",text);
  }
  return false;
}
bool history_runtime_idle() noexcept {return runtime_idle;}
bool history_gpu_idle() noexcept {return renderer_idle;}
bool history_start_export_idle() noexcept {
  return runtime_idle&&!match_history_in_tick&&!match_history.dispatching&&
         !match_history.status.pending_command;
}
bool history_interior_hps_authorized(const Match&,Gate point,uint32_t expected) noexcept {
  return hps_pending&&point==Gate::InteriorPresentComplete&&expected==session.frame;
}
Result history_boundary(const Match& owner,Gate point,uint32_t expected,void*) noexcept {
  ++boundary_calls;
  last_boundary_gate=point;
  const uint32_t actual=const_cast<Match&>(owner).source_frames();
  now_ms+=boundary_advance_ms;
  return boundary_admitted&&actual==expected
      ?Result::Admitted:Result::WrongFrame;
}
double emscripten_get_now() noexcept {return now_ms;}
using EM_BOOL=int;
constexpr EM_BOOL EM_FALSE=0;
EM_BOOL history_dispatch(double,void*) noexcept {return EM_FALSE;}
EM_BOOL emscripten_request_animation_frame(EM_BOOL(*)(double,void*),void*) noexcept {return 0;}
@WAIT_HELPERS@
@START@
#define MELEE_WEB_ROLLBACK_STAGING 1
@SAMPLE@
#undef MELEE_WEB_ROLLBACK_STAGING
@REQUEST@
void reset_fixture(double time=0) {
  match_history=MatchHistoryControl{};
  capsule=Capsule{};
  session=Match{};
  match.value=&session;
  match_history.capsule=&capsule;
  match_history.match_owner=&session;
  match_history_terminal=true;
  match_history_in_tick=false;
  runtime_idle=renderer_idle=boundary_admitted=true;
  hps_pending=false;
  boundary_calls=refusal_calls=match_handle_accesses=match_dereferences=0;
  now_ms=time;boundary_advance_ms=0;
  menu_clock.reset();
  capsule.metadata_.schema=1;capsule.metadata_.bytes=64;
  capsule.metadata_.phase=static_cast<uint32_t>(capsule.phase_);
  capsule.metadata_.pre_frame=capsule.pre_;
  capsule.metadata_.current_frame=capsule.frame_;
  capsule.cursor_.bound=1;
  capsule.cursor_.owner.capsule_identity=&capsule;
  capsule.cursor_.owner.checkpoint_identity=&capsule;
  capsule.cursor_.owner.storage_identity=&session;
  capsule.cursor_.owner.context_identity=&session;
  capsule.cursor_.owner.flow_identity=&session;
  capsule.cursor_.owner.audio_identity=&session;
  capsule.cursor_.owner.ticks_identity=&session;
  capsule.cursor_.owner.world_generation=1;
  capsule.cursor_.owner.context_epoch=1;
  capsule.cursor_.owner.ticks_bytes=sizeof(uint64_t);
}
void set_wait_recording_state(uint32_t frame,uint32_t phase,uint32_t completed=5){
  capsule.phase_=static_cast<Phase>(phase);capsule.frame_=frame;session.frame=frame;
  capsule.forward_=frame-capsule.pre_;
  capsule.metadata_.phase=phase;capsule.metadata_.current_frame=frame;
  capsule.metadata_.forward_steps=capsule.forward_;
  match_history.status.history=capsule.metadata_;
  match_history.status.last_completed_command=completed;
  match_history.status.request_seq=match_history.status.complete_seq=20;
  match_history.presentation_command=19;match_history.presentation_serial=2;
  match_history.submitted_frame=frame;
}
} // namespace melee_web_menu_browser
int main() {
  using namespace melee_web_menu_browser;
  // Start seeds the real 60 Hz clock at native time. Each admitted sample grants
  // at most one due step, including after later recording resumes from a wait.
  reset_fixture(1000.0);
  assert(melee_web_native_menu_match_history_pacing_start()==1);
  assert(match_history.pacing_started&&!match_history.pacing_failed);
  assert(match_history.pacing.phase==static_cast<uint32_t>(Phase::Armed));
  assert(match_history.pacing.current_frame==0&&menu_clock.pending_ticks()==0.0);
  now_ms+=1000.0/60.0;
  const auto* receipt=melee_web_native_menu_match_history_pacing_sample(1);
  assert(receipt&&receipt->steps==1&&receipt->pending_steps==0);
  assert(last_boundary_gate==Gate::ForwardBefore);
  capsule.phase_=Phase::EpisodeRecording;capsule.frame_=1;capsule.forward_=1;session.frame=1;
  match_history.status.last_completed_command=5;
  match_history.status.request_seq=match_history.status.complete_seq=19;
  now_ms+=1000.0/60.0;
  receipt=melee_web_native_menu_match_history_pacing_sample(0);
  assert(receipt&&receipt->steps==0&&last_boundary_gate==Gate::InteriorPoll);
  now_ms+=1000.0/60.0;
  receipt=melee_web_native_menu_match_history_pacing_sample(1);
  assert(receipt&&receipt->steps==1&&receipt->current_frame==1&&
         last_boundary_gate==Gate::InteriorPoll);

  // A retired-to-rebased interval has no capsule and consumes no due debt.
  // The next valid PRE sample receives one grant and leaves the remaining debt.
  reset_fixture(2000.0);
  assert(melee_web_native_menu_match_history_pacing_start()==1);
  match_history.capsule=nullptr;
  now_ms+=50.0;
  receipt=melee_web_native_menu_match_history_pacing_sample(1);
  assert(receipt&&receipt->phase==0);
  assert(receipt->steps==0&&receipt->pending_steps==3);
  assert(menu_clock.pending_ticks()>2.9&&menu_clock.pending_ticks()<3.1);
  capsule=Capsule{};match_history.capsule=&capsule;
  now_ms+=1.0;
  receipt=melee_web_native_menu_match_history_pacing_sample(1);
  assert(receipt&&receipt->steps==1&&receipt->pending_steps==2);
  assert(menu_clock.pending_ticks()>1.9&&menu_clock.pending_ticks()<2.2);

  // The original ending source frame can stay fixed. Fresh actual inputs still
  // spend the same clock's due steps; retained corrective suffix rows do not.
  reset_fixture(2500.0);
  assert(melee_web_native_menu_match_history_pacing_start()==1);
  capsule.phase_=Phase::TerminalTailReady;capsule.frame_=session.frame=600;
  capsule.tail_count=2;
  match_history.terminal_tail_handoff_ready=true;
  match_history.status.last_completed_command=26;
  now_ms+=50.0;
  receipt=melee_web_native_menu_match_history_pacing_sample(1);
  assert(receipt&&receipt->phase==36&&receipt->current_frame==600);
  assert(receipt->steps==0&&receipt->pending_steps==3);
  capsule.tail_cursor=2;match_history.status.last_completed_command=5;
  now_ms+=1.0;
  receipt=melee_web_native_menu_match_history_pacing_sample(1);
  assert(receipt&&receipt->steps==1&&receipt->current_frame==600&&receipt->pending_steps==2);
  assert(last_boundary_gate==Gate::TerminalTailPoll);
  const auto tail_debt=menu_clock.pending_ticks();
  assert(tail_debt>1.9&&tail_debt<2.2);
  capsule.ticket_ok=false;now_ms+=1.0;
  assert(melee_web_native_menu_match_history_pacing_sample(0)==nullptr);
  assert(menu_clock.pending_ticks()==tail_debt);

  // A real source-input wait excludes only its measured interval. Fractional
  // debt and the full-debt guard survive the wait unchanged.
  reset_fixture(5000.0);
  assert(melee_web_native_menu_match_history_pacing_start()==1);
  now_ms+=58.3333333333333;
  receipt=melee_web_native_menu_match_history_pacing_sample(1);
  assert(receipt&&receipt->steps==1&&receipt->pending_steps==2);
  const double debt_before_wait=menu_clock.pending_ticks();
  assert(debt_before_wait>2.49&&debt_before_wait<2.51);
  set_wait_recording_state(1,static_cast<uint32_t>(Phase::EpisodeRecording));
  boundary_advance_ms=0.25; // Native admission work before wait start remains active time.
  assert(melee_web_native_menu_match_history_pacing_wait_begin(1,0)==1);
  boundary_advance_ms=0;
  const double debt_at_wait_start=menu_clock.pending_ticks();
  assert(debt_at_wait_start>debt_before_wait);
  assert(match_history.pacing_wait.active);
  const char* wait_json=melee_web_native_menu_match_history_pacing_wait_json();
  assert(wait_json&&std::strstr(wait_json,"\"active\":1")&&
         std::strstr(wait_json,"\"kind\":1")&&
         std::strstr(wait_json,"\"inputOrdinal\":0"));
  now_ms+=150.0;
  receipt=melee_web_native_menu_match_history_pacing_sample(0);
  assert(receipt&&receipt->steps==0&&menu_clock.pending_ticks()==debt_at_wait_start);
  assert(receipt->pending_ticks==debt_at_wait_start);
  assert(melee_web_native_menu_match_history_pacing_wait_end(1,0)==1);
  assert(!match_history.pacing_wait.active&&menu_clock.pending_ticks()==debt_at_wait_start);
  wait_json=melee_web_native_menu_match_history_pacing_wait_json();
  assert(wait_json&&std::strstr(wait_json,"\"active\":0")&&
         std::strstr(wait_json,"\"excludedMs\":150")&&
         std::strstr(wait_json,"\"totalExcludedMs\":150"));
  const char* pending_field=std::strstr(wait_json,"\"pendingTicks\":");
  assert(pending_field&&std::strtod(pending_field+15,nullptr)==debt_at_wait_start);
  now_ms+=150.0; // Active work after the wait still crosses the unchanged >8 guard.
  receipt=melee_web_native_menu_match_history_pacing_sample(0);
  assert(receipt&&receipt->stalled&&receipt->stall_reason==
      static_cast<uint32_t>(melee_web::FixedTickClock::StallReason::Debt));
  assert(match_history.pacing_failed);

  // The other three wait kinds bind to their exact phase/ordinal owner.
  reset_fixture(6000.0);
  assert(melee_web_native_menu_match_history_pacing_start()==1);
  set_wait_recording_state(1,static_cast<uint32_t>(Phase::EpisodeRecording));
  const double before_refill_query=menu_clock.pending_ticks();
  assert(melee_web_native_menu_match_history_refill_boundary(1)==1);
  hps_pending=true;
  assert(melee_web_native_menu_match_history_refill_boundary(1)==2);
  assert(last_boundary_gate==Gate::InteriorPresentComplete);
  assert(melee_web_native_menu_match_history_refill_boundary(2)==0);
  boundary_admitted=false;
  assert(melee_web_native_menu_match_history_refill_boundary(1)==0);
  boundary_admitted=true;
  assert(menu_clock.pending_ticks()==before_refill_query&&!match_history.status.first_failure);
  assert(melee_web_native_menu_match_history_pacing_wait_begin(2,0)==1);
  assert(melee_web_native_menu_match_history_refill_boundary(1)==0);
  now_ms+=10.0;
  assert(melee_web_native_menu_match_history_pacing_wait_end(2,0)==1);
  hps_pending=false;
  set_wait_recording_state(7,static_cast<uint32_t>(Phase::EpisodeRecording));
  assert(melee_web_native_menu_match_history_refill_boundary(7)==1);

  reset_fixture(6100.0);
  assert(melee_web_native_menu_match_history_pacing_start()==1);
  capsule.phase_=Phase::TerminalRecording;capsule.frame_=session.frame=100;
  capsule.metadata_.phase=static_cast<uint32_t>(Phase::TerminalRecording);
  capsule.metadata_.current_frame=100;
  capsule.cursor_.selected_terminal_valid=1;
  capsule.cursor_.selected_terminal_source_frame_after=100;
  capsule.cursor_.selected_terminal_input_ordinal=120;
  match_history.status.history=capsule.metadata_;
  match_history.status.last_completed_command=13;
  match_history.status.request_seq=match_history.status.complete_seq=22;
  assert(melee_web_native_menu_match_history_pacing_wait_begin(3,119)==1);
  now_ms+=10.0;
  assert(melee_web_native_menu_match_history_pacing_wait_end(3,119)==1);

  reset_fixture(6200.0);
  assert(melee_web_native_menu_match_history_pacing_start()==1);
  capsule.phase_=Phase::TerminalTailTicketed;capsule.frame_=session.frame=100;
  capsule.metadata_.phase=static_cast<uint32_t>(Phase::TerminalTailTicketed);
  capsule.metadata_.current_frame=100;
  match_history.status.history=capsule.metadata_;
  match_history.status.last_completed_command=27;
  match_history.status.request_seq=match_history.status.complete_seq=30;
  match_history.terminal_tail_handoff_ready=true;
  capsule.tail_valid=true;capsule.tail_.ticket_id=30;capsule.tail_.request_sequence=30;
  capsule.tail_.source_frame=100;capsule.tail_.input_ordinal=600;
  capsule.tail_.local_port=0;capsule.tail_.remote_port=1;
  assert(melee_web_native_menu_match_history_pacing_wait_begin(4,600)==1);
  now_ms+=10.0;
  assert(melee_web_native_menu_match_history_pacing_wait_end(4,600)==1);

  // Invalid phase/key, command attempts, owner mutation, duplicate transition,
  // and non-monotone native time all fail closed without consuming clock debt.
  reset_fixture(6300.0);
  assert(melee_web_native_menu_match_history_pacing_start()==1);
  assert(melee_web_native_menu_match_history_pacing_wait_begin(2,0)==0);
  assert(!match_history.pacing_wait.active&&match_history.status.first_failure==1);

  reset_fixture(6400.0);
  assert(melee_web_native_menu_match_history_pacing_start()==1);
  set_wait_recording_state(1,static_cast<uint32_t>(Phase::EpisodeRecording));
  assert(melee_web_native_menu_match_history_pacing_wait_begin(1,0)==1);
  const auto locked_debt=menu_clock.pending_ticks();
  assert(history_request(13,0,true)==0);
  assert(match_history.status.request_seq==20&&match_history.status.pending_command==0);
  assert(match_history.pacing_failed==false&&match_history.status.first_failure==1);
  assert(menu_clock.pending_ticks()==locked_debt);

  reset_fixture(6500.0);
  assert(melee_web_native_menu_match_history_pacing_start()==1);
  set_wait_recording_state(1,static_cast<uint32_t>(Phase::EpisodeRecording));
  assert(melee_web_native_menu_match_history_pacing_wait_begin(1,0)==1);
  assert(melee_web_native_menu_match_history_pacing_wait_begin(1,0)==0);
  assert(match_history.pacing_wait.active&&match_history.status.first_failure==1);

  reset_fixture(6600.0);
  assert(melee_web_native_menu_match_history_pacing_start()==1);
  set_wait_recording_state(1,static_cast<uint32_t>(Phase::EpisodeRecording));
  assert(melee_web_native_menu_match_history_pacing_wait_begin(1,0)==1);
  assert(melee_web_native_menu_match_history_pacing_wait_end(1,1)==0);
  assert(match_history.pacing_failed&&match_history.pacing_wait.active);

  reset_fixture(6700.0);
  assert(melee_web_native_menu_match_history_pacing_start()==1);
  set_wait_recording_state(1,static_cast<uint32_t>(Phase::EpisodeRecording));
  assert(melee_web_native_menu_match_history_pacing_wait_begin(1,0)==1);
  const auto owner_debt=menu_clock.pending_ticks();session.frame=2;
  assert(melee_web_native_menu_match_history_pacing_sample(0)==nullptr);
  assert(match_history.pacing_failed&&menu_clock.pending_ticks()==owner_debt);

  reset_fixture(6800.0);
  assert(melee_web_native_menu_match_history_pacing_start()==1);
  set_wait_recording_state(1,static_cast<uint32_t>(Phase::EpisodeRecording));
  assert(melee_web_native_menu_match_history_pacing_wait_begin(1,0)==1);
  const auto nonfinite_debt=menu_clock.pending_ticks();
  now_ms=std::numeric_limits<double>::quiet_NaN();
  assert(melee_web_native_menu_match_history_pacing_sample(0)==nullptr);
  assert(match_history.pacing.stall_reason==
      static_cast<uint32_t>(melee_web::FixedTickClock::StallReason::NonFiniteClock));
  assert(match_history.pacing_failed&&menu_clock.pending_ticks()==nonfinite_debt);

  reset_fixture(6900.0);
  assert(melee_web_native_menu_match_history_pacing_start()==1);
  set_wait_recording_state(1,static_cast<uint32_t>(Phase::EpisodeRecording));
  assert(melee_web_native_menu_match_history_pacing_wait_begin(1,0)==1);
  const auto regression_debt=menu_clock.pending_ticks();now_ms-=1.0;
  assert(melee_web_native_menu_match_history_pacing_sample(0)==nullptr);
  assert(match_history.pacing.stall_reason==
      static_cast<uint32_t>(melee_web::FixedTickClock::StallReason::ClockRegression));
  assert(match_history.pacing_failed&&menu_clock.pending_ticks()==regression_debt);

  reset_fixture(6950.0);
  assert(melee_web_native_menu_match_history_pacing_start()==1);
  now_ms+=150.0;
  receipt=melee_web_native_menu_match_history_pacing_sample(0);
  assert(receipt&&receipt->stalled&&receipt->stall_reason==
      static_cast<uint32_t>(melee_web::FixedTickClock::StallReason::Debt));
  assert(melee_web_native_menu_match_history_pacing_wait_begin(1,0)==0);
  const auto refused_stall=*melee_web_native_menu_match_history_pacing_sample(0);
  assert(refused_stall.stalled&&refused_stall.stall_reason==receipt->stall_reason);

  reset_fixture(2700.0);
  assert(melee_web_native_menu_match_history_pacing_start()==1);
  capsule.phase_=Phase::TerminalTailReady;capsule.frame_=session.frame=600;
  match_history.terminal_tail_handoff_ready=true;
  match_history.status.last_completed_command=26;
  now_ms+=150.0;
  receipt=melee_web_native_menu_match_history_pacing_sample(1);
  assert(receipt&&receipt->stalled&&receipt->stall_reason==
      static_cast<uint32_t>(melee_web::FixedTickClock::StallReason::Debt));
  assert(match_history.pacing_failed&&match_history.status.first_failure==1);

  // More than eight due ticks produces one terminal debt receipt; later queries
  // return it without touching the owner or restarting the internally reset clock.
  reset_fixture(3000.0);
  assert(melee_web_native_menu_match_history_pacing_start()==1);
  now_ms+=150.0;
  receipt=melee_web_native_menu_match_history_pacing_sample(1);
  assert(receipt&&receipt->stalled==1);
  assert(receipt->stall_reason==static_cast<uint32_t>(melee_web::FixedTickClock::StallReason::Debt));
  assert(match_history.pacing_failed&&match_history.status.first_failure==1);
  const auto first_stall=*receipt;
  const unsigned old_refusals=refusal_calls;
  match_handle_accesses=match_dereferences=session.source_reads=0;
  now_ms+=1000.0;
  receipt=melee_web_native_menu_match_history_pacing_sample(1);
  assert(receipt==&match_history.pacing&&receipt->stalled==first_stall.stalled);
  assert(receipt->stall_reason==first_stall.stall_reason&&receipt->steps==first_stall.steps);
  assert(match_handle_accesses==0&&match_dereferences==0&&session.source_reads==0);
  assert(refusal_calls==old_refusals&&menu_clock.pending_ticks()==0.0);

  // An Asyncify yield is refused before match access and before clock debt changes.
  reset_fixture(4000.0);
  assert(melee_web_native_menu_match_history_pacing_start()==1);
  match_history.capsule=nullptr;
  now_ms+=50.0;
  receipt=melee_web_native_menu_match_history_pacing_sample(1);
  assert(receipt&&receipt->pending_steps==3);
  const double debt_before=menu_clock.pending_ticks();
  assert(debt_before>2.9&&debt_before<3.1);
  runtime_idle=false;now_ms+=250.0;
  match_handle_accesses=match_dereferences=session.source_reads=0;
  assert(melee_web_native_menu_match_history_pacing_sample(0)==nullptr);
  assert(match_history.status.first_failure==1);
  assert(match_handle_accesses==0&&match_dereferences==0&&session.source_reads==0);
  assert(menu_clock.pending_ticks()==debt_before);

  // A non-finite initial clock sample also latches failure at explicit start.
  reset_fixture(std::numeric_limits<double>::quiet_NaN());
  assert(melee_web_native_menu_match_history_pacing_start()==0);
  assert(match_history.pacing_failed&&!match_history.pacing_started);
  assert(match_history.pacing.stalled==1&&match_history.status.first_failure==1);
}
'''
        source_text = (fixture.replace('@TYPES@', types)
                              .replace('@SAMPLE_TYPE@', sample_type)
                              .replace('@START@', start)
                              .replace('@WAIT_HELPERS@', wait_helpers)
                              .replace('@SAMPLE@', sample)
                              .replace('@REQUEST@', request))
        source = self.work / 'pacing.cpp'
        binary = self.work / 'pacing'
        source.write_text(source_text)
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A C++20 compiler is required')
        for name, command in [
            ('compile', [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                         '-I', str(SOURCE_ROOT / 'src'), str(source), '-o', str(binary)]),
            ('pacing-controls', [str(binary)]),
        ]:
            result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                    text=True, timeout=30)
            output = result.stdout + result.stderr
            (self.work / f'{name}.log').write_text(output)
            self.assertEqual(result.returncode, 0, output)


if __name__ == '__main__':
    unittest.main()
