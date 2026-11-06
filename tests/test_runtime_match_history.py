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
                    'const char* melee_web_native_menu_match_history_rebase_identity_json('),
        ])
        self.assertIn('(void)emscripten_request_animation_frame(history_dispatch,nullptr);', request)
        self.assertNotIn('EMSCRIPTEN_RESULT_SUCCESS', request)

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


if __name__ == '__main__':
    unittest.main()
