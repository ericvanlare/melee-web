"""Captured original joint matrices distinguish fused SRT from unfused C."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from gameplay_sources import prepare_sources
from check_gameplay import node_runtime


class GameplaySrtTests(unittest.TestCase):
    def test_original_parent_and_child_matrices_and_unfused_control(self):
        sdk = ROOT / ".deps/emsdk"
        emcc = sdk / "upstream/emscripten/emcc.py"
        if not emcc.is_file():
            self.skipTest("Project-local SDK unavailable")
        source = prepare_sources()
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / ".emscripten"),
                   EMSDK_PYTHON=sys.executable)
        common = [sys.executable, str(emcc), "-O2", "-std=c11",
                  "-fno-builtin-sinf", "-fno-builtin-cosf", "-ffp-contract=off",
                  "-ffunction-sections", "-fdata-sections", "-DTARGET_PC",
                  "-I", str(ROOT / "src"), "-I", str(source),
                  "-I", str(ROOT / ".deps/aurora/include"),
                  "-I", str(ROOT / ".deps/melee/extern/dolphin/include"),
                  "-include", str(ROOT / "src/gameplay_compat.h"),
                  str(ROOT / "tests/gameplay_srt_trace.c"),
                  str(ROOT / "src/gameplay_ps_math.c"),
                  str(source / "MSL/trigf.c"), str(source / "MSL/math_data.c"),
                  str(source / "MSL/float.c"),
                  "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1"]
        output = {}
        with tempfile.TemporaryDirectory(prefix="melee-srt-") as directory:
            for name, matrix_source in (
                ("source", source / "sysdolphin/baselib/mtx.c"),
                ("unfused", ROOT / ".deps/melee/src/sysdolphin/baselib/mtx.c"),
            ):
                target = Path(directory) / (name + ".js")
                result = subprocess.run(common + [str(matrix_source), "-o", str(target)],
                                        env=env, capture_output=True, text=True, timeout=90)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(node_runtime()), str(target)],
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                output[name] = result.stdout.strip().splitlines()
        self.assertEqual(output["source"], [
            "3effef31 bcb5e192 3f750455 c271c1d2 bc9df328 3f8a2519 "
            "3d0fcb75 4189b18b bf75087a bd088ad0 3eff8e2a 32d201bf",
            "3f3235cd 3e9cfd55 3f444476 c271b531 be2d5932 3f84699f "
            "be85209c 419785a6 bf4ee44e 3d42405d 3f370065 bce87db0",
        ])
        self.assertNotEqual(output["unfused"], output["source"])
        self.assertEqual(output["unfused"][0].split()[10], "3eff8e2b")
        self.assertEqual(output["unfused"][1].split()[11], "bce87daf")


if __name__ == "__main__":
    unittest.main()
