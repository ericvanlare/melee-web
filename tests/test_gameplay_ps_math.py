"""The isolated PSMTXConcat helper follows the original paired-single order."""

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


class GameplayPsMathTests(unittest.TestCase):
    def test_source_fma_alias_and_signed_zero_vectors(self):
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
                  str(ROOT / "tests/gameplay_ps_math_trace.c"),
                  str(ROOT / "src/gameplay_ps_math.c"),
                  "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1"]
        with tempfile.TemporaryDirectory(prefix="melee-ps-math-") as directory:
            target = Path(directory) / "ps_math.js"
            result = subprocess.run(common + ["-o", str(target)], env=env,
                                    capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(node_runtime()), str(target)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            lines = result.stdout.strip().splitlines()
        self.assertEqual(len(lines), 2)
        self.assertEqual(lines[1], "signed-zero 80000000 80000000 80000000 80000000")
        # This output is the pinned operation order, including the col-2
        # Unit01 zero lane. It also makes an unfused C multiply/add negative
        # control fail inside the harness.
        self.assertEqual(lines[0],
                         "3efffffc c0980003 bfc00001 40580000 "
                         "c02fffff 41020002 bfc00006 bfb7fff4 "
                         "c0f80004 c0680001 41818002 41080000")


if __name__ == "__main__":
    unittest.main()
