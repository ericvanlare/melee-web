"""The actual source MSL kernel must match retail; host libm is a red control."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from gameplay_sources import prepare_sources
from check_gameplay import node_runtime


class GameplayTrigTests(unittest.TestCase):
    def test_original_kernel_and_host_libm_negative_control(self):
        sdk = ROOT / '.deps/emsdk'
        emcc = sdk / 'upstream/emscripten/emcc.py'
        if not emcc.is_file():
            self.skipTest('Project-local SDK unavailable')
        source = prepare_sources()
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / '.emscripten'),
                   EMSDK_PYTHON=sys.executable)
        common = [sys.executable, str(emcc), '-O2', '-std=c11',
                  '-fno-builtin-sinf', '-fno-builtin-cosf', '-fno-builtin-tanf',
                  '-fno-builtin-atanf', '-fno-builtin-atan2f',
                  '-ffunction-sections', '-fdata-sections',
                  '-ffp-contract=off', '-DTARGET_PC',
                  '-I', str(ROOT / 'src'), '-I', str(source),
                  '-I', str(ROOT / '.deps/aurora/include'),
                  '-I', str(ROOT / '.deps/melee/extern/dolphin/include'),
                  '-include', str(ROOT / 'src/gameplay_compat.h'),
                  str(ROOT / 'tests/gameplay_trig_trace.c'),
                  '-sENVIRONMENT=node', '-sEXIT_RUNTIME=1']
        outputs = {}
        with tempfile.TemporaryDirectory(prefix='melee-trig-') as directory:
            for name, files in (
                ('source', [source / 'MSL/trigf.c', source / 'MSL/math_data.c',
                            source / 'melee/lb/lbtrigf.c', source / 'MSL/float.c']),
                ('libm', []),
            ):
                target = Path(directory) / (name + '.js')
                result = subprocess.run(common + [str(p) for p in files] + ['-o', str(target)],
                                        env=env, capture_output=True, text=True, timeout=90)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(node_runtime()), str(target)],
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                outputs[name] = result.stdout.strip()
        self.assertEqual(outputs['source'], 'bf20b463 bf474614\nbf1b04f7 bf11b7bd 3f527b31')
        self.assertNotEqual(outputs['libm'], outputs['source'])
