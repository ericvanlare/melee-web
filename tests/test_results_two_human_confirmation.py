"""Actual original Results acknowledgement/readiness, with fake drawing APIs.
No assets, source world, browser, PCM or gameplay acceptance is exercised here.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from test_gameplay_sd_handoff_order import function
from owned_test_workspace import OwnedWorkspaceTests

class ResultsTwoHumanConfirmationTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.scratch = cls.new_workspace(ROOT, 'results-two-human-confirmation-')

    def test_original_per_port_readiness_and_confirmation(self):
        raw = (ROOT / '.deps/melee/src/melee/gm/gmresultplayer.c').read_text()
        prepared = (ROOT / 'build/gameplay-source/src/melee/gm/gmresultplayer.c').read_text()
        original = function(raw, 'void fn_80178050(')
        self.assertEqual(original, function(prepared, 'void fn_80178050('),
                         'This control requires the exact pinned original function')
        scratch = self.scratch
        source = scratch / 'control.cpp'
        source.write_text((ROOT / 'tests/results_two_human_confirmation_test.cpp')
                          .read_text().replace('// ORIGINAL_FUNCTION', original))
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        build = subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                                '-O1', str(source), '-o', str(scratch/'control')],
                               capture_output=True, text=True, timeout=30)
        self.assertEqual(build.returncode, 0, build.stdout+build.stderr)
        run = subprocess.run([str(scratch/'control')], capture_output=True,
                             text=True, timeout=5)
        print(run.stdout, end='')
        self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
        self.assertEqual(len(run.stdout.splitlines()), 5)
