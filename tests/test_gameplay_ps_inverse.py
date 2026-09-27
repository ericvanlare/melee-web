"""Source PS inverse arithmetic and its original lazy camera consumer."""
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


class GameplayPsInverseTests(unittest.TestCase):
    def test_lane_order_aliases_singular_and_original_camera_consumer(self):
        sdk = ROOT / ".deps/emsdk"
        emcc = sdk / "upstream/emscripten/emcc.py"
        if not emcc.is_file():
            self.skipTest("Project-local SDK unavailable")
        source = prepare_sources()
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / ".emscripten"),
                   EMSDK_PYTHON=sys.executable)
        with tempfile.TemporaryDirectory(prefix="melee-ps-inverse-") as tmp:
            target = Path(tmp) / "inverse.js"
            command = [sys.executable, str(emcc), "-O2", "-std=c11", "-DTARGET_PC",
                       "-ffp-contract=off", "-ffunction-sections", "-fdata-sections",
                       "-I", str(ROOT / "src"), "-I", str(source),
                       "-I", str(ROOT / ".deps/aurora/include"),
                       "-include", str(ROOT / "src/gameplay_compat.h"),
                       str(ROOT / "tests/gameplay_ps_inverse_trace.c"),
                       str(ROOT / "src/gameplay_ps_math.c"),
                       str(source / "sysdolphin/baselib/cobj.c"),
                       str(ROOT / ".deps/aurora/lib/dolphin/mtx/mtx.c"),
                       "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-o", str(target)]
            built = subprocess.run(command, env=env, capture_output=True,
                                   text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(node_runtime()), str(target)],
                                 capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("inverse cases=130 aliases=260 camera=129", run.stdout)

    def test_owned_dol_instruction_profile(self):
        configured = os.environ.get("MELEE_CPU_DOL")
        if not configured:
            self.skipTest("owned DOL not configured (MELEE_CPU_DOL)")
        sys.path.insert(0, str(ROOT / "tools"))
        from retail_allocation_profile import Dol, read_symbols
        dol = Dol(Path(configured))
        symbol = read_symbols(ROOT / ".deps/melee/config/GALE01/symbols.txt")["PSMTXInverse"]
        self.assertEqual((symbol["address"], symbol["size"]), (0x80342320, 0xf8))
        # Bind the lane transcription to every instruction in the owned SDK
        # routine, including the zero-determinant return and fres refinement.
        self.assertEqual(hashlib.sha256(dol.read(symbol["address"], symbol["size"])).hexdigest(),
                         "619aff730415a57f4337d133f8eb8710461266610bfe500ab12dfa9660c5d20c")


if __name__ == "__main__":
    unittest.main()
