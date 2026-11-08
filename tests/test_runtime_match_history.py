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


def native_enum(name, header='slippi_profile/runtime_match_history.hpp'):
    source = (ROOT / 'src' / header).read_text()
    start = source.index(f'enum class {name} ')
    return source[start:source.index('};', start) + 2]


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

    def test_actual_menu_boundary_scopes_owned_hps_to_interior_operations(self):
        menu = (ROOT / 'src/slippi_profile/runtime_match_history_menu.inc').read_text()
        native = (ROOT / 'src/slippi_profile/runtime_match_history.cpp').read_text()
        header = (ROOT / 'src/slippi_profile/runtime_match_history.hpp').read_text()

        def section(text, start, end):
            begin = text.index(start)
            return text[begin:text.index(end, begin)]

        enums = '\n'.join([
            section(header, 'enum class RuntimeMatchHistoryGatePoint',
                    'enum class RuntimeMatchHistoryGateResult'),
            section(header, 'enum class RuntimeMatchHistoryGateResult',
                    'using RuntimeMatchHistoryAudioStep'),
            section(header, 'enum class RuntimeMatchHistoryPhase',
                    'enum class RuntimeMatchHistoryFailure'),
        ])
        source_idle = section(menu, 'const char* history_source_idle_failure(const GameplayMatchSession& session) noexcept {',
                              'bool history_source_idle(')
        interior = section(menu, 'bool history_interior_hps_point(',
                           'melee_web::RuntimeMatchHistoryGateResult history_source_boundary(')
        boundaries = section(menu,
            'melee_web::RuntimeMatchHistoryGateResult history_source_boundary(',
            'struct MatchHistoryRendererSample')
        capsule = section(native, 'bool RuntimeMatchHistory::interior_hps_pending(',
                          'bool RuntimeMatchHistory::can_admit_poll_ticket(')

        fixture = r'''#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
namespace melee_web {
@ENUMS@
}
using uint32_t=std::uint32_t;
constexpr int DVD_STATE_END=0,DVD_STATE_BUSY=1;
struct Selection {uint32_t seed=7;};
using GameplayMatchSession=struct Session;
struct Session {
 uint32_t frame=201;bool files=true,built=true,is_paused=false,is_ending=false,is_complete=false;
 Selection selection{};
 uint32_t source_frames()const{return frame;}
 bool diagnostic_source_files_quiescent()const{return files;}
 bool construction_complete()const{return built;}bool paused()const{return is_paused;}
 bool ending()const{return is_ending;}bool complete()const{return is_complete;}
 const Selection& start_selection()const{return selection;}
};
struct GameplayCpuCheckpoint {
 const Session* owner{};uint32_t pre=196,span=7;bool pending=true,token=true;
 bool interior_hps_pending(const Session& s)const noexcept {
  return pending&&token&&owner==&s&&s.source_frames()>pre&&s.source_frames()<pre+span;
 }
};
struct RuntimeMatchHistoryMetadata {};
class RuntimeMatchHistory {
public:
 explicit RuntimeMatchHistory(Session& s):session_(s){}
 bool interior_hps_pending(const Session&,uint32_t)const noexcept;
 // This fixture owns the live interior branch and no terminal receipt.
 bool terminal_selected_at(uint32_t)const noexcept{return false;}
 bool terminal_tail_authorized(const Session&,melee_web::RuntimeMatchHistoryGatePoint,
                               uint32_t)const noexcept{return false;}
 melee_web::RuntimeMatchHistoryPhase phase()const{return phase_;}
 uint32_t current_frame()const{return session_.source_frames();}
 bool live_owner(uint32_t f)const{return owner_live_&&session_.source_frames()==f;}
 Session& session_;GameplayCpuCheckpoint* checkpoint_{};
 bool episode_mode_=true,arena_mode_=true,owner_live_=true;
 melee_web::RuntimeMatchHistoryPhase phase_=melee_web::RuntimeMatchHistoryPhase::EpisodePresentPending;
};
@CAPSULE@
using Gate=melee_web::RuntimeMatchHistoryGatePoint;
using Result=melee_web::RuntimeMatchHistoryGateResult;
struct Status {
 uint32_t last_completed_command=19,request_seq=22,complete_seq=22;
 struct {uint32_t current_frame=201;} history;
};
struct MatchHistoryControl {
 Status status{};RuntimeMatchHistory* capsule{};bool episode_enabled=true,arena_enabled=true;
 Session* match_owner{};int* archive_owner{};int* host_owner{};uint64_t asset_owner=1;
 Selection selection{};uint64_t submitted_frame=500,presentation_serial=1;
 uint32_t presentation_command=19,presentation_request_sequence=22;
};
int archive_tag=0,host_tag=0;
struct Cache {int* get()const{return &archive_tag;}} archive_cache;
struct MatchHandle {
 Session* value{};explicit operator bool()const{return value!=nullptr;}
 Session* get()const{return value;}Session* operator->()const{return value;}
 Session& operator*()const{return *value;}
};
MatchHandle match{};int* host=&host_tag;uint64_t asset_generation=1;
bool match_history_terminal=true,running=false,pending=false,replay=false,
 menu_scene_rebuild_pending=false,first_use_draw_pending=false,faulted=false,net_active=false;
enum class AssetDestination {None,Other};AssetDestination asset_destination=AssetDestination::None;
struct Preparation {bool busy()const{return false;}} preparation;
bool file_drive=false,bank_busy=false,stream_loading=true,memory_healthy=true,
 runtime_ready=true,gpu_ready=true;
int dvd_status=DVD_STATE_BUSY;
bool melee_web_source_file_drive_busy(){return file_drive;}
bool melee_web_audio_bank_transport_busy(){return bank_busy;}
bool melee_web_audio_stream_source_loading(){return stream_loading;}
int DVDGetDriveStatus(){return dvd_status;}
bool melee_web_source_memory_healthy(){return memory_healthy;}
bool melee_web_net_active(){return net_active;}
bool history_runtime_idle(){return runtime_ready;}
bool history_gpu_idle() noexcept {return gpu_ready;}
struct AuroraBrowserQuiescenceDescriptor {uint64_t lastEndFrameId=500;};
AuroraBrowserQuiescenceDescriptor descriptor{};
const AuroraBrowserQuiescenceDescriptor* aurora_browser_quiescence_observe_descriptor(){return &descriptor;}
MatchHistoryControl match_history{};
@SOURCE_IDLE@
@INTERIOR@
@BOUNDARIES@
int main(){
 Session session{};RuntimeMatchHistory capsule{session};GameplayCpuCheckpoint proof{&session};
 capsule.checkpoint_=&proof;match.value=&session;match_history.capsule=&capsule;
 match_history.match_owner=&session;match_history.archive_owner=&archive_tag;
 match_history.host_owner=&host_tag;match_history.asset_owner=asset_generation;
 match_history.selection=session.selection;
 assert(capsule.interior_hps_pending(session,201));
 for(const auto point:{Gate::InteriorPoll,Gate::InteriorForwardBefore,Gate::InteriorForwardAfter,
      Gate::InteriorReplayBefore,Gate::InteriorReplayAfter,Gate::InteriorPresentSubmit,
      Gate::InteriorPresentComplete})
  assert(history_boundary(session,point,201,nullptr)==Result::Admitted);
 assert(history_renderer_prepare_hps_authorized(session,5,201));
 assert(history_boundary(session,Gate::InteriorPresentComplete,201,nullptr)==Result::Admitted);
 descriptor.lastEndFrameId=499;
 assert(history_boundary(session,Gate::InteriorPresentComplete,201,nullptr)==Result::RendererBusy);
 descriptor.lastEndFrameId=500;
 assert(history_boundary(session,Gate::ForwardAfter,201,nullptr)==Result::SourceFilesBusy);
 assert(history_boundary(session,Gate::EndpointComplete,201,nullptr)==Result::SourceFilesBusy);
 assert(history_boundary(session,Gate::InteriorPoll,200,nullptr)==Result::WrongFrame);
 Session foreign{};foreign.frame=201;
 assert(!capsule.interior_hps_pending(foreign,201));
 assert(history_boundary(foreign,Gate::InteriorPoll,201,nullptr)==Result::MatchOwnerChanged);
 assert(!history_renderer_prepare_hps_authorized(session,10,201));
 match_history.status.history.current_frame=200;
 assert(!history_renderer_prepare_hps_authorized(session,5,201));
 match_history.status.history.current_frame=201;
 ++match_history.status.complete_seq;
 assert(!history_renderer_prepare_hps_authorized(session,5,201));
 --match_history.status.complete_seq;
 match_history.status.last_completed_command=4;
 assert(!history_renderer_prepare_hps_authorized(session,5,201));
 match_history.status.last_completed_command=19;

 proof.token=false;assert(history_boundary(session,Gate::InteriorPoll,201,nullptr)==Result::SourceFilesBusy);
 proof.token=true;proof.pending=false;
 assert(history_boundary(session,Gate::InteriorReplayAfter,201,nullptr)==Result::SourceFilesBusy);
 proof.pending=true;capsule.owner_live_=false;
 assert(history_boundary(session,Gate::InteriorForwardAfter,201,nullptr)==Result::SourceFilesBusy);
 capsule.owner_live_=true;capsule.arena_mode_=false;
 assert(history_boundary(session,Gate::InteriorPoll,201,nullptr)==Result::SourceFilesBusy);
 capsule.arena_mode_=true;capsule.episode_mode_=false;
 assert(history_boundary(session,Gate::InteriorPoll,201,nullptr)==Result::SourceFilesBusy);
 capsule.episode_mode_=true;

 session.files=false;assert(history_boundary(session,Gate::InteriorPoll,201,nullptr)==Result::SourceFilesBusy);
 session.files=true;file_drive=true;assert(history_boundary(session,Gate::InteriorPoll,201,nullptr)==Result::SourceFilesBusy);
 file_drive=false;bank_busy=true;assert(history_boundary(session,Gate::InteriorPoll,201,nullptr)==Result::SourceFilesBusy);
 bank_busy=false;memory_healthy=false;
 assert(history_boundary(session,Gate::InteriorPoll,201,nullptr)==Result::SourceFilesBusy);
 memory_healthy=true;dvd_status=77;
 assert(history_boundary(session,Gate::InteriorPoll,201,nullptr)==Result::SourceFilesBusy);
 dvd_status=DVD_STATE_END;stream_loading=true;
 assert(history_boundary(session,Gate::InteriorPoll,201,nullptr)==Result::Admitted);
 dvd_status=DVD_STATE_BUSY;stream_loading=false;
 assert(history_boundary(session,Gate::InteriorPoll,201,nullptr)==Result::Admitted);
 runtime_ready=false;assert(history_boundary(session,Gate::InteriorPoll,201,nullptr)==Result::RuntimeBusy);
 runtime_ready=true;gpu_ready=false;
 assert(history_boundary(session,Gate::InteriorPoll,201,nullptr)==Result::RendererBusy);
}
'''
        source_text = (fixture.replace('@ENUMS@', enums)
            .replace('@CAPSULE@', capsule)
            .replace('@SOURCE_IDLE@', source_idle)
            .replace('@INTERIOR@', interior)
            .replace('@BOUNDARIES@', boundaries))
        source = self.work / 'owned-hps-boundary.cpp'
        binary = self.work / 'owned-hps-boundary'
        source.write_text(source_text)
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A C++20 compiler is required')
        for name, command in [
            ('compile-owned-hps-boundary', [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                                            str(source), '-o', str(binary)]),
            ('run-owned-hps-boundary', [str(binary)]),
        ]:
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=30)
            output = result.stdout + result.stderr
            (self.work / f'{name}.log').write_text(output)
            self.assertEqual(result.returncode, 0, output)

    def test_request_dispatch_treats_raf_return_as_opaque_handle(self):
        history = (ROOT / 'src/slippi_profile/runtime_match_history_menu.inc').read_text()

        def section(start, end):
            begin = history.index(start)
            return history[begin:history.index(end, begin)]

        refusal = section('bool history_refuse(', 'bool history_runtime_idle() noexcept;')
        request = '\n'.join([
            section('int history_request(', 'bool history_terminal_tail_final_receipt('),
            section('int melee_web_native_menu_match_history_request(',
                    'int melee_web_native_menu_match_history_request_terminal_tail_begin('),
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
  bool inline_dispatch=false;
  bool terminal_tail_handoff_ready=false;
  struct {bool active=false;} pacing_wait;
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

    def test_inline_dispatch_is_opt_in_and_runs_one_staged_request(self):
        history = (ROOT / 'src/slippi_profile/runtime_match_history_menu.inc').read_text()

        def section(start, end):
            begin = history.index(start)
            return history[begin:history.index(end, begin)]

        refusal = section('bool history_refuse(', 'bool history_runtime_idle() noexcept;')
        request = section('int history_request(', 'bool history_terminal_tail_final_receipt(')
        enable = section('int melee_web_native_menu_match_history_inline_dispatch_enable()',
                         'int melee_web_native_menu_match_history_request_episode_forward(')
        fixture = r'''#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <array>
using EM_BOOL=int;
constexpr EM_BOOL EM_FALSE=0;
struct Status { uint32_t owner_state=1,first_failure=0,pending_command=0,last_completed_command=0;
 uint32_t request_seq=0,complete_seq=0,expected_frame=50; };
struct Session { uint32_t frame=50; uint32_t source_frames() const { return frame; } };
struct Capsule { bool endpoint_active() const { return false; } };
struct Control { Status status{};bool dispatching=false,identity_ready=true,remote_ready=false,
 episode_enabled=false,arena_enabled=false,inline_dispatch=false,terminal_tail_handoff_ready=false;Capsule* capsule=nullptr;
 struct {bool active=false;} pacing_wait;
 int* statics=nullptr;uint32_t requested_rebase_end{};
 Session* match_owner=nullptr;std::array<char,512> error{}; };
Control match_history{};
std::unique_ptr<Session> match;
bool match_history_terminal=true,match_history_in_tick=false,boundary_admitted=true,runtime_idle=true;
unsigned rAF_calls=0,dispatch_calls=0;
using Callback=EM_BOOL(*)(double,void*);
Callback queued_callback=nullptr;void* queued_data=nullptr;
namespace melee_web {
enum class RuntimeMatchHistoryGatePoint { CapturePre };
enum class RuntimeMatchHistoryGateResult { Admitted, Refused };
}
melee_web::RuntimeMatchHistoryGateResult history_boundary(const Session& s,
 melee_web::RuntimeMatchHistoryGatePoint,uint32_t frame,void*) {
 return boundary_admitted&&s.source_frames()==frame?
  melee_web::RuntimeMatchHistoryGateResult::Admitted:melee_web::RuntimeMatchHistoryGateResult::Refused;
}
bool history_start_export_idle() { return runtime_idle&&!match_history_in_tick&&
  !match_history.dispatching&&!match_history.status.pending_command; }
EM_BOOL history_dispatch(double,void*);
int emscripten_request_animation_frame(Callback cb,void* data) {
 ++rAF_calls;queued_callback=cb;queued_data=data;return 0;
}
@REFUSAL@
EM_BOOL history_dispatch(double,void*) {
 ++dispatch_calls;auto& c=match_history;
 const uint32_t command=c.status.pending_command;
 if(!command||c.dispatching)return EM_FALSE;
 c.dispatching=true;c.status.last_completed_command=command;
 c.status.complete_seq=c.status.request_seq;c.status.pending_command=0;
 c.dispatching=false;return EM_FALSE;
}
@REQUEST@
@ENABLE@
static int configured_statics_storage=1;
static int* configured_statics=&configured_statics_storage;
void reset() {
 match_history=Control{};match=std::make_unique<Session>();
 match_history_terminal=true;match_history_in_tick=false;boundary_admitted=true;runtime_idle=true;
 rAF_calls=dispatch_calls=0;queued_callback=nullptr;queued_data=nullptr;
 match_history.status.expected_frame=match->source_frames();
 match_history.match_owner=match.get();match_history.statics=configured_statics;
}
int main() {
 reset();
 assert(!match_history.inline_dispatch);
 assert(history_request(4,0)==1);
 assert(rAF_calls==1&&dispatch_calls==0&&queued_callback==history_dispatch);
 assert(match_history.status.request_seq==1&&match_history.status.pending_command==4);
 queued_callback(1.0,queued_data);
 assert(rAF_calls==1&&dispatch_calls==1);
 assert(match_history.status.complete_seq==1&&match_history.status.pending_command==0);

 reset();
 assert(melee_web_native_menu_match_history_inline_dispatch_enable()==1);
 assert(match_history.inline_dispatch);
 assert(history_request(4,0)==1);
 assert(rAF_calls==0&&dispatch_calls==1);
 assert(match_history.status.request_seq==1&&match_history.status.last_completed_command==4);
 assert(match_history.status.complete_seq==1&&match_history.status.pending_command==0);
 assert(melee_web_native_menu_match_history_inline_dispatch_enable()==0);
 assert(match_history.inline_dispatch&&match_history.status.first_failure==1);

 reset();Capsule capsule{};match_history.capsule=&capsule;
 assert(melee_web_native_menu_match_history_inline_dispatch_enable()==0);
 assert(!match_history.inline_dispatch&&match_history.status.first_failure==1);
 assert(rAF_calls==0&&dispatch_calls==0);

 reset();match_history.status.pending_command=4;
 assert(melee_web_native_menu_match_history_inline_dispatch_enable()==0);
 assert(!match_history.inline_dispatch&&match_history.status.first_failure==1);
 assert(rAF_calls==0&&dispatch_calls==0);
}
'''
        source_text = (fixture.replace('@REFUSAL@', refusal)
                              .replace('@REQUEST@', request)
                              .replace('@ENABLE@', enable))
        source = self.work / 'inline-dispatch.cpp'
        binary = self.work / 'inline-dispatch'
        source.write_text(source_text)
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A C++20 compiler is required')
        for name, command in [
            ('compile', [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                         str(source), '-o', str(binary)]),
            ('inline-dispatch-controls', [str(binary)]),
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

    def test_prefix_poll_gate_stays_closed_until_suffix_then_opens_monotonically(self):
        source_text = (ROOT / 'src/slippi_profile/runtime_match_history.cpp').read_text()
        start = source_text.index('bool RuntimeMatchHistory::can_admit_poll_ticket(')
        end = source_text.index('bool RuntimeMatchHistory::admit_poll_ticket(', start)
        admission = source_text[start:end]

        fixture = r'''#include <array>
#include <cassert>
#include <cstdint>
constexpr unsigned PAD_MAX_CONTROLLERS=4;
struct Session {
  uint32_t frame=0;
  uint32_t source_frames() const { return frame; }
};
enum class RuntimeMatchHistoryPhase { Armed, EpisodeRecording };
struct Receipt { unsigned local_port=0,remote_port=0; };
struct PrefixHandoff {
  bool active_=true;
  uint32_t suffix_cursor_=0,suffix_count_=2,selected_end_frame_=42;
};
class RuntimeMatchHistory {
public:
  Session session_{};
  RuntimeMatchHistoryPhase phase_=RuntimeMatchHistoryPhase::Armed;
  bool prefix_handoff_imported_=true;
  PrefixHandoff* prefix_handoff_=nullptr;
  bool episode_mode_=true,checkpoint_=true,current_ticket_pending_=false;
  bool owner_ok_=true;
  uint32_t pre_frame_=40,last_ticket_id_=100,ticket_count_=0;
  std::array<Receipt,7> episode_rows_{};
  bool live_owner(uint32_t expected) const { return owner_ok_&&session_.frame==expected; }
  bool can_admit_poll_ticket(uint32_t,unsigned,unsigned) const noexcept;
};
@ADMISSION@
int main() {
  PrefixHandoff handoff{};
  RuntimeMatchHistory history{};
  history.prefix_handoff_=&handoff;
  history.episode_rows_[0]={0,1};

  // No original suffix receipt has been adopted yet.
  history.session_.frame=40;
  history.ticket_count_=0;
  assert(!history.can_admit_poll_ticket(101,0,1));

  // One of two suffix rows is still outstanding.
  handoff.suffix_cursor_=1;
  history.session_.frame=41;
  history.ticket_count_=1;
  assert(!history.can_admit_poll_ticket(101,0,1));

  // Even an exhausted cursor cannot poll until execution has returned to F.
  handoff.suffix_cursor_=handoff.suffix_count_;
  assert(!history.can_admit_poll_ticket(101,0,1));

  // F, F+1, and F+2 are admitted in sequence after each successful forward.
  history.session_.frame=42;
  history.ticket_count_=2;
  assert(history.can_admit_poll_ticket(101,0,1));
  history.session_.frame=43;
  history.ticket_count_=3;
  history.last_ticket_id_=101;
  assert(history.can_admit_poll_ticket(102,0,1));
  history.session_.frame=44;
  history.ticket_count_=4;
  history.last_ticket_id_=102;
  assert(history.can_admit_poll_ticket(103,0,1));
  assert(!history.can_admit_poll_ticket(102,0,1)); // stale id
  history.ticket_count_=3;
  assert(!history.can_admit_poll_ticket(103,0,1)); // source/counter mismatch
  history.ticket_count_=4;
  history.owner_ok_=false;
  assert(!history.can_admit_poll_ticket(103,0,1)); // current owner refused
}
'''.replace('@ADMISSION@', admission)

        source = self.work / 'prefix-poll-gate.cpp'
        binary = self.work / 'prefix-poll-gate'
        source.write_text(fixture)
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A C++20 compiler is required')
        command = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                   str(source), '-o', str(binary)]
        compile_result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                        text=True, timeout=30)
        (self.work / 'prefix-poll-gate-compile.log').write_text(
            compile_result.stdout + compile_result.stderr)
        self.assertEqual(compile_result.returncode, 0,
                         compile_result.stdout + compile_result.stderr)
        run_result = subprocess.run([str(binary)], cwd=ROOT, capture_output=True,
                                    text=True, timeout=30)
        (self.work / 'prefix-poll-gate-run.log').write_text(
            run_result.stdout + run_result.stderr)
        self.assertEqual(run_result.returncode, 0,
                         run_result.stdout + run_result.stderr)

    def test_native_recording_present_uses_actual_callback_once_per_source_frame(self):
        source_text = (ROOT / 'src/slippi_profile/runtime_match_history.cpp').read_text()
        begin = source_text.index('bool RuntimeMatchHistory::submit_present() noexcept')
        end = source_text.index('\nbool RuntimeMatchHistory::seal_presented_frontier()', begin)
        methods = source_text[begin:end]
        methods = methods.replace('RuntimeMatchHistory::', 'NativeHistoryFixture::')
        fixture = r'''#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
@RuntimeMatchHistoryPhase@
enum class RuntimeMatchHistoryFailure { None, Boundary, Phase, Present };
@RuntimeMatchHistoryGatePoint@
enum class RuntimeMatchHistoryPresentResult { Submitted, Refused };
enum { PAD_MAX_CONTROLLERS=4 };
struct Session { std::uint32_t frame{}; std::uint32_t source_frames() const { return frame; } };
struct Ticket { std::uint32_t ticket_id{}, source_frame{}, incarnation{}; std::uint64_t poll_serial{}; };
struct Row { std::uint32_t ticket_id{}, source_frame{}, incarnation{}, local_port{},remote_port{},state{};
  std::uint64_t poll_serial{};std::array<std::uint8_t,44> raw_pad{}; };
struct Cursor { bool selected_terminal_valid{};std::uint32_t selected_terminal_input_ordinal{},
  selected_terminal_source_frame_before{},selected_terminal_source_frame_after{},
  selected_terminal_ending_after{},selected_terminal_complete_after{},input_ordinal{},source_frame{};
  std::uint64_t selected_terminal_completed_tick{}; };
using Present = RuntimeMatchHistoryPresentResult (*)(Session&,std::uint32_t,void*) noexcept;
unsigned presents=0, gates=0;
RuntimeMatchHistoryPresentResult present(Session& session,std::uint32_t expected,void*) noexcept {
  ++presents; return session.source_frames()==expected ? RuntimeMatchHistoryPresentResult::Submitted
                                                        : RuntimeMatchHistoryPresentResult::Refused;
}
struct NativeHistoryFixture {
  Session session_{}; int checkpoint_=1;
  RuntimeMatchHistoryPhase phase_=RuntimeMatchHistoryPhase::EpisodeRecording;
  RuntimeMatchHistoryFailure failure_=RuntimeMatchHistoryFailure::None;
  Present present_=&present; void* callback_context_{};
  std::uint32_t sealed_end_{},pre_frame_{},forward_steps_{},ticket_count_{},incarnation_{};
  std::uint32_t recording_present_count_{},last_present_source_frame_{},last_ticket_id_{},
    terminal_selected_source_frame_{},terminal_selected_ordinal_{},local_port_{},remote_port_{};
  std::uint64_t poll_serial_{};
  std::uint64_t terminal_selected_tick_{},terminal_last_presented_tick_{};
  bool episode_mode_=true,arena_mode_=true,current_ticket_pending_{},latest_ticket_valid_=true;
  bool last_present_source_frame_valid_{},terminal_needs_present_{},terminal_checkpoint_sealed_{};
  bool terminal_tail_complete_after_{};
  std::uint64_t terminal_tail_completed_tick_{};
  std::uint32_t terminal_tail_last_presented_ordinal_{},terminal_tail_input_ordinal_{};
  bool read_terminal_tail_cursor(std::uint64_t,std::uint32_t,bool) const { return false; }
  Ticket latest_ticket_{};Cursor terminal_cursor_{};
  std::array<Row,7> episode_rows_{},episode_origin_rows_{};
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
  h.latest_ticket_={77,100,1,9001};
  h.episode_rows_[0].ticket_id=77;h.episode_rows_[0].source_frame=100;
  h.episode_rows_[0].incarnation=1;h.episode_rows_[0].state=1;
  h.episode_rows_[0].poll_serial=9001;
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

  presents=gates=0;
  auto earlier_terminal=episode_fixture();
  earlier_terminal.phase_=RuntimeMatchHistoryPhase::TerminalSealed;
  earlier_terminal.forward_steps_=earlier_terminal.ticket_count_=3;
  earlier_terminal.incarnation_=2;earlier_terminal.last_ticket_id_=80;
  earlier_terminal.poll_serial_=9003;earlier_terminal.latest_ticket_={80,102,2,9003};
  earlier_terminal.terminal_needs_present_=true;
  earlier_terminal.terminal_selected_source_frame_=101;
  earlier_terminal.terminal_selected_ordinal_=102;
  earlier_terminal.terminal_selected_tick_=202;
  earlier_terminal.local_port_=0;earlier_terminal.remote_port_=1;
  earlier_terminal.terminal_cursor_.selected_terminal_valid=true;
  earlier_terminal.terminal_cursor_.selected_terminal_input_ordinal=102;
  earlier_terminal.terminal_cursor_.selected_terminal_source_frame_before=101;
  earlier_terminal.terminal_cursor_.selected_terminal_source_frame_after=101;
  earlier_terminal.terminal_cursor_.selected_terminal_ending_after=1;
  earlier_terminal.terminal_cursor_.selected_terminal_complete_after=0;
  earlier_terminal.terminal_cursor_.input_ordinal=102;
  earlier_terminal.terminal_cursor_.source_frame=101;
  earlier_terminal.terminal_cursor_.selected_terminal_completed_tick=202;
  earlier_terminal.episode_rows_[1].ticket_id=78;
  earlier_terminal.episode_rows_[1].source_frame=101;
  earlier_terminal.episode_rows_[1].incarnation=2;
  earlier_terminal.episode_rows_[1].local_port=0;
  earlier_terminal.episode_rows_[1].remote_port=1;
  earlier_terminal.episode_rows_[1].state=2;
  earlier_terminal.episode_rows_[1].poll_serial=9002;
  earlier_terminal.episode_rows_[1].raw_pad[0]=0x18;
  earlier_terminal.episode_origin_rows_[1]=earlier_terminal.episode_rows_[1];
  earlier_terminal.episode_origin_rows_[1].incarnation=1;
  earlier_terminal.episode_origin_rows_[1].state=1;
  earlier_terminal.episode_rows_[2].ticket_id=80;
  earlier_terminal.episode_rows_[2].source_frame=102;
  earlier_terminal.episode_rows_[2].incarnation=2;
  earlier_terminal.episode_rows_[2].local_port=0;
  earlier_terminal.episode_rows_[2].remote_port=1;
  earlier_terminal.episode_rows_[2].state=1;
  earlier_terminal.episode_rows_[2].poll_serial=9003;
  const auto suffix_before=earlier_terminal.episode_rows_[2];
  assert(earlier_terminal.submit_recording_present());
  assert(presents==1&&earlier_terminal.phase_==RuntimeMatchHistoryPhase::TerminalPresentPending);
  assert(earlier_terminal.terminal_last_presented_tick_==202);
  assert(earlier_terminal.last_ticket_id_==80&&earlier_terminal.poll_serial_==9003);
  assert(earlier_terminal.latest_ticket_.ticket_id==80&&
         earlier_terminal.episode_rows_[2].ticket_id==suffix_before.ticket_id&&
         earlier_terminal.episode_rows_[2].poll_serial==suffix_before.poll_serial);

  presents=gates=0;
  auto changed_origin=earlier_terminal;
  changed_origin.phase_=RuntimeMatchHistoryPhase::TerminalSealed;
  changed_origin.terminal_needs_present_=true;
  changed_origin.terminal_last_presented_tick_=0;
  changed_origin.episode_origin_rows_[1].poll_serial++;
  assert(!changed_origin.submit_recording_present()&&presents==0);
  assert(changed_origin.failure_==RuntimeMatchHistoryFailure::Present);
}
'''
        fixture = fixture.replace('@RuntimeMatchHistoryPhase@', native_enum('RuntimeMatchHistoryPhase'))
        fixture = fixture.replace('@RuntimeMatchHistoryGatePoint@', native_enum('RuntimeMatchHistoryGatePoint'))
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

    def test_presented_frontier_is_a_distinct_post_draw_endpoint(self):
        source_text = (ROOT / 'src/slippi_profile/runtime_match_history.cpp').read_text()

        def method(start, end):
            begin = source_text.index(start)
            return source_text[begin:source_text.index(end, begin)].replace(
                'RuntimeMatchHistory::', 'NativeHistoryFixture::')

        methods = '\n'.join([
            method('bool RuntimeMatchHistory::seal_presented_frontier() noexcept',
                   '\nbool RuntimeMatchHistory::endpoint_role_allowed('),
            method('bool RuntimeMatchHistory::endpoint_role_allowed(',
                   '\nbool RuntimeMatchHistory::begin_endpoint_batch('),
            method('bool RuntimeMatchHistory::begin_endpoint_batch(',
                   '\nGameplayCpuEndpointRecord RuntimeMatchHistory::endpoint_record('),
            method('bool RuntimeMatchHistory::end_endpoint_batch() noexcept',
                   '\nbool RuntimeMatchHistory::close_selected_endpoint() noexcept'),
            method('bool RuntimeMatchHistory::close_selected_endpoint() noexcept',
                   '\nbool RuntimeMatchHistory::retire_confirmed() noexcept'),
            method('bool RuntimeMatchHistory::begin_continuation() noexcept',
                   '\nbool RuntimeMatchHistory::forward_from_poll('),
            method('bool RuntimeMatchHistory::restore_pre() noexcept',
                   '\nbool RuntimeMatchHistory::correct_from_retained('),
        ])
        seal_body = source_text[source_text.index(
            'bool RuntimeMatchHistory::seal_presented_frontier() noexcept'):
            source_text.index('\nbool RuntimeMatchHistory::endpoint_role_allowed(',
                              source_text.index('bool RuntimeMatchHistory::seal_presented_frontier() noexcept'))]
        self.assertNotIn('present_(', seal_body)
        self.assertNotIn('session_.tick', seal_body)
        self.assertNotIn('audio_step_', seal_body)

        fixture = r'''#include <array>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <memory>
@RuntimeMatchHistoryPhase@
enum class RuntimeMatchHistoryFailure { None, Boundary, Phase, Owner, Seal, Endpoint, Restore, Close, Present };
@RuntimeMatchHistoryGatePoint@
enum class View { Pre, CpuEnd, DrawEnd, TerminalEnd };
enum class Provenance { SharedPre, Reference, Corrected, Terminal };
enum class GameplayCpuCommitResult { Ok, Refused };
struct Session { std::uint32_t frame{}; std::uint32_t source_frames() const { return frame; } };
struct Row { std::uint32_t ticket_id{}, source_frame{}, incarnation{}, state{}; std::uint64_t poll_serial{}; };
struct Ticket { std::uint32_t ticket_id{}, source_frame{}, incarnation{}; std::uint64_t poll_serial{}; };
struct MockCheckpoint {
  bool seal_ok=true, close_ok=true, archive_ok=true, restore_ok=true, begin_ok=true;
  unsigned seal_calls{}, close_calls{}, archive_calls{}, restore_calls{}, begin_calls{};
  std::uint32_t seal_frame{}, restore_frame=100;
  bool seal_history_incarnation(Session& s,const char**) { ++seal_calls; seal_frame=s.frame; return seal_ok; }
  bool seal_history_terminal_incarnation(Session&,std::uint64_t,std::uint32_t,std::uint32_t,const char**) { return false; }
  bool close_history_endpoint(Session&,char*,std::size_t) { ++close_calls; return close_ok; }
  bool archive_history_endpoint(Session&) { ++archive_calls; return archive_ok; }
  GameplayCpuCommitResult restore_history_pre(Session& s,char*,std::size_t) {
    ++restore_calls; if(!restore_ok)return GameplayCpuCommitResult::Refused;
    s.frame=restore_frame; return GameplayCpuCommitResult::Ok;
  }
  bool begin_history_incarnation(Session&) { ++begin_calls; return begin_ok; }
};
struct GameplayCpuEndpointReader {
  bool active_{}; bool revalidate_ok=true; bool begin_ok=true;
  bool active() const { return active_; }
  bool begin_history(Session&,MockCheckpoint&,View,Provenance) {
    if(!begin_ok){return false;}
    active_=true;
    return true;
  }
  bool revalidate() const { return revalidate_ok; }
  bool complete_immediate() const { assert(false); return false; }
};
struct NativeHistoryFixture {
  using View=::View; using Provenance=::Provenance;
  enum EndpointBit : std::uint32_t { CapturedPre=1u<<0, ArmedPre=1u<<1,
    PredictedCpu=1u<<2, PredictedDraw=1u<<3, RestoredPre=1u<<4,
    CorrectedCpu=1u<<5, CorrectedDraw=1u<<6, PrefixCpu=1u<<7,
    PresentedFrontier=1u<<8, TerminalEnd=1u<<9 };
  enum class EndpointRole { None, CapturedPre, ArmedPre, PredictedCpu, PredictedDraw,
    RestoredPre, CorrectedCpu, CorrectedDraw, PrefixCpu, PresentedFrontier, TerminalEnd };
  enum class EndpointCompletionMode { CopiedRows, ImmediateAdmission };
  Session session_{}; std::unique_ptr<MockCheckpoint> checkpoint_=std::make_unique<MockCheckpoint>();
  GameplayCpuEndpointReader reader_{};
  RuntimeMatchHistoryPhase phase_=RuntimeMatchHistoryPhase::EpisodeRecording;
  RuntimeMatchHistoryFailure failure_=RuntimeMatchHistoryFailure::None;
  EndpointRole endpoint_role_=EndpointRole::None;
  std::array<Row,7> episode_rows_{}; Ticket latest_ticket_{};
  std::uint32_t pre_frame_{},sealed_end_{},incarnation_{},restore_count_{},endpoint_mask_{};
  std::uint32_t forward_steps_{},replay_steps_{},last_present_source_frame_{},ticket_count_{};
  std::uint32_t last_ticket_id_{},recording_present_count_{},local_port_{},remote_port_{};
  std::uint32_t segment_start_frame_{},replay_target_end_{};
  std::uint64_t poll_serial_{}; unsigned audio_phase_{},pre_audio_phase_{};
  bool episode_mode_=true,arena_mode_=true,current_ticket_pending_{},latest_ticket_valid_=true;
  bool last_present_source_frame_valid_{},endpoint_read_failure_{},gate_ok=true;
  bool source_step_possible_{},terminal_checkpoint_sealed_{},terminal_needs_present_{};
  std::uint32_t replay_target_ordinal_{},terminal_selected_source_frame_{},terminal_selected_ordinal_{};
  std::uint64_t terminal_selected_tick_{},terminal_last_presented_tick_{};
  struct {bool selected_terminal_valid{};std::uint32_t selected_terminal_source_frame_after{},
    selected_terminal_input_ordinal{};std::uint64_t selected_terminal_completed_tick{};} terminal_cursor_;
  bool rewind_terminal_cursor_pre() const { return true; }
  bool live_owner(std::uint32_t f) const { return session_.source_frames()==f; }
  bool gate(RuntimeMatchHistoryGatePoint,std::uint32_t f) { return gate_ok&&live_owner(f); }
  bool fail(RuntimeMatchHistoryFailure f,const char* = nullptr) { failure_=f; phase_=RuntimeMatchHistoryPhase::Failed; return false; }
  bool checkpoint_failure(RuntimeMatchHistoryFailure f,const char*) { return fail(f); }
  bool seal_presented_frontier() noexcept;
  bool endpoint_role_allowed(View,Provenance,EndpointRole&) const noexcept;
  bool begin_endpoint_batch(View,Provenance) noexcept;
  bool end_endpoint_batch() noexcept;
  bool admit_endpoint_batch(View,Provenance) noexcept;
  bool end_endpoint_batch_impl(EndpointCompletionMode) noexcept;
  bool close_selected_endpoint() noexcept;
  bool begin_continuation() noexcept;
  bool restore_pre() noexcept;
};
'''
        fixture += methods + r'''
static NativeHistoryFixture episode_fixture() {
  NativeHistoryFixture h{};
  h.pre_frame_=100; h.session_.frame=101; h.forward_steps_=h.ticket_count_=1;
  h.incarnation_=1; h.last_ticket_id_=77; h.poll_serial_=9001;
  h.last_present_source_frame_=101; h.last_present_source_frame_valid_=true;
  h.latest_ticket_={77,100,1,9001}; h.episode_rows_[0]={77,100,1,1,9001};
  return h;
}
static void seal_and_copy(NativeHistoryFixture& h) {
  assert(h.seal_presented_frontier());
  assert(h.phase_==RuntimeMatchHistoryPhase::PresentedSealed);
  assert(h.checkpoint_->seal_calls==1 && h.checkpoint_->seal_frame==101);
  assert(h.endpoint_mask_==0);
  assert(h.begin_endpoint_batch(View::DrawEnd,Provenance::Reference));
  assert(h.endpoint_role_==NativeHistoryFixture::EndpointRole::PresentedFrontier);
  assert(h.end_endpoint_batch());
  assert(h.phase_==RuntimeMatchHistoryPhase::PresentedComplete);
  assert(h.endpoint_mask_==NativeHistoryFixture::PresentedFrontier);
  assert((h.endpoint_mask_&(NativeHistoryFixture::PredictedCpu|NativeHistoryFixture::PredictedDraw))==0);
}
int main() {
  auto valid=episode_fixture();
  NativeHistoryFixture::EndpointRole role{};
  assert(valid.seal_presented_frontier());
  assert(!valid.endpoint_role_allowed(View::CpuEnd,Provenance::Reference,role));
  assert(!valid.endpoint_role_allowed(View::DrawEnd,Provenance::Corrected,role));
  assert(valid.endpoint_role_allowed(View::DrawEnd,Provenance::Reference,role));
  assert(role==NativeHistoryFixture::EndpointRole::PresentedFrontier);
  assert(valid.begin_endpoint_batch(View::DrawEnd,Provenance::Reference));
  valid.reader_.revalidate_ok=false;
  assert(!valid.end_endpoint_batch());
  assert(valid.endpoint_mask_==0); // drift does not mint a receipt

  auto stale=episode_fixture(); stale.last_present_source_frame_=100;
  assert(!stale.seal_presented_frontier() && stale.checkpoint_->seal_calls==0);
  auto wrong_row=episode_fixture(); wrong_row.episode_rows_[0].state=2;
  assert(!wrong_row.seal_presented_frontier() && wrong_row.checkpoint_->seal_calls==0);
  auto pending=episode_fixture(); pending.current_ticket_pending_=true;
  assert(!pending.seal_presented_frontier() && pending.checkpoint_->seal_calls==0);
  auto active_reader=episode_fixture(); active_reader.reader_.active_=true;
  assert(!active_reader.seal_presented_frontier() && active_reader.checkpoint_->seal_calls==0);
  auto wrong_mode=episode_fixture(); wrong_mode.arena_mode_=false;
  assert(!wrong_mode.seal_presented_frontier() && wrong_mode.checkpoint_->seal_calls==0);
  auto not_idle=episode_fixture(); not_idle.gate_ok=false;
  assert(!not_idle.seal_presented_frontier() && not_idle.checkpoint_->seal_calls==0);

  auto closed=episode_fixture(); seal_and_copy(closed);
  assert(closed.close_selected_endpoint());
  assert(closed.phase_==RuntimeMatchHistoryPhase::Closed && closed.checkpoint_->close_calls==1);
  assert(!closed.begin_continuation()); // confirmed frontier closes toward retire/rebase only
  assert(closed.checkpoint_->begin_calls==0);

  auto restored=episode_fixture(); seal_and_copy(restored);
  assert(restored.restore_pre());
  assert(restored.checkpoint_->archive_calls==1 && restored.checkpoint_->restore_calls==1);
  assert(restored.session_.frame==100 && restored.phase_==RuntimeMatchHistoryPhase::RestoredPrePending);
  assert(restored.endpoint_mask_==0); // corrected incarnation gets a fresh endpoint receipt
  assert(!restored.restore_pre());

  auto bad_mask=episode_fixture(); seal_and_copy(bad_mask);
  bad_mask.endpoint_mask_|=NativeHistoryFixture::PredictedCpu;
  assert(!bad_mask.close_selected_endpoint() && bad_mask.checkpoint_->close_calls==0);
  auto legacy=episode_fixture();
  legacy.phase_=RuntimeMatchHistoryPhase::PredictedComplete;
  legacy.sealed_end_=101;
  legacy.endpoint_mask_=NativeHistoryFixture::PredictedCpu|NativeHistoryFixture::PredictedDraw;
  assert(legacy.close_selected_endpoint());
  assert(legacy.phase_==RuntimeMatchHistoryPhase::Closed && legacy.checkpoint_->close_calls==1);
}
'''
        fixture = fixture.replace('@RuntimeMatchHistoryPhase@', native_enum('RuntimeMatchHistoryPhase'))
        fixture = fixture.replace('@RuntimeMatchHistoryGatePoint@', native_enum('RuntimeMatchHistoryGatePoint'))
        source = self.work / 'presented-frontier-native.cpp'
        binary = self.work / 'presented-frontier-native'
        source.write_text(fixture)
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A C++20 compiler is required')
        for name, command in [
            ('compile', [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                         str(source), '-o', str(binary)]),
            ('presented-frontier-control', [str(binary)]),
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
        self.assertIn('history_start_release_completed_match();\n  pending=true;', history)
        self.assertLess(browser.index('if(history_start_ordinary_callback())return;'),
                        browser.index('}else if(pending){', browser.index('if(history_start_ordinary_callback())return;')))
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
                    'bool history_pacing_metadata_equal('),
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
@RuntimeMatchHistoryGatePoint@
@RuntimeMatchHistoryPhase@
}
using Gate=melee_web::RuntimeMatchHistoryGateResult;
struct GameplayMatchSession {} session;
struct Capsule {
  uint32_t current_frame() const { return 4; }
  bool endpoint_active() const { return false; }
  bool terminal_selected_at(uint32_t) const { return false; }
  bool terminal_tail_authorized(const GameplayMatchSession&,melee_web::RuntimeMatchHistoryGatePoint,
                                uint32_t) const { return false; }
  bool interior_hps_pending(const GameplayMatchSession&,uint32_t) const { return false; }
  melee_web::RuntimeMatchHistoryPhase phase() const {
    return melee_web::RuntimeMatchHistoryPhase::EpisodePresentPending;
  }
} capsule;
struct {
  struct {unsigned owner_state=1,pending_command=0,last_completed_command=4,request_seq=0,complete_seq=0;
          struct {uint32_t current_frame=4;} history;} status;
  Capsule* capsule=&::capsule;
  GameplayMatchSession* match_owner=&session;
  uint64_t submitted_frame=7,cache_flush_frame=0,presentation_serial=0;
  uint32_t presentation_command=0,presentation_request_sequence=0;
  bool dispatching=false,episode_enabled=true,arena_enabled=true;
  struct {bool active=false;} pacing_wait;
  std::array<char,2048> renderer_preparation{};
} match_history;
struct MatchHandle {
  GameplayMatchSession* get() const { return &session; }
  explicit operator bool() const { return true; }
  GameplayMatchSession& operator*() const { return session; }
} match;
bool match_history_terminal=true,match_history_in_tick=false,runtime_idle=true;
unsigned match_history_renderer_errors=0,flushes=0,observations=0,refusals=0;
Gate owner_gate=Gate::Admitted;
AuroraBrowserQuiescenceLease lease{};
AuroraPipelineCacheStatus cache=AURORA_PIPELINE_CACHE_PENDING;
bool strict_failure_after_flush=false;
bool history_runtime_idle(){return runtime_idle;}
Gate history_source_boundary(const GameplayMatchSession&,uint32_t frame,bool=false,bool=false,bool=false){
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
        fixture = fixture.replace('@HELPERS@',
            section('bool history_interior_hps_point(',
                    'melee_web::RuntimeMatchHistoryGateResult history_source_boundary(') +
            section('struct MatchHistoryRendererSample {', 'bool history_audio_step('))
        fixture = fixture.replace('@EXPORT@', section(
            'const char* melee_web_native_menu_match_history_renderer_prepare_json(',
            'const MatchHistoryStatus* melee_web_native_menu_match_history_status('))
        fixture = fixture.replace('@RuntimeMatchHistoryGatePoint@', native_enum('RuntimeMatchHistoryGatePoint'))
        fixture = fixture.replace('@RuntimeMatchHistoryPhase@', native_enum('RuntimeMatchHistoryPhase'))
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

    def test_atomic_endpoint_admission_closes_same_stack_and_split_batch_revalidates(self):
        native = (ROOT / 'src/slippi_profile/runtime_match_history.cpp').read_text()
        endpoint = (ROOT / 'src/gameplay_cpu_endpoint.cpp').read_text()

        def native_method(start, end):
            begin = native.index(start)
            return native[begin:native.index(end, begin)].replace(
                'RuntimeMatchHistory::', 'NativeHistoryFixture::')

        runtime_methods = '\n'.join([
            native_method('bool RuntimeMatchHistory::endpoint_role_allowed(',
                          '\nGameplayCpuEndpointRecord RuntimeMatchHistory::endpoint_record('),
            native_method('bool RuntimeMatchHistory::end_endpoint_batch() noexcept',
                          '\nbool RuntimeMatchHistory::close_selected_endpoint() noexcept'),
        ])

        def endpoint_method(start, end):
            begin = endpoint.index(start)
            return endpoint[begin:endpoint.index(end, begin)]

        immediate_begin = endpoint.index(
            'bool GameplayCpuEndpointReader::complete_immediate() noexcept')
        immediate_end = endpoint.index('\n}', immediate_begin) + 2
        reader_methods = '\n'.join([
            endpoint_method('bool GameplayCpuEndpointReader::history_identity_matches() const noexcept',
                            '\nbool GameplayCpuEndpointReader::begin('),
            endpoint_method('bool GameplayCpuEndpointReader::begin_history(',
                            '\nbool GameplayCpuEndpointReader::particle_header('),
            endpoint_method('bool GameplayCpuEndpointReader::revalidate() noexcept',
                            '\nbool GameplayCpuEndpointReader::complete_immediate() noexcept'),
            endpoint[immediate_begin:immediate_end],
        ])
        fixture = r'''#include <array>
#include <cassert>
#include <cstdint>
namespace melee_web {
enum class GameplayCpuEndpointStatus : std::uint32_t { Invalid, Ok, WrongPhase, OwnerRefused, Incomplete };
using Status=GameplayCpuEndpointStatus;
@GameplayCpuEndpointView@
@GameplayCpuEndpointProvenance@
@RuntimeMatchHistoryPhase@
enum class RuntimeMatchHistoryFailure : std::uint32_t { None, Boundary, Phase, Endpoint };
@RuntimeMatchHistoryGatePoint@
struct GameplayMatchSession { std::uint32_t frame=100; std::uint32_t source_frames()const{return frame;} };
struct GameplayCpuCheckpoint {
 bool history_mode_=true;std::uint32_t phase_=4,history_end_frame_{},history_end_source_frame_{},history_restore_count_{};
 std::uint64_t history_incarnation_=1;struct {std::uint64_t frame=100;} owner_;
 struct {std::uint64_t effect_token=77;} commit_;
};
struct EndpointData {
 std::uint32_t source_frame{},history_phase{},history_pre_frame{},history_current_end{},history_current_source_frame{},history_restore_count{};
 std::uint64_t history_incarnation{},history_effect_token{},leaf_epoch{};
};
struct ReaderCalls {unsigned collects{},same_batches{};} calls;
std::uint64_t leaf_epoch=0;
class GameplayCpuEndpointReader {
public:
 using View=GameplayCpuEndpointView;using Provenance=GameplayCpuEndpointProvenance;
 using BatchData=EndpointData;
 bool active()const{return active_;}
 bool begin_history(const GameplayMatchSession&,const GameplayCpuCheckpoint&,View,Provenance)noexcept;
 bool revalidate()noexcept;
 bool complete_immediate()noexcept;
 bool history_identity_matches()const noexcept;
 bool collect(EndpointData&)const noexcept;
 bool same_batch(const EndpointData&,const EndpointData&)const noexcept;
 bool active_{};bool history_mode_{};GameplayCpuEndpointStatus status_=Status::Invalid;
 const GameplayMatchSession* session_{};const GameplayCpuCheckpoint* checkpoint_{};EndpointData data_{};
 View view_{};Provenance provenance_{};
};
bool GameplayCpuEndpointReader::collect(EndpointData& out)const noexcept{
 ++calls.collects;if(!session_||!checkpoint_)return false;
 out.source_frame=session_->source_frames();out.history_phase=checkpoint_->phase_;
 out.history_pre_frame=static_cast<std::uint32_t>(checkpoint_->owner_.frame);
 out.history_current_source_frame=checkpoint_->history_end_source_frame_;
 out.history_current_end=checkpoint_->history_end_frame_;out.history_restore_count=checkpoint_->history_restore_count_;
 out.history_incarnation=checkpoint_->history_incarnation_;out.history_effect_token=checkpoint_->commit_.effect_token;
 out.leaf_epoch=leaf_epoch;return true;
}
bool GameplayCpuEndpointReader::same_batch(const EndpointData& a,const EndpointData& b)const noexcept{
 ++calls.same_batches;
 return a.source_frame==b.source_frame&&a.history_phase==b.history_phase&&
  a.history_pre_frame==b.history_pre_frame&&a.history_current_end==b.history_current_end&&
  a.history_current_source_frame==b.history_current_source_frame&&
  a.history_restore_count==b.history_restore_count&&a.history_incarnation==b.history_incarnation&&
  a.history_effect_token==b.history_effect_token&&a.leaf_epoch==b.leaf_epoch;
}
@READER_METHODS@
class NativeHistoryFixture {
public:
 using View=GameplayCpuEndpointView;using Provenance=GameplayCpuEndpointProvenance;
 enum EndpointBit : std::uint32_t { CapturedPre=1u<<0,ArmedPre=1u<<1,PredictedCpu=1u<<2,
  PredictedDraw=1u<<3,RestoredPre=1u<<4,CorrectedCpu=1u<<5,CorrectedDraw=1u<<6,
  PrefixCpu=1u<<7,PresentedFrontier=1u<<8, TerminalEnd=1u<<9 };
 enum class EndpointRole : std::uint32_t { None,CapturedPre,ArmedPre,PredictedCpu,PredictedDraw,
  RestoredPre,CorrectedCpu,CorrectedDraw,PrefixCpu,PresentedFrontier,TerminalEnd };
 enum class EndpointCompletionMode : std::uint32_t { CopiedRows,ImmediateAdmission };
 GameplayMatchSession session_{};GameplayCpuCheckpoint checkpoint_storage_{};
 GameplayCpuCheckpoint* checkpoint_=&checkpoint_storage_;GameplayCpuEndpointReader reader_{};
 RuntimeMatchHistoryPhase phase_=RuntimeMatchHistoryPhase::Captured;
 RuntimeMatchHistoryFailure failure_=RuntimeMatchHistoryFailure::None;EndpointRole endpoint_role_=EndpointRole::None;
 std::uint32_t pre_frame_=100,sealed_end_=101,endpoint_mask_{};bool endpoint_read_failure_{},gate_ok=true;
 unsigned gates{},fail_gate{};
 bool live_owner(std::uint32_t frame)const{return session_.source_frames()==frame;}
 bool gate(RuntimeMatchHistoryGatePoint,std::uint32_t frame){++gates;return gate_ok&&gates!=fail_gate&&live_owner(frame);}
 bool fail(RuntimeMatchHistoryFailure f,const char* =nullptr){failure_=f;phase_=RuntimeMatchHistoryPhase::Failed;return false;}
 bool endpoint_role_allowed(View,Provenance,EndpointRole&)const noexcept;
 bool begin_endpoint_batch(View,Provenance)noexcept;
 bool end_endpoint_batch()noexcept;
 bool admit_endpoint_batch(View,Provenance)noexcept;
 bool end_endpoint_batch_impl(EndpointCompletionMode)noexcept;
};
@RUNTIME_METHODS@
}
int main(){
 using namespace melee_web;using View=GameplayCpuEndpointView;using Provenance=GameplayCpuEndpointProvenance;
 calls={};leaf_epoch=0;NativeHistoryFixture atomic{};
 assert(atomic.admit_endpoint_batch(View::Pre,Provenance::SharedPre));
 assert(calls.collects==1&&calls.same_batches==0);
 assert(atomic.endpoint_mask_==NativeHistoryFixture::CapturedPre);
 assert(atomic.phase_==RuntimeMatchHistoryPhase::CapturedPreCopied);
 assert(atomic.endpoint_role_==NativeHistoryFixture::EndpointRole::None&&atomic.gates==2);

 calls={};leaf_epoch=0;NativeHistoryFixture split{};
 assert(split.begin_endpoint_batch(View::Pre,Provenance::SharedPre));
 assert(calls.collects==1);++leaf_epoch;
 assert(!split.end_endpoint_batch());
 assert(calls.collects==2&&calls.same_batches==1);
 assert(split.endpoint_mask_==0&&split.phase_==RuntimeMatchHistoryPhase::Failed);

 calls={};leaf_epoch=0;NativeHistoryFixture rejected{};rejected.fail_gate=2;
 assert(!rejected.admit_endpoint_batch(View::Pre,Provenance::SharedPre));
 assert(calls.collects==1&&calls.same_batches==0);
 assert(rejected.endpoint_mask_==0&&rejected.phase_==RuntimeMatchHistoryPhase::Failed);

 calls={};NativeHistoryFixture bad_role{};
 assert(!bad_role.admit_endpoint_batch(View::DrawEnd,Provenance::Reference));
 assert(calls.collects==0&&calls.same_batches==0&&bad_role.gates==0);
 assert(bad_role.endpoint_mask_==0&&bad_role.failure_==RuntimeMatchHistoryFailure::Phase);

 NativeHistoryFixture stale{};
 assert(stale.reader_.begin_history(stale.session_,stale.checkpoint_storage_,View::Pre,Provenance::SharedPre));
 ++stale.checkpoint_storage_.phase_;
 assert(!stale.reader_.complete_immediate());
 assert(stale.reader_.status_==Status::OwnerRefused&&!stale.reader_.active_);
}
'''
        fixture = fixture.replace('@GameplayCpuEndpointView@', native_enum('GameplayCpuEndpointView', 'gameplay_cpu_endpoint.hpp'))
        fixture = fixture.replace('@GameplayCpuEndpointProvenance@', native_enum('GameplayCpuEndpointProvenance', 'gameplay_cpu_endpoint.hpp'))
        fixture = fixture.replace('@RuntimeMatchHistoryPhase@', native_enum('RuntimeMatchHistoryPhase'))
        fixture = fixture.replace('@RuntimeMatchHistoryGatePoint@', native_enum('RuntimeMatchHistoryGatePoint'))
        source = self.work / 'atomic-endpoint-admission.cpp'
        binary = self.work / 'atomic-endpoint-admission'
        source.write_text(fixture.replace('@READER_METHODS@', reader_methods)
                          .replace('@RUNTIME_METHODS@', runtime_methods))
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A C++20 compiler is required')
        for name, command in [
            ('compile-atomic-endpoint-admission', [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                                                   str(source), '-o', str(binary)]),
            ('run-atomic-endpoint-admission', [str(binary)]),
        ]:
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=30)
            output = result.stdout + result.stderr
            (self.work / f'{name}.log').write_text(output)
            self.assertEqual(result.returncode, 0, output)


    def test_terminal_finish_dispatch_keeps_suffix_or_captures_prefix_handoff(self):
        menu = (ROOT / 'src/slippi_profile/runtime_match_history_menu.inc').read_text()
        start = menu.index('bool history_finish_confirmed_prefix_dispatch(')
        end = menu.index('\nEM_BOOL history_dispatch(', start)
        helper = menu[start:end]
        helper = helper.replace('history_finish_confirmed_prefix_dispatch(',
                                'fixture_finish_confirmed_prefix_dispatch(', 1)
        fixture = r'''#include <cassert>
#include <cstdint>
namespace melee_web {
enum class RuntimeMatchHistoryPhase { PrefixSealed, TerminalSealed, EpisodeRecording };
}
struct Handoff { std::uint32_t marker{}; };
struct Status { std::uint32_t request_seq=71; };
struct Capsule {
 melee_web::RuntimeMatchHistoryPhase after=melee_web::RuntimeMatchHistoryPhase::EpisodeRecording;
 bool finish_ok=true,capture_ok=true;
 std::uint32_t finish_calls{},capture_calls{},captured_sequence{},captured_end{};
 bool finish_confirmed_prefix(std::uint32_t end) {
  ++finish_calls;captured_end=end;return finish_ok;
 }
 melee_web::RuntimeMatchHistoryPhase phase() const {return after;}
 bool capture_confirmed_prefix_handoff(std::uint32_t sequence,Handoff& handoff) {
  ++capture_calls;captured_sequence=sequence;handoff.marker=sequence;return capture_ok;
 }
};
struct MatchHistoryControl {
 bool episode_enabled=true,arena_enabled=true;
 Capsule* capsule{};std::uint32_t requested_rebase_end=203;
 Status status{};Handoff prefix_handoff{};
};
@HELPER@
int main(){
 using Phase=melee_web::RuntimeMatchHistoryPhase;
 Capsule prefix{};prefix.after=Phase::PrefixSealed;
 MatchHistoryControl prefix_control{};prefix_control.capsule=&prefix;
 assert(fixture_finish_confirmed_prefix_dispatch(prefix_control));
 assert(prefix.finish_calls==1&&prefix.capture_calls==1);
 assert(prefix.captured_end==203&&prefix.captured_sequence==71);
 assert(prefix_control.prefix_handoff.marker==71);

 Capsule terminal{};terminal.after=Phase::TerminalSealed;
 MatchHistoryControl terminal_control{};terminal_control.capsule=&terminal;
 terminal_control.prefix_handoff.marker=0xA5A5;
 assert(fixture_finish_confirmed_prefix_dispatch(terminal_control));
 assert(terminal.finish_calls==1&&terminal.capture_calls==0);
 assert(terminal_control.prefix_handoff.marker==0xA5A5);

 Capsule unexpected{};unexpected.after=Phase::EpisodeRecording;
 MatchHistoryControl unexpected_control{};unexpected_control.capsule=&unexpected;
 assert(!fixture_finish_confirmed_prefix_dispatch(unexpected_control));
 assert(unexpected.finish_calls==1&&unexpected.capture_calls==0);

 Capsule finish_refused{};finish_refused.finish_ok=false;
 finish_refused.after=Phase::TerminalSealed;
 MatchHistoryControl refused_control{};refused_control.capsule=&finish_refused;
 assert(!fixture_finish_confirmed_prefix_dispatch(refused_control));
 assert(finish_refused.finish_calls==1&&finish_refused.capture_calls==0);
}
'''.replace('@HELPER@', helper)
        source = self.work / 'terminal-finish-dispatch.cpp'
        binary = self.work / 'terminal-finish-dispatch'
        source.write_text(fixture)
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A C++20 compiler is required')
        for name, command in [
            ('compile-terminal-finish-dispatch', [compiler, '-std=c++20', '-Wall', '-Wextra',
              '-Werror', str(source), '-o', str(binary)]),
            ('run-terminal-finish-dispatch', [str(binary)]),
        ]:
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=30)
            output = result.stdout + result.stderr
            (self.work / f'{name}.log').write_text(output)
            self.assertEqual(result.returncode, 0, output)

    def test_terminal_receipt_export_retains_first_and_selected_states(self):
        menu = (ROOT / 'src/slippi_profile/runtime_match_history_menu.inc').read_text()
        helper_start = menu.index('const melee_web::RuntimeMatchHistoryTerminalReceipt*\nhistory_terminal_receipt_copy()')
        helper_end = menu.index('\n}\n\nextern "C" {', helper_start) + 2
        getter_start = menu.index('unsigned melee_web_native_menu_match_history_terminal_receipt_bytes()', helper_end)
        getter_end = menu.index('const char* melee_web_native_menu_match_history_terminal_tail_json()', getter_start)
        tail_end = menu.index('unsigned melee_web_native_menu_match_history_terminal_tail_ticket_bytes()', getter_end)
        exports = menu[helper_start:helper_end] + '\n' + menu[getter_start:tail_end]
        fixture = r'''#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
namespace melee_web {
@PHASE@
struct RuntimeMatchHistoryTerminalReceipt {
 std::uint32_t schema=1,bytes=104,flags{},incarnation{};
 std::uint32_t pre_source_frame{},selected_source_frame{},pre_ordinal{},selected_ordinal{};
 std::uint32_t first_ordinal{},first_source_frame{},ticket_id{},local_port{};
 std::uint32_t remote_port{},reserved{};
 std::uint64_t pre_completed_tick{},selected_completed_tick{},first_completed_tick{};
 std::uint64_t world_generation{},context_epoch{},poll_serial{};
};
static_assert(sizeof(RuntimeMatchHistoryTerminalReceipt)==104);
}
using Phase=melee_web::RuntimeMatchHistoryPhase;
enum { PAD_MAX_CONTROLLERS=4 };
struct HistoryStatus {
 unsigned owner_state=1,first_failure{},pending_command{},last_completed_command=20;
 unsigned request_seq=88,complete_seq=88;struct {unsigned current_frame=11;} history;
};
struct Capsule {
 unsigned current=11;unsigned source_writes{};
 Phase state=Phase::TerminalTailReady;
 Phase phase()const noexcept{return state;}
 unsigned terminal_tail_source_frame()const noexcept{return current;}
 unsigned terminal_tail_consumed_ordinal()const noexcept{return 13;}
 unsigned terminal_tail_completed_tick()const noexcept{return 103;}
 unsigned terminal_tail_suffix_count()const noexcept{return 0;}
 unsigned terminal_tail_suffix_cursor()const noexcept{return 0;}
 struct Cursor {struct {unsigned world_generation=7,context_epoch=9;} owner;};
 Cursor terminal_cursor()const noexcept{return {};}
 melee_web::RuntimeMatchHistoryTerminalReceipt receipt{};
 unsigned current_frame()const noexcept{return current;}
 melee_web::RuntimeMatchHistoryTerminalReceipt terminal_receipt()const noexcept{return receipt;}
};
struct MatchHistoryControl {
 HistoryStatus status{};Capsule* capsule{};bool dispatching{};void* match_owner{};
 bool episode_enabled=true,arena_enabled=true,terminal_tail_handoff_ready=false;
 unsigned presentation_command=29,presentation_request_sequence=87;
 std::array<char,2048> terminal_tail_json{};
 melee_web::RuntimeMatchHistoryTerminalReceipt terminal_receipt{};
};
struct MatchHandle {void* value{};explicit operator bool()const{return value!=nullptr;}void* get()const{return value;}
 MatchHandle* operator->(){return this;}const MatchHandle* operator->()const{return this;}
 unsigned source_frame=11;unsigned source_frames()const{return source_frame;}};
MatchHistoryControl match_history{};MatchHandle match{};bool match_history_terminal=true,match_history_in_tick=false;
bool runtime_idle=true;
bool history_start_export_idle()noexcept{return runtime_idle&&!match_history_in_tick&&
 !match_history.dispatching&&!match_history.status.pending_command;}
@EXPORTS@
static void expect_refused(){
 assert(melee_web_native_menu_match_history_terminal_receipt_bytes()==0);
 assert(melee_web_native_menu_match_history_terminal_receipt_data()==nullptr);
}
int main(){
 int owner=1;Capsule capsule{};
 match.value=&owner;match.source_frame=11;match_history.match_owner=&owner;
 match_history.capsule=&capsule;
 assert(melee_web_native_menu_match_history_terminal_receipt_bytes()==104);
 const auto* empty=melee_web_native_menu_match_history_terminal_receipt_data();
 assert(empty&&empty->flags==0&&empty->incarnation==0&&empty->first_ordinal==0);
 capsule.receipt.flags=1;capsule.receipt.incarnation=2;
 capsule.receipt.pre_source_frame=10;capsule.receipt.pre_ordinal=10;
 capsule.receipt.first_ordinal=13;capsule.receipt.first_source_frame=12;
 capsule.receipt.pre_completed_tick=100;capsule.receipt.first_completed_tick=103;
 capsule.receipt.world_generation=7;capsule.receipt.context_epoch=9;
 const auto* first=melee_web_native_menu_match_history_terminal_receipt_data();
 assert(first&&first->flags==1&&first->first_ordinal==13&&first->selected_ordinal==0);
 assert(first->pre_completed_tick==100&&first->first_completed_tick==103);
 assert(capsule.source_writes==0&&match_history.status.complete_seq==88);
 capsule.receipt.flags=3;capsule.receipt.selected_source_frame=11;
 capsule.receipt.selected_ordinal=12;capsule.receipt.selected_completed_tick=102;
 capsule.receipt.ticket_id=101;capsule.receipt.poll_serial=1001;
 capsule.receipt.local_port=0;capsule.receipt.remote_port=1;
 const auto* selected=melee_web_native_menu_match_history_terminal_receipt_data();
 assert(selected&&selected->flags==3&&selected->first_ordinal==13&&selected->selected_ordinal==12);
 assert(selected->selected_source_frame==11&&selected->ticket_id==101&&selected->poll_serial==1001);
 auto& c=match_history;
 const auto descriptor=melee_web_native_menu_match_history_terminal_tail_json;
 c.status.last_completed_command=5;
 assert(!descriptor());
 c.terminal_tail_handoff_ready=true;
 const auto* text=descriptor();
 assert(text&&std::strstr(text,"\"completedTick\":\"103\"")&&
        std::strstr(text,"\"sourceFrame\":11")&&
        std::strstr(text,"\"consumedOrdinal\":13"));
 capsule.state=Phase::TerminalTailPresented;assert(descriptor());
 capsule.state=Phase::TerminalTailPresentPending;assert(!descriptor());
 capsule.state=Phase::TerminalTailReady;
 c.presentation_request_sequence=86;assert(!descriptor());
 c.presentation_request_sequence=87;c.presentation_command=19;assert(!descriptor());
 c.presentation_command=29;
 c.status.last_completed_command=26;assert(descriptor());
 c.status.last_completed_command=27;assert(!descriptor());
 capsule.state=Phase::TerminalTailTicketed;assert(descriptor());
 c.status.last_completed_command=28;capsule.state=Phase::TerminalTailForwarded;assert(descriptor());
 c.status.last_completed_command=29;capsule.state=Phase::TerminalTailPresentPending;
 assert(!descriptor());c.presentation_request_sequence=88;assert(descriptor());
 c.status.last_completed_command=30;assert(!descriptor());
 c.status.last_completed_command=20;c.terminal_tail_handoff_ready=false;
 auto saved=match_history.status;
 match_history.status.pending_command=1;expect_refused();match_history.status=saved;
 match_history.dispatching=true;expect_refused();match_history.dispatching=false;
 match_history_in_tick=true;expect_refused();match_history_in_tick=false;
 runtime_idle=false;expect_refused();runtime_idle=true;
 match_history.status.complete_seq=87;expect_refused();match_history.status.complete_seq=88;
 match_history.status.first_failure=1;expect_refused();match_history.status.first_failure=0;
 match_history.status.owner_state=0;expect_refused();match_history.status.owner_state=1;
 void* other=&capsule;match_history.match_owner=other;expect_refused();match_history.match_owner=&owner;
 match.source_frame=12;expect_refused();match.source_frame=11;
 capsule.current=12;expect_refused();capsule.current=11;
 match_history.status.last_completed_command=26;expect_refused();match_history.status.last_completed_command=20;
 capsule.receipt.flags=2;expect_refused();capsule.receipt.flags=0;expect_refused();
 capsule.receipt.flags=4;expect_refused();
}
'''.replace('@EXPORTS@', exports).replace('@PHASE@', native_enum('RuntimeMatchHistoryPhase'))
        source = self.work / 'terminal-receipt-export.cpp'
        binary = self.work / 'terminal-receipt-export'
        source.write_text(fixture)
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A C++20 compiler is required')
        for name, command in [
            ('compile-terminal-receipt-export', [compiler, '-std=c++20', '-Wall', '-Wextra',
              '-Werror', str(source), '-o', str(binary)]),
            ('run-terminal-receipt-export', [str(binary)]),
        ]:
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=30)
            output = result.stdout + result.stderr
            (self.work / f'{name}.log').write_text(output)
            self.assertEqual(result.returncode, 0, output)



if __name__ == '__main__':
    unittest.main()
