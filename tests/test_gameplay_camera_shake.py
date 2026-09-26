"""Execute original camera translation scaling with source FMA order."""
import hashlib
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


class GameplayCameraShakeTests(unittest.TestCase):
    def test_production_depth_scaling_fusion_and_reset(self):
        sdk = ROOT / ".deps/emsdk"
        compiler = sdk / "upstream/emscripten/emcc.py"
        if not compiler.is_file():
            self.skipTest("Project-local SDK unavailable")
        source = prepare_sources()
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / ".emscripten"),
                   EMSDK_PYTHON=sys.executable)
        with tempfile.TemporaryDirectory(prefix="melee-camera-shake-") as tmp:
            target = Path(tmp) / "camera_shake.js"
            command = [sys.executable, str(compiler), "-O2", "-std=c11",
                       "-DTARGET_PC", "-ffp-contract=off", "-ffunction-sections",
                       "-fdata-sections", "-I", str(ROOT / "src"),
                       "-I", str(source), "-I", str(ROOT / ".deps/aurora/include"),
                       "-include", str(ROOT / "src/gameplay_compat.h"),
                       str(ROOT / "tests/gameplay_camera_shake_trace.c"),
                       "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-o", str(target)]
            built = subprocess.run(command, env=env, capture_output=True,
                                   text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(node_runtime()), str(target)],
                                 capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("camera shake cases=512 source_differences=0", run.stdout)

    def test_owned_dol_scale_instruction_profile(self):
        configured = os.environ.get("MELEE_CPU_DOL")
        if not configured:
            self.skipTest("owned DOL not configured (MELEE_CPU_DOL)")
        sys.path.insert(0, str(ROOT / "tools"))
        from retail_allocation_profile import Dol, read_symbols
        dol = Dol(Path(configured))
        symbol = read_symbols(ROOT / ".deps/melee/config/GALE01/symbols.txt")["Camera_8002A0C0"]
        self.assertEqual((symbol["address"], symbol["size"]), (0x8002A0C0, 0x1B8))
        self.assertEqual(hashlib.sha256(dol.read(symbol["address"], symbol["size"])).hexdigest(),
                         "13cda6078c85a1df31483a8bdcedb050f941496fe4a09ed1600d6ea091e558bd")


if __name__ == "__main__":
    unittest.main()
