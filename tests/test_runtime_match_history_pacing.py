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
        ])
        sample_type = section(history, 'struct MatchHistoryPacingSample {',
                              'struct MatchHistoryInput {')
        start = section(history, 'int melee_web_native_menu_match_history_pacing_start()',
                        'const MatchHistoryPacingSample* melee_web_native_menu_match_history_pacing_sample(')
        sample = section(history,
            'const MatchHistoryPacingSample* melee_web_native_menu_match_history_pacing_sample(',
            'unsigned melee_web_native_menu_match_history_pacing_sample_bytes()')

        fixture = r'''#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include "animation_clock.hpp"
namespace melee_web {
@TYPES@
}
namespace melee_web_menu_browser {
using Phase=melee_web::RuntimeMatchHistoryPhase;
using Gate=melee_web::RuntimeMatchHistoryGatePoint;
using Result=melee_web::RuntimeMatchHistoryGateResult;
@SAMPLE_TYPE@
struct MatchHistoryStatus {
  uint32_t owner_state=1,first_failure=0,pending_command=0,last_completed_command=18;
  uint32_t request_seq=18,complete_seq=18,local_port=0,remote_port=1;
};
struct Capsule {
  Phase phase_=Phase::Armed;
  uint32_t frame_=0,pre_=0,forward_=0,poll_=0,restore_=0;
  bool endpoint_=false,ticket_ok=true;
  uint32_t tail_cursor=0,tail_count=0;
  Phase phase() const {return phase_;}
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
unsigned match_handle_accesses=0,match_dereferences=0;
struct MatchOwner {
  Match* value=nullptr;
  explicit operator bool() const {++match_handle_accesses;return value!=nullptr;}
  Match* get() const {++match_handle_accesses;return value;}
  Match* operator->() const {++match_handle_accesses;return value;}
  Match& operator*() const {++match_handle_accesses;++match_dereferences;return *value;}
};
struct MatchHistoryControl {
  MatchHistoryStatus status{};
  Capsule* capsule=nullptr;
  Match* match_owner=nullptr;
  bool dispatching=false,identity_ready=true,episode_enabled=true,arena_enabled=true;
  bool pacing_started=false,pacing_failed=false;
  bool terminal_tail_handoff_ready=false;
  PrefixHandoff prefix_handoff{};
  MatchHistoryPacingSample pacing{};
  std::array<char,256> error{};
};
struct Match session{};
Capsule capsule{};
MatchOwner match{};
MatchHistoryControl match_history{};
bool match_history_terminal=true,match_history_in_tick=false;
bool runtime_idle=true,renderer_idle=true,boundary_admitted=true;
unsigned boundary_calls=0,refusal_calls=0;
Gate last_boundary_gate=Gate::CapturePre;
double now_ms=0;
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
Result history_boundary(const Match& owner,Gate point,uint32_t expected,void*) noexcept {
  ++boundary_calls;
  last_boundary_gate=point;
  return boundary_admitted&&const_cast<Match&>(owner).source_frames()==expected
      ?Result::Admitted:Result::WrongFrame;
}
double emscripten_get_now() noexcept {return now_ms;}
@START@
@SAMPLE@
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
  boundary_calls=refusal_calls=match_handle_accesses=match_dereferences=0;
  now_ms=time;
  menu_clock.reset();
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
                              .replace('@SAMPLE@', sample))
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
