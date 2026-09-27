"""Exact paired-single camera vector and HSD CObj consumer regressions."""

import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime
from gameplay_sources import prepare_sources


class GameplayCameraMathTests(unittest.TestCase):
    def test_source_instruction_oracle_aliases_and_dirty_camera_consumer(self):
        sdk = ROOT / ".deps/emsdk"
        emcc = sdk / "upstream/emscripten/emcc.py"
        if not emcc.is_file():
            self.skipTest("Project-local SDK unavailable")
        source = prepare_sources()
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / ".emscripten"),
                   EMSDK_PYTHON=sys.executable)
        common = [sys.executable, str(emcc), "-O2", "-std=c11", "-ffp-contract=off",
                  "-DTARGET_PC", "-ffunction-sections", "-fdata-sections",
                  "-I", str(ROOT / "src"), "-I", str(source),
                  "-I", str(ROOT / ".deps/aurora/include"),
                  "-I", str(ROOT / ".deps/melee/extern/dolphin/include"),
                  "-include", str(ROOT / "src/gameplay_compat.h"),
                  str(ROOT / "tests/gameplay_camera_math_trace.c"),
                  str(ROOT / "src/gameplay_ps_math.c"),
                  str(source / "sysdolphin/baselib/cobj.c"),
                  str(ROOT / ".deps/aurora/lib/dolphin/mtx/mtx.c"),
                  str(ROOT / ".deps/aurora/lib/dolphin/mtx/mtxvec.c"),
                  str(ROOT / ".deps/aurora/lib/dolphin/mtx/vec.c"),
                  "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1"]
        with tempfile.TemporaryDirectory(prefix="melee-camera-math-") as directory:
            target = Path(directory) / "camera_math.js"
            built = subprocess.run(common + ["-o", str(target)], env=env,
                                   capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(node_runtime()), str(target)],
                                 capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        match = re.fullmatch(
            r"camera-math vectors=64 cross=64 aliases=8 cameras=2 fallback_differences=([1-9][0-9]*)\n?",
            run.stdout)
        self.assertIsNotNone(match, run.stdout)

    def test_owned_dol_instruction_profile(self):
        configured = os.environ.get("MELEE_CPU_DOL")
        if not configured:
            self.skipTest("owned DOL not configured (MELEE_CPU_DOL)")
        sys.path.insert(0, str(ROOT / "tools"))
        from retail_allocation_profile import Dol, read_symbols
        dol = Dol(Path(configured))
        symbols = read_symbols(ROOT / ".deps/melee/config/GALE01/symbols.txt")
        expected = {
            "C_MTXLookAt": (0x80342734, 0x18C,
                             "340ac093654a53c7ad6ccebfb1c6e912a1315b186c1cb5eafc4dc446e0f501d0"),
            "PSVECNormalize": (0x80342DB8, 0x44,
                               "c1544f35a474d45c4187a02c2b7462e226d54983fb01f5fc2354971e9bbbe289"),
            "PSVECCrossProduct": (0x80342E58, 0x3C,
                                   "5628e2b0f92b5abb682884c6b2ca18d2286f3036053e283dbc44550998ee11a4"),
        }
        for name, (address, size, digest) in expected.items():
            self.assertEqual((symbols[name]["address"], symbols[name]["size"]),
                             (address, size))
            self.assertEqual(hashlib.sha256(dol.read(address, size)).hexdigest(), digest)


if __name__ == "__main__":
    unittest.main()
