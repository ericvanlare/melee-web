"""Exercise the original throw-position update with captured scalar operands."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_gameplay import node_runtime
from gameplay_sources import prepare_sources


class ThrowSmoothingTests(unittest.TestCase):
    def test_original_throw_rounding_and_unfused_negative_control(self):
        sdk = ROOT / '.deps/emsdk'
        emcc = sdk / 'upstream/emscripten/emcc.py'
        if not emcc.is_file():
            self.skipTest('Project-local SDK unavailable')
        source = (prepare_sources() / 'melee/ft/ftcommon.c').read_text()
        start = source.index('void ftCommon_8007E3EC(')
        end = source.index('\nvoid ftCommon_8007E5AC(', start)
        # Compile the production function unchanged with only its object access
        # boundary stubbed. The scalar expectations come from original replay.
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / '.emscripten'),
                   EMSDK_PYTHON=sys.executable)
        with tempfile.TemporaryDirectory(prefix='melee-throw-smoothing-') as directory:
            root = Path(directory)
            (root / 'throw_smoothing_source.h').write_text(source[start:end])
            target = root / 'throw.js'
            cmd = [sys.executable, str(emcc), '-O2', '-std=c11', '-ffp-contract=off',
                   '-I', str(root), str(ROOT / 'tests/gameplay_throw_smoothing_trace.c'),
                   '-sENVIRONMENT=node', '-sEXIT_RUNTIME=1', '-o', str(target)]
            built = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=90)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(node_runtime()), str(target)], capture_output=True,
                                 text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('retail=all unfused=diff clean-joint=unchanged', run.stdout)
