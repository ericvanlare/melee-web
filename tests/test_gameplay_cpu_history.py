"""Actual bounded CPU-history controls; these are not gameplay acceptance."""
from pathlib import Path
import os
import subprocess
import sys
import unittest

ROOT = Path(os.environ.get(
    'MELEE_CPU_CHECKPOINT_REPO_ROOT', Path(__file__).resolve().parents[1]
)).resolve()
SOURCE = Path(os.environ.get(
    'MELEE_CPU_CHECKPOINT_CANDIDATE_ROOT', ROOT
)).resolve()
sys.path.insert(0, str(ROOT / 'tests'))
sys.path.insert(0, str(ROOT / 'scripts'))
from owned_test_workspace import OwnedWorkspaceTests
from check_gameplay import node_runtime


class GameplayCpuHistoryControls(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.emcc = ROOT / '.deps/emsdk/upstream/emscripten/emcc'
        cls.emxx = ROOT / '.deps/emsdk/upstream/emscripten/em++'
        if not cls.emcc.is_file() or not cls.emxx.is_file():
            raise unittest.SkipTest('Pinned Emscripten toolchain unavailable')
        cls.work = cls.new_workspace(ROOT, 'gameplay-cpu-history-')

    def compile_run(self, name, sources, *, cpp):
        directory = self.work / name
        directory.mkdir()
        temporary = directory / 'tmp'
        temporary.mkdir()
        binary = directory / 'control.js'
        command = [
            str(self.emxx if cpp else self.emcc),
            '-std=c++20' if cpp else '-std=gnu11', '-O1', '-UNDEBUG', '-DTARGET_PC',
            '-DMELEE_WEB_CPU_INTERVAL_ENABLED=1', '-ffp-contract=off',
            '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
            '-I' + str(SOURCE / 'src'), '-I' + str(ROOT / 'src'),
            '-I' + str(ROOT / 'build/gameplay-source/src'),
            '-I' + str(ROOT / 'build/gameplay-source/src/melee/gm'),
            '-I' + str(ROOT / 'build/gameplay-source/src/melee/lb'),
            '-I' + str(ROOT / '.deps/melee/src'),
            '-I' + str(ROOT / '.deps/aurora/include'),
            '-I' + str(ROOT / '.deps/melee/extern/dolphin/include'),
        ]
        if cpp:
            command.extend(['-fexceptions', '-sDISABLE_EXCEPTION_CATCHING=0'])
        command.extend(map(str, sources))
        command.extend(['-sENVIRONMENT=node', '-sEXIT_RUNTIME=1', '-sASSERTIONS=2',
                        '-o', str(binary)])
        env = dict(os.environ, TMPDIR=str(temporary))
        compiled = subprocess.run(command, cwd=ROOT, env=env,
                                  capture_output=True, text=True, timeout=120)
        (directory / 'compile.log').write_text(compiled.stdout + compiled.stderr)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        run = subprocess.run([str(node_runtime()), str(binary)], cwd=ROOT, env=env,
                             capture_output=True, text=True, timeout=30)
        (directory / 'run.log').write_text(run.stdout + run.stderr)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        return run.stdout

    def test_actual_motor_history_two_restores_and_bounds(self):
        output = self.compile_run(
            'motor-history',
            [SOURCE / 'src/gameplay_cpu_interval.c',
             SOURCE / 'tests/cpu_interval_history_control.c'],
            cpp=False,
        )
        self.assertIn('passed two same-PRE restores', output)

    def test_actual_statics_history_two_restores_and_legacy(self):
        output = self.compile_run(
            'statics-history',
            [SOURCE / 'src/gameplay_cpu_statics.cpp',
             SOURCE / 'tests/cpu_statics_history_control.cpp'],
            cpp=True,
        )
        self.assertIn('permits two bounded restores', output)
        self.assertIn('preserves legacy single-use mode', output)
