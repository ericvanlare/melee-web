"""Exercise the original FObj parser's terminal single-constant boundary."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from check_gameplay import node_runtime

class TerminalConstants(unittest.TestCase):
    def test_delayed_constants_and_unknown_state_rejection(self):
        source=ROOT/'build/gameplay-source/src/sysdolphin/baselib'
        compiler=ROOT/'.deps/emsdk/upstream/emscripten/emcc'
        if not (source/'fobj.c').is_file() or not compiler.is_file():
            self.skipTest('Prepared gameplay source and pinned SDK required')
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)/'terminal.js'
            env=dict(os.environ,EM_CONFIG=str(ROOT/'.deps/emsdk/.emscripten'))
            command=[str(compiler),'-O2','-std=c11','-DTARGET_PC','-ffunction-sections','-fdata-sections',
                     '-ffp-contract=off','-Isrc','-Ibuild/gameplay-source/src','-I.deps/aurora/include',
                     '-include','src/hsd_probe_compat.h',str(source/'fobj.c'),str(source/'spline.c'),
                     'tests/gameplay_fobj_terminal_test.c','-sENVIRONMENT=node','-sEXIT_RUNTIME=1','-o',str(output)]
            built=subprocess.run(command,cwd=ROOT,env=env,capture_output=True,text=True,timeout=90)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            for arguments in ([],['--invalid']):
                result=subprocess.run([str(node_runtime()),str(output),*arguments],capture_output=True,text=True,timeout=20)
                if arguments:
                    self.assertEqual(result.returncode,2,result.stdout+result.stderr)
                    self.assertIn('undefined interpolation output',result.stderr)
                else:self.assertEqual(result.returncode,0,result.stdout+result.stderr)
