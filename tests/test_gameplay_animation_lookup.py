"""Keep original first-animation lookup valid across the Wasm longjmp boundary."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_gameplay import node_runtime

class AnimationLookup(unittest.TestCase):
    def test_first_animation_survives_longjmp(self):
        source = ROOT / 'build/gameplay-source/src/melee/gr/granime.c'
        compiler = ROOT / '.deps/emsdk/upstream/emscripten/emcc'
        if not source.is_file() or not compiler.is_file():
            self.skipTest('Prepared source and pinned Wasm SDK required')
        text = source.read_text()
        start = text.index('#if defined(MELEE_WEB_GAMEPLAY)\nvoid fn_801C82E8')
        end = text.index('\nmelee_source_bool grAnime_801C83D0', start)
        functions = text[start:end]
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            for negative in (False, True):
                case = functions.replace('HSD_AObj* volatile', 'HSD_AObj*') if negative else functions
                (temp / 'lookup.inc').write_text(case)
                output = temp / 'lookup.js'
                env = dict(os.environ, EM_CONFIG=str(ROOT / '.deps/emsdk/.emscripten'))
                command = [str(compiler), '-std=c11', '-O2', '-DMELEE_WEB_GAMEPLAY', '-DTARGET_PC',
                           '-Isrc', '-Ibuild/gameplay-source/src', '-I.deps/aurora/include',
                           '-I', str(temp), 'tests/gameplay_animation_lookup_test.c',
                           '-sENVIRONMENT=node', '-sASSERTIONS=1', '-o', str(output)]
                built = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=90)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                result = subprocess.run([str(node_runtime()), str(output)], capture_output=True, text=True, timeout=20)
                if negative:
                    self.assertNotEqual(result.returncode, 0, "Non-volatile longjmp result unexpectedly survived")
                else:
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
