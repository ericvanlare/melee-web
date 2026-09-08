from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from check_gameplay import node_runtime
class StageCallbackSignatures(unittest.TestCase):
    def test_original_dispatch_all_formats_and_negative_control(self):
        source=ROOT/"build/gameplay-source/src/melee/gr/granime.c"
        compiler=ROOT/".deps/emsdk/upstream/emscripten/emcc"
        if not source.is_file() or not compiler.is_file():self.skipTest("Prepared source and pinned Wasm SDK required")
        text=source.read_text()
        start=text.index("typedef void (*Callback1)")
        end=text.index("\nvoid grAnime_801C706C",start)
        dispatch=text[start:end]
        self.assertIn("case AOBJ_ARG_AF",dispatch,"Prepared source needs typed stage callback patch")
        with tempfile.TemporaryDirectory() as directory:
            temp=Path(directory);(temp/"stage_dispatch.inc").write_text(dispatch)
            env=dict(os.environ,EM_CONFIG=str(ROOT/".deps/emsdk/.emscripten"))
            for negative in (False,True):
                output=temp/("negative.js" if negative else "positive.js")
                command=[str(compiler),"-std=c11","-O1","-DTARGET_PC","-Isrc","-Ibuild/gameplay-source/src","-I.deps/aurora/include","-I",str(temp),"tests/gameplay_stage_dispatch_test.c","-sENVIRONMENT=node","-sASSERTIONS=1","-o",str(output)]
                if negative:command.append("-DDISPATCH_NEGATIVE_CONTROL")
                result=subprocess.run(command,cwd=ROOT,env=env,capture_output=True,text=True,timeout=90)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                result=subprocess.run([str(node_runtime()),str(output)],capture_output=True,text=True,timeout=20)
                if negative:self.assertNotEqual(result.returncode,0,"Untyped PPC dispatch unexpectedly passed Wasm ABI negative control")
                else:self.assertEqual(result.returncode,0,result.stdout+result.stderr)
