# SPDX-License-Identifier: GPL-3.0-or-later
"""Focused private ordinary-runtime host transport and start controls."""
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_gameplay import node_runtime
sys.path.insert(0, str(ROOT / 'tests'))
from owned_test_workspace import OwnedWorkspaceTests


class RuntimeMatchHistoryHostControls(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.work = cls.new_workspace(ROOT, 'runtime-match-history-host-')

    def test_actual_controller_and_peer_transport_controls(self):
        tests = ROOT / 'src/slippi_profile/tests'
        result = subprocess.run([str(node_runtime(ROOT)), '--test',
            str(tests / 'test_runtime_match_history_controller.mjs'),
            str(tests / 'test_cpu_arena_endpoint.mjs'),
            str(ROOT / 'tests/runtime_match_history_peer.test.mjs')], cwd=ROOT,
            capture_output=True, text=True, timeout=30)
        (self.work / 'host-controls.log').write_text(result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_request_dispatch_treats_raf_return_as_opaque_handle(self):
        history = (ROOT / 'src/slippi_profile/runtime_match_history_menu.inc').read_text()

        def section(start, end):
            begin = history.index(start)
            return history[begin:history.index(end, begin)]

        refusal = section('bool history_refuse(', 'bool history_runtime_idle() noexcept;')
        request = '\n'.join([
            section('int history_request(', '} // namespace'),
            section('int melee_web_native_menu_match_history_request(',
                    'int melee_web_native_menu_match_history_episode_enable('),
        ])
        self.assertIn('(void)emscripten_request_animation_frame(history_dispatch,nullptr);', request)
        self.assertNotIn('EMSCRIPTEN_RESULT_SUCCESS', request)
        poll_case = section('case 12:{', 'case 13:match_history_pcm_frames=0;')
        self.assertLess(poll_case.index('can_admit_poll_ticket'), poll_case.index('melee_web_input_poll()'))
        self.assertIn('history_request(13,0,true)', history)
        self.assertIn('history_request(15,0,true)', history)

        fixture = r'''#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
using EM_BOOL = int;
constexpr EM_BOOL EM_FALSE = 0;
struct MatchHistoryStatus {
  uint32_t owner_state=1, first_failure=0, pending_command=0, last_completed_command=0;
  uint32_t request_seq=0, complete_seq=0;
};
struct Capsule { bool endpoint_active() const { return false; } };
struct MatchHistoryControl {
  MatchHistoryStatus status{};
  bool dispatching=false;
  bool episode_enabled=false;
  bool arena_enabled=false;
  uint32_t requested_rebase_end=0;
  Capsule* capsule=nullptr;
  std::array<char,512> error{};
};
MatchHistoryControl match_history{};
bool match_history_terminal=true, match_history_in_tick=false;
using Callback = EM_BOOL (*)(double,void*);
int request_handle=0, request_calls=0, dispatch_calls=0;
uint32_t dispatched_rebase_end=0;
Callback queued_callback=nullptr;
void* queued_data=nullptr;
EM_BOOL history_dispatch(double,void*);
int emscripten_request_animation_frame(Callback callback,void* data) {
  ++request_calls;
  queued_callback=callback;
  queued_data=data;
  return request_handle;
}
@REFUSAL@
EM_BOOL history_dispatch(double,void*) {
  ++dispatch_calls;
  auto& status=match_history.status;
  const auto command=status.pending_command;
  if (!command || match_history.dispatching) return EM_FALSE;
  if(command==11)dispatched_rebase_end=match_history.requested_rebase_end;
  match_history.requested_rebase_end=0;
  status.last_completed_command=command;
  status.complete_seq=status.request_seq;
  status.pending_command=0;
  return EM_FALSE;
}
@REQUEST@
void reset(int handle) {
  match_history=MatchHistoryControl{};
  match_history_terminal=true;
  match_history_in_tick=false;
  request_handle=handle;
  request_calls=dispatch_calls=0;
  dispatched_rebase_end=0;
  queued_callback=nullptr;
  queued_data=nullptr;
}
int main() {
  for (const int handle : {37,0,-1}) {
    reset(handle);
    assert(melee_web_native_menu_match_history_request(4)==1);
    assert(request_calls==1 && queued_callback==history_dispatch);
    assert(match_history.status.request_seq==1 && match_history.status.pending_command==4);
    assert(match_history.status.first_failure==0 && dispatch_calls==0);
    queued_callback(1.0,queued_data);
    assert(dispatch_calls==1 && request_calls==1);
    assert(match_history.status.last_completed_command==4);
    assert(match_history.status.complete_seq==1 && match_history.status.pending_command==0);
    assert(dispatched_rebase_end==0);

    constexpr uint32_t end=42;
    assert(melee_web_native_menu_match_history_request_rebase(end)==1);
    assert(request_calls==2 && queued_callback==history_dispatch && dispatch_calls==1);
    assert(match_history.status.request_seq==2 && match_history.status.pending_command==11);
    assert(match_history.status.first_failure==0 && match_history.requested_rebase_end==end);
    queued_callback(2.0,queued_data);
    assert(dispatch_calls==2 && request_calls==2 && dispatched_rebase_end==end);
    assert(match_history.status.last_completed_command==11);
    assert(match_history.status.complete_seq==2 && match_history.status.pending_command==0);
  }
  reset(37);
  match_history.episode_enabled=true;
  assert(melee_web_native_menu_match_history_request_rebase(77)==0);
  assert(request_calls==0 && match_history.status.request_seq==0);
  reset(37);
  match_history.episode_enabled=true;
  match_history.arena_enabled=true;
  assert(melee_web_native_menu_match_history_request_rebase(77)==1);
  assert(request_calls==1 && match_history.status.pending_command==11);
  queued_callback(3.0,queued_data);
  assert(dispatched_rebase_end==77 && match_history.status.last_completed_command==11);
  reset(37);
  assert(melee_web_native_menu_match_history_request(11)==0);
  const std::string first_error=match_history.error.data();
  assert(match_history.status.first_failure==1 && first_error=="Confirmed rebase requires an explicit END frame");
  assert(match_history.status.request_seq==0 && match_history.status.pending_command==0);
  assert(request_calls==0);
  assert(melee_web_native_menu_match_history_request(4)==0);
  assert(melee_web_native_menu_match_history_request_rebase(42)==0);
  assert(match_history.status.first_failure==1 && match_history.error.data()==first_error);
  assert(match_history.status.request_seq==0 && match_history.status.pending_command==0);
  assert(request_calls==0);
  for (const unsigned command : {13u,15u}) {
    reset(37);
    match_history.episode_enabled=true;
    assert(melee_web_native_menu_match_history_request(command)==0);
    const std::string missing_plan_error=match_history.error.data();
    assert(match_history.status.first_failure==1 && request_calls==0);
    assert(melee_web_native_menu_match_history_request(12)==0);
    assert(match_history.error.data()==missing_plan_error);
    assert(match_history.status.request_seq==0 && match_history.status.pending_command==0);
  }
}
'''
        source_text = fixture.replace('@REFUSAL@', refusal).replace('@REQUEST@', request)
        source = self.work / 'dispatch.cpp'
        binary = self.work / 'dispatch'
        source.write_text(source_text)
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A C++20 compiler is required')
        for name, command in [
            ('compile', [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                         str(source), '-o', str(binary)]),
            ('dispatch-controls', [str(binary)]),
        ]:
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=30)
            output = result.stdout + result.stderr
            (self.work / f'{name}.log').write_text(output)
            self.assertEqual(result.returncode, 0, output)

    def test_native_confirmed_rebase_keeps_legacy_one_tick_and_bounds_arena_episode(self):
        source = (ROOT / 'src/slippi_profile/runtime_match_history.cpp').read_text()
        begin = source.index('bool RuntimeMatchHistory::can_rebase_confirmed(')
        end = source.index('\nbool RuntimeMatchHistory::release_after_match_close(', begin)
        method = source[begin:end]
        fixture = r'''#include <array>
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <memory>
constexpr unsigned PAD_MAX_CONTROLLERS=4;
enum class RuntimeMatchHistoryPhase { Retired };
enum class RuntimeMatchHistoryFailure { None, Other };
struct Session { std::uint32_t frame{}; std::uint32_t source_frames() const { return frame; } };
struct Reader { bool is_active{}; bool active() const { return is_active; } };
struct Ticket { std::uint32_t ticket_id{}, source_frame{}, incarnation{}, local_port{}, remote_port{}; std::uint64_t poll_serial{}; };
struct Row { std::uint32_t ticket_id{}, source_frame{}, incarnation{}, local_port{}, remote_port{}, state{}; std::uint64_t poll_serial{}; };
struct NativeHistoryFixture {
  Session session_{}; Reader reader_{}; std::unique_ptr<int> checkpoint_;
  RuntimeMatchHistoryPhase phase_=RuntimeMatchHistoryPhase::Retired;
  RuntimeMatchHistoryFailure failure_=RuntimeMatchHistoryFailure::None;
  std::uint32_t sealed_end_{},pre_frame_{},forward_steps_{},ticket_count_{},incarnation_{},restore_count_{};
  std::uint32_t last_ticket_id_{},local_port_{},remote_port_{};
  std::uint64_t poll_serial_{}; bool episode_mode_{},arena_mode_{},current_ticket_pending_{},latest_ticket_valid_{};
  Ticket latest_ticket_{}; std::array<Row,7> episode_rows_{}; bool owner_live=true;
  bool live_owner(std::uint32_t frame) const { return owner_live&&session_.source_frames()==frame; }
  bool can_rebase_confirmed(std::uint32_t end_frame) const noexcept;
};
'''
        method = method.replace('RuntimeMatchHistory::can_rebase_confirmed',
                                'NativeHistoryFixture::can_rebase_confirmed')
        fixture += method + r'''
int main() {
  NativeHistoryFixture legacy{};
  legacy.pre_frame_=20; legacy.sealed_end_=21; legacy.session_.frame=21; legacy.forward_steps_=1;
  assert(legacy.can_rebase_confirmed(21));
  legacy.forward_steps_=2; assert(!legacy.can_rebase_confirmed(21));
  legacy.forward_steps_=1; legacy.arena_mode_=true; assert(!legacy.can_rebase_confirmed(21));

  NativeHistoryFixture arena{};
  arena.episode_mode_=arena.arena_mode_=true; arena.pre_frame_=100; arena.sealed_end_=104;
  arena.session_.frame=104; arena.forward_steps_=arena.ticket_count_=4;
  arena.incarnation_=3; arena.restore_count_=2; arena.local_port_=0; arena.remote_port_=1;
  arena.last_ticket_id_=24; arena.poll_serial_=904; arena.latest_ticket_valid_=true;
  arena.latest_ticket_={24,103,1,0,1,904};
  for(std::uint32_t i=0;i<4;++i)
    arena.episode_rows_[i]={21+i,100+i,1+i%3,0,1,(i==2?2u:1u),901+i};
  assert(arena.can_rebase_confirmed(104));
  arena.reader_.is_active=true; assert(!arena.can_rebase_confirmed(104));
  arena.reader_.is_active=false; arena.checkpoint_=std::make_unique<int>(1);
  assert(!arena.can_rebase_confirmed(104)); arena.checkpoint_.reset();
  arena.failure_=RuntimeMatchHistoryFailure::Other; assert(!arena.can_rebase_confirmed(104));
  arena.failure_=RuntimeMatchHistoryFailure::None;
  assert(!arena.can_rebase_confirmed(103));
  arena.restore_count_=3; assert(!arena.can_rebase_confirmed(104));
  arena.restore_count_=2; arena.current_ticket_pending_=true; assert(!arena.can_rebase_confirmed(104));
  arena.current_ticket_pending_=false; arena.owner_live=false; assert(!arena.can_rebase_confirmed(104));
  arena.owner_live=true; arena.episode_rows_[2].source_frame=103; assert(!arena.can_rebase_confirmed(104));
  arena.episode_rows_[2].source_frame=102; arena.ticket_count_=8; assert(!arena.can_rebase_confirmed(104));
  arena.ticket_count_=4; arena.incarnation_=4; assert(!arena.can_rebase_confirmed(104));
  arena.incarnation_=3; arena.arena_mode_=false; assert(!arena.can_rebase_confirmed(104));

  for(const std::uint32_t span : {1u,7u}) {
    NativeHistoryFixture bounded{};
    bounded.episode_mode_=bounded.arena_mode_=true; bounded.pre_frame_=200;
    bounded.sealed_end_=bounded.session_.frame=200+span;
    bounded.forward_steps_=bounded.ticket_count_=span; bounded.incarnation_=1;
    bounded.local_port_=0; bounded.remote_port_=1;
    for(std::uint32_t i=0;i<span;++i)
      bounded.episode_rows_[i]={40+i,200+i,1,0,1,1,1000+i};
    const auto& last=bounded.episode_rows_[span-1];
    bounded.last_ticket_id_=last.ticket_id; bounded.poll_serial_=last.poll_serial;
    bounded.latest_ticket_valid_=true;
    bounded.latest_ticket_={last.ticket_id,last.source_frame,last.incarnation,
                            last.local_port,last.remote_port,last.poll_serial};
    assert(bounded.can_rebase_confirmed(200+span));
  }
}
'''
        source = self.work / 'native-rebase-guard.cpp'
        binary = self.work / 'native-rebase-guard'
        source.write_text(fixture)
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A C++20 compiler is required')
        result = subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                                 str(source), '-o', str(binary)], cwd=ROOT,
                                capture_output=True, text=True, timeout=30)
        output = result.stdout + result.stderr
        (self.work / 'native-rebase-guard-compile.log').write_text(output)
        self.assertEqual(result.returncode, 0, output)
        result = subprocess.run([str(binary)], cwd=ROOT, capture_output=True,
                                text=True, timeout=30)
        output = result.stdout + result.stderr
        (self.work / 'native-rebase-guard.log').write_text(output)
        self.assertEqual(result.returncode, 0, output)

    def test_native_recording_present_uses_actual_callback_once_per_source_frame(self):
        source_text = (ROOT / 'src/slippi_profile/runtime_match_history.cpp').read_text()
        begin = source_text.index('bool RuntimeMatchHistory::submit_present() noexcept')
        end = source_text.index('\nbool RuntimeMatchHistory::endpoint_role_allowed(', begin)
        methods = source_text[begin:end]
        methods = methods.replace('RuntimeMatchHistory::', 'NativeHistoryFixture::')
        fixture = r'''#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
enum class RuntimeMatchHistoryPhase { PredictedCpuCopied, PredictedDrawPending,
  PredictedDrawIdle, CorrectedCpuCopied, CorrectedDrawPending, CorrectedDrawIdle,
  EpisodeRecording, EpisodePresentPending };
enum class RuntimeMatchHistoryFailure { None, Boundary, Phase, Present };
enum class RuntimeMatchHistoryGatePoint { PresentSubmit, PresentComplete };
enum class RuntimeMatchHistoryPresentResult { Submitted, Refused };
struct Session { std::uint32_t frame{}; std::uint32_t source_frames() const { return frame; } };
struct Ticket { std::uint32_t ticket_id{}, source_frame{}, incarnation{}; std::uint64_t poll_serial{}; };
struct Row { std::uint32_t ticket_id{}, source_frame{}, incarnation{}, state{}; std::uint64_t poll_serial{}; };
using Present = RuntimeMatchHistoryPresentResult (*)(Session&,std::uint32_t,void*) noexcept;
unsigned presents=0, gates=0;
RuntimeMatchHistoryPresentResult present(Session& session,std::uint32_t expected,void*) noexcept {
  ++presents; return session.source_frames()==expected ? RuntimeMatchHistoryPresentResult::Submitted
                                                        : RuntimeMatchHistoryPresentResult::Refused;
}
struct NativeHistoryFixture {
  Session session_{}; std::unique_ptr<int> checkpoint_;
  RuntimeMatchHistoryPhase phase_=RuntimeMatchHistoryPhase::EpisodeRecording;
  RuntimeMatchHistoryFailure failure_=RuntimeMatchHistoryFailure::None;
  Present present_=&present; void* callback_context_{};
  std::uint32_t sealed_end_{},pre_frame_{},forward_steps_{},ticket_count_{},incarnation_{};
  std::uint32_t recording_present_count_{},last_present_source_frame_{},last_ticket_id_{};
  std::uint64_t poll_serial_{};
  bool episode_mode_=true,arena_mode_=true,current_ticket_pending_{},latest_ticket_valid_=true;
  bool last_present_source_frame_valid_{};
  Ticket latest_ticket_{}; std::array<Row,7> episode_rows_{};
  bool live_owner(std::uint32_t frame) const { return session_.source_frames()==frame; }
  bool gate(RuntimeMatchHistoryGatePoint,std::uint32_t frame) { ++gates; return live_owner(frame); }
  bool fail(RuntimeMatchHistoryFailure failure,const char* = nullptr) { failure_=failure; return false; }
  bool submit_present() noexcept;
  bool submit_recording_present() noexcept;
  bool admit_present_completion() noexcept;
};
'''
        fixture += methods + r'''
static NativeHistoryFixture episode_fixture() {
  NativeHistoryFixture h{};
  h.session_.frame=101; h.pre_frame_=100; h.forward_steps_=h.ticket_count_=1;
  h.incarnation_=1; h.last_ticket_id_=77; h.poll_serial_=9001;
  h.latest_ticket_={77,100,1,9001}; h.episode_rows_[0]={77,100,1,1,9001};
  h.checkpoint_=std::make_unique<int>(1);
  return h;
}
int main() {
  presents=gates=0;
  auto recording=episode_fixture();
  assert(recording.submit_recording_present());
  assert(presents==1 && recording.recording_present_count_==1);
  assert(recording.phase_==RuntimeMatchHistoryPhase::EpisodePresentPending);
  assert(recording.last_present_source_frame_valid_&&recording.last_present_source_frame_==101);
  assert(recording.admit_present_completion());
  assert(recording.phase_==RuntimeMatchHistoryPhase::EpisodeRecording && presents==1);
  assert(!recording.submit_recording_present());
  assert(recording.failure_==RuntimeMatchHistoryFailure::Phase && presents==1);

  presents=gates=0;
  auto sealed=episode_fixture();
  sealed.phase_=RuntimeMatchHistoryPhase::PredictedCpuCopied;
  sealed.sealed_end_=101;
  assert(sealed.submit_present());
  assert(presents==1 && sealed.last_present_source_frame_valid_&&sealed.last_present_source_frame_==101);
  /* A sealed endpoint draw followed by continuation cannot draw that frame again. */
  sealed.failure_=RuntimeMatchHistoryFailure::None;
  sealed.phase_=RuntimeMatchHistoryPhase::EpisodeRecording;
  assert(!sealed.submit_recording_present());
  assert(sealed.failure_==RuntimeMatchHistoryFailure::Phase && presents==1);

  presents=0;
  auto wrong_mode=episode_fixture();
  wrong_mode.arena_mode_=false;
  assert(!wrong_mode.submit_recording_present() && presents==0);
  auto wrong_row=episode_fixture();
  wrong_row.episode_rows_[0].state=2;
  assert(!wrong_row.submit_recording_present() && presents==0);
}
'''
        source = self.work / 'native-recording-present.cpp'
        binary = self.work / 'native-recording-present'
        source.write_text(fixture)
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A C++20 compiler is required')
        for name, command in [
            ('compile', [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                         str(source), '-o', str(binary)]),
            ('recording-present-control', [str(binary)]),
        ]:
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=30)
            output = result.stdout + result.stderr
            (self.work / f'{name}.log').write_text(output)
            self.assertEqual(result.returncode, 0, output)

    def test_extracted_native_start_with_controlled_owners(self):
        browser = (ROOT / 'src/gameplay_menu_browser.cpp').read_text()
        history = (ROOT / 'src/slippi_profile/runtime_match_history_menu.inc').read_text()
        self.assertNotIn('history_start_publish_initial_source', browser)
        self.assertIn('history_start_before_tick(sample,input->samples);', browser)
        self.assertIn('if(agreed_prefix_tick&&history_start_after_tick())break;', browser)
        self.assertIn('if(history_start_prefix_running())history_start_after_draw(drew_source);', browser)
        self.assertIn('if(history_start_ordinary_callback())return;', browser)
        self.assertIn('history_start_finish_asset_match();', browser)

        def section(start, end):
            begin = history.index(start)
            return history[begin:history.index(end, begin)]

        takeover = section('int melee_web_native_menu_match_history_take_over(',
                           'int melee_web_native_menu_match_history_configure(')
        self.assertIn('first_use_draw_pending||', takeover)
        self.assertIn('c.start.state!=MatchHistoryStartDisabled&&!agreed_ready', takeover)
        fixture = (ROOT / 'tests/runtime_match_history_start_test.cpp.in').read_text()
        fixture = fixture.replace('@START_TYPES@', section(
            'enum MatchHistoryStartState', 'struct MatchHistoryControl'))
        fixture = fixture.replace('@START_FUNCTIONS@', '\n'.join([
            section('bool history_refuse(', 'void history_write_selection('),
            section('std::string_view history_start_string_argument(',
                    'const char* history_gpu_idle_failure() noexcept {'),
            section('void history_encode_pad(uint8_t* to,', 'void history_copy_input()'),
            section('void match_history_before_close() noexcept {',
                    'void match_history_after_match_close()'),
            section('int melee_web_native_menu_match_history_start_enable()',
                    'int melee_web_native_menu_match_history_take_over('),
        ]))
        source = self.work / 'start.cpp'
        binary = self.work / 'start'
        source.write_text(fixture)
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A C++20 compiler is required')
        for name, command in [
            ('compile', [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                         '-I', str(ROOT / 'src'), str(source), '-o', str(binary)]),
            ('start-controls', [str(binary)]),
        ]:
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=30)
            output = result.stdout + result.stderr
            (self.work / f'{name}.log').write_text(output)
            self.assertEqual(result.returncode, 0, output)

    def test_terminal_renderer_preparation_flushes_only_owned_pending_cache(self):
        history = (ROOT / 'src/slippi_profile/runtime_match_history_menu.inc').read_text()
        def section(start, end):
            i = history.index(start)
            return history[i:history.index(end, i)]
        fixture = r"""
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <aurora/aurora.h>
#include <aurora/gfx.h>
namespace melee_web {
enum class RuntimeMatchHistoryGateResult { Admitted, WrongFrame, RuntimeBusy };
}
using Gate=melee_web::RuntimeMatchHistoryGateResult;
struct GameplayMatchSession {} session;
struct Capsule {
  uint32_t current_frame() const { return 4; }
  bool endpoint_active() const { return false; }
} capsule;
struct {
  struct {unsigned owner_state=1,pending_command=0,last_completed_command=4;
          struct {uint32_t current_frame=4;} history;} status;
  Capsule* capsule=&::capsule;
  uint64_t submitted_frame=7,cache_flush_frame=0;
  bool dispatching=false;
  std::array<char,2048> renderer_preparation{};
} match_history;
GameplayMatchSession* match=&session;
bool match_history_terminal=true,match_history_in_tick=false,runtime_idle=true;
unsigned match_history_renderer_errors=0,flushes=0,observations=0,refusals=0;
Gate owner_gate=Gate::Admitted;
AuroraBrowserQuiescenceLease lease{};
AuroraPipelineCacheStatus cache=AURORA_PIPELINE_CACHE_PENDING;
bool strict_failure_after_flush=false;
bool history_runtime_idle(){return runtime_idle;}
Gate history_source_boundary(const GameplayMatchSession&,uint32_t frame){
  return frame==4?owner_gate:Gate::WrongFrame;
}
void history_refuse(const char*){++refusals;}
int aurora_browser_quiescence_capture_lease(AuroraBrowserQuiescenceLease* out){
  ++observations;*out=lease;return cache==AURORA_PIPELINE_CACHE_READY;
}
AuroraPipelineCacheStatus aurora_pipeline_cache_status(){return cache;}
const char* history_gpu_idle_failure(){
  return cache!=AURORA_PIPELINE_CACHE_READY||strict_failure_after_flush?"not quiescent":nullptr;
}
bool aurora_flush_pipeline_cache(){
  ++flushes;cache=AURORA_PIPELINE_CACHE_READY;lease.pipelineCacheStatus=cache;return true;
}
@HELPERS@
@EXPORT@
static const char* prepare(unsigned command=5){
  const auto* result=melee_web_native_menu_match_history_renderer_prepare_json(command);
  assert(result);
  return result;
}
int main(){
  lease.stagingGeneration=1;lease.currentFrameIndex=2;
  lease.lastBeginFrameId=lease.lastEndFrameId=7;
  lease.pipelineCacheStatus=cache;
  assert(!melee_web_native_menu_match_history_renderer_prepare_json(4));
  assert(!flushes&&!observations);
  runtime_idle=false;
  assert(std::strstr(prepare(),"\"observed\":0"));
  assert(!flushes&&!observations);
  runtime_idle=true;
  owner_gate=Gate::WrongFrame;
  assert(std::strstr(prepare(),"\"action\":0"));
  owner_gate=Gate::Admitted;
  for(auto field:{&lease.pendingFramePackets,&lease.pendingStagingBuffers,&lease.workerBusy,
                  &lease.queuedPipelines}){
    *field=1;
    assert(std::strstr(prepare(),"\"action\":0"));
    *field=0;
  }
  lease.lastEndFrameId=6;
  assert(std::strstr(prepare(),"\"action\":0"));
  lease.lastEndFrameId=7;
  lease.staleCallbackCount=1;
  assert(std::strstr(prepare(),"\"action\":0"));
  lease.staleCallbackCount=0;
  assert(!flushes&&!refusals);
  const auto* receipt=prepare();
  assert(flushes==1&&std::strstr(receipt,"\"action\":1"));
  assert(std::strstr(receipt,"\"before\":{\"cacheStatus\":1"));
  assert(std::strstr(receipt,"\"after\":{\"cacheStatus\":0"));
  cache=AURORA_PIPELINE_CACHE_PENDING;lease.pipelineCacheStatus=cache;
  assert(std::strstr(prepare(10),"\"action\":0")&&flushes==1);
  match_history.submitted_frame=++lease.lastEndFrameId;
  lease.lastBeginFrameId=lease.lastEndFrameId;
  strict_failure_after_flush=true;
  assert(std::strstr(prepare(10),"\"action\":2"));
  assert(flushes==2&&refusals==1);
}
"""
        fixture = fixture.replace('@HELPERS@', section('struct MatchHistoryRendererSample {',
                                                       'bool history_audio_step('))
        fixture = fixture.replace('@EXPORT@', section(
            'const char* melee_web_native_menu_match_history_renderer_prepare_json(',
            'const MatchHistoryStatus* melee_web_native_menu_match_history_status('))
        source = self.work / 'renderer-preparation.cpp'
        binary = self.work / 'renderer-preparation'
        source.write_text(fixture)
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A C++20 compiler is required')
        for name, command in [('compile', [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                '-D__EMSCRIPTEN__=1',
                '-I', str(ROOT / '.deps/aurora/include'), str(source), '-o', str(binary)]),
                ('run', [str(binary)])]:
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=30)
            output = result.stdout + result.stderr
            (self.work / f'renderer-preparation-{name}.log').write_text(output)
            self.assertEqual(result.returncode, 0, output)


if __name__ == '__main__':
    unittest.main()
