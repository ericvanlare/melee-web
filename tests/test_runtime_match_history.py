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

    def test_extracted_native_start_with_controlled_owners(self):
        browser = (ROOT / 'src/gameplay_menu_browser.cpp').read_text()
        history = (ROOT / 'src/slippi_profile/runtime_match_history_menu.inc').read_text()
        self.assertIn('history_start_publish_initial_source(present_source);', browser)
        self.assertIn('if(history_start_ordinary_callback())return;', browser)
        self.assertIn('history_start_finish_asset_match();', browser)

        def section(start, end):
            begin = history.index(start)
            return history[begin:history.index(end, begin)]

        self.assertIn('first_use_draw_pending||', section(
            'int melee_web_native_menu_match_history_take_over(',
            'int melee_web_native_menu_match_history_configure('))
        fixture = (ROOT / 'tests/runtime_match_history_start_test.cpp.in').read_text()
        fixture = fixture.replace('@START_TYPES@', section(
            'enum MatchHistoryStartState', 'struct MatchHistoryControl'))
        fixture = fixture.replace('@START_FUNCTIONS@', '\n'.join([
            section('bool history_refuse(', 'void history_write_selection('),
            section('std::string_view history_start_string_argument(',
                    'bool history_gpu_idle() noexcept {'),
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
