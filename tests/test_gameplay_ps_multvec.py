"""The isolated PSMTXMultVec helper preserves the retail PS lane order."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime
from gameplay_sources import prepare_sources


class GameplayPsMultVecTests(unittest.TestCase):
    def test_retail_vectors_aliases_and_unfused_control(self):
        sdk = ROOT / ".deps/emsdk"
        emcc = sdk / "upstream/emscripten/emcc.py"
        if not emcc.is_file():
            self.skipTest("Project-local SDK unavailable")
        source = prepare_sources()
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / ".emscripten"),
                   EMSDK_PYTHON=sys.executable)
        common = [sys.executable, str(emcc), "-O2", "-std=c11",
                  "-ffp-contract=off", "-DTARGET_PC",
                  "-I", str(ROOT / "src"), "-I", str(source),
                  "-I", str(ROOT / ".deps/aurora/include"),
                  "-I", str(ROOT / ".deps/melee/extern/dolphin/include"),
                  "-include", str(ROOT / "src/gameplay_compat.h"),
                  str(ROOT / "tests/gameplay_ps_multvec_trace.c"),
                  str(ROOT / "src/gameplay_ps_math.c"),
                  "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1"]
        with tempfile.TemporaryDirectory(prefix="melee-ps-multvec-") as directory:
            target = Path(directory) / "ps_multvec.js"
            result = subprocess.run(common + ["-o", str(target)], env=env,
                                    capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(node_runtime()), str(target)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            lines = result.stdout.strip().splitlines()
        self.assertEqual(lines, [
            "multvec cases=9 direct=9 inplace=9 mtx=9 ps=9 negative=y-ulp",
        ])


if __name__ == "__main__":
    unittest.main()
