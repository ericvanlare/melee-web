"""Exact PSMTXQuat and fres checks from isolated retail scalar captures."""

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


class GameplayPsQuatTests(unittest.TestCase):
    def test_retail_psquat_vectors_and_fres_edges(self):
        sdk = ROOT / ".deps/emsdk"
        emcc = sdk / "upstream/emscripten/emcc.py"
        if not emcc.is_file():
            self.skipTest("Project-local SDK unavailable")
        source = prepare_sources()
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / ".emscripten"),
                   EMSDK_PYTHON=sys.executable)
        common = [sys.executable, str(emcc), "-O2", "-std=c11",
                  "-fno-builtin-sinf", "-fno-builtin-cosf", "-fno-builtin-tanf",
                  "-fno-builtin-atanf", "-fno-builtin-atan2f", "-fno-builtin-acosf",
                  "-ffunction-sections", "-fdata-sections", "-ffp-contract=off",
                  "-DTARGET_PC", "-DMELEE_WEB_GAMEPLAY",
                  "-I", str(ROOT / "src"), "-I", str(source),
                  "-I", str(ROOT / ".deps/aurora/include"),
                  "-I", str(ROOT / ".deps/melee/extern/dolphin/include"),
                  "-include", str(ROOT / "src/gameplay_compat.h"),
                  str(ROOT / "tests/gameplay_psquat_trace.c"),
                  str(ROOT / "src/gameplay_ps_math.c"),
                  "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1"]
        with tempfile.TemporaryDirectory(prefix="melee-psquat-") as directory:
            target = Path(directory) / "psquat.js"
            result = subprocess.run(common + ["-o", str(target)], env=env,
                                    capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(node_runtime(ROOT)), str(target)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout.strip(),
                             "psquat_cases 4 fres_cases 16 failures 0")


if __name__ == "__main__":
    unittest.main()
