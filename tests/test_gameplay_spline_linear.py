"""Compile the patched type-0 stage spline against captured retail words."""
from __future__ import annotations

import os
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GRST = ROOT / "assets-local/next-gate/GrSt.dat"
GRST_SHA256 = "1ef0ccc51fc69bf2e06f55377111ec1b67e00438df0597bf6195c3032ae29f83"
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime  # noqa: E402
from gameplay_sources import prepare_sources  # noqa: E402


class GameplaySplineLinearTests(unittest.TestCase):
    def test_grst_type0_retail_samples_and_unfused_control(self):
        sdk = ROOT / ".deps/emsdk"
        emcc = sdk / "upstream/emscripten/emcc.py"
        if not emcc.is_file() or not (sdk / ".emscripten").is_file():
            self.skipTest("Project-local Emscripten SDK unavailable")
        if not GRST.is_file():
            self.skipTest("Ignored GrSt.dat fixture unavailable")
        self.assertEqual(hashlib.sha256(GRST.read_bytes()).hexdigest(), GRST_SHA256)

        source = prepare_sources()
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / ".emscripten"),
                   EMSDK_PYTHON=sys.executable)
        common = [sys.executable, str(emcc), "-O2", "-std=c11",
                  "-ffp-contract=off", "-DTARGET_PC",
                  "-I", str(ROOT / "src"), "-I", str(source),
                  "-I", str(ROOT / ".deps/aurora/include"),
                  "-I", str(ROOT / ".deps/melee/extern/dolphin/include"),
                  "-include", str(ROOT / "src/gameplay_compat.h"),
                  str(ROOT / "tests/gameplay_spline_linear_trace.c"),
                  str(source / "sysdolphin/baselib/spline.c"),
                  "-sENVIRONMENT=node", "-sNODERAWFS=1", "-sEXIT_RUNTIME=1"]
        with tempfile.TemporaryDirectory(prefix="melee-spline-linear-") as directory:
            target = Path(directory) / "spline_linear.js"
            result = subprocess.run(common + ["-o", str(target)], env=env,
                                    cwd=directory, capture_output=True, text=True,
                                    timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(node_runtime()), str(target), str(GRST)], cwd=directory,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.strip(),
                         "spline-linear cases=9 retail=all fused=x-y-z negative=diff")


if __name__ == "__main__":
    unittest.main()
