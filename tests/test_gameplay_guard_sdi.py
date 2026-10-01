"""Reduce both source shield SDI writers and verify their original arithmetic."""
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime
from gameplay_sources import prepare_sources


class GameplayGuardSdiTests(unittest.TestCase):
    def test_actual_callbacks_round_fused_position_writes_and_preserve_gates(self):
        sdk = ROOT / ".deps/emsdk"
        compiler = sdk / "upstream/emscripten/emcc.py"
        if not compiler.is_file():
            self.skipTest("Project-local SDK unavailable")
        source = prepare_sources()
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / ".emscripten"),
                   EMSDK_PYTHON=sys.executable)
        with tempfile.TemporaryDirectory(prefix="melee-guard-sdi-") as directory:
            target = Path(directory) / "guard_sdi.js"
            command = [sys.executable, str(compiler), "-O2", "-std=c11",
                       "-DTARGET_PC", "-ffp-contract=off", "-ffunction-sections",
                       "-fdata-sections", "-I", str(ROOT / "src"), "-I", str(source),
                       "-I", str(ROOT / ".deps/aurora/include"),
                       "-include", str(ROOT / "src/gameplay_compat.h"),
                       str(ROOT / "tests/gameplay_guard_sdi_trace.c"),
                       str(source / "melee/ft/kinds/ftCommon/ftCo_Guard.c"),
                       "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-o", str(target)]
            built = subprocess.run(command, env=env, capture_output=True,
                                   text=True, timeout=90)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(node_runtime()), str(target)], capture_output=True,
                                 text=True, timeout=30)
            print(ran.stdout, end="")
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("shield SDI boundary x=4145fcc5 original=4145fcc5", ran.stdout)
            self.assertIn("cases=1024", ran.stdout)
            self.assertIn("source_differences=0", ran.stdout)

    def test_owned_dol_two_products_then_fused_position_updates(self):
        configured = os.environ.get("MELEE_CPU_DOL")
        if not configured:
            self.skipTest("owned DOL not configured (MELEE_CPU_DOL)")
        sys.path.insert(0, str(ROOT / "tools"))
        from retail_allocation_profile import Dol, read_symbols
        dol = Dol(Path(configured))
        symbols = read_symbols(ROOT / ".deps/melee/config/GALE01/symbols.txt")
        bodies = {
            "ftCo_80093240": (0x80093240, 0x9C,
                "c72aea9d74455a8f112315df571a6c1eb07d836cbbebc4a3fa0714f0aa6e55b2"),
            "ftCo_800932DC": (0x800932DC, 0x78,
                "cf1775c1d7a054c161a4ec25e9fff323761dacbe3ec05510593daa1cb20e8f42"),
        }
        for name, (address, size, digest) in bodies.items():
            self.assertEqual((symbols[name]["address"], symbols[name]["size"]), (address, size))
            self.assertEqual(hashlib.sha256(dol.read(address, size)).hexdigest(), digest)
        word = lambda address: struct.unpack(">I", dol.read(address, 4))[0]
        for distance, factor, x, negate_y, y in (
                (0x800932A8, 0x800932B4, 0x800932B8, 0x800932C8, 0x800932CC),
                (0x80093324, 0x80093330, 0x80093334, 0x80093344, 0x80093348)):
            self.assertEqual(word(distance), 0xEC420032)  # fmuls f2,f2,f0
            self.assertEqual(word(factor), 0xEC4300B2)  # fmuls f2,f3,f2
            self.assertEqual(word(x), 0xEC0100BA)  # fmadds f0,f1,f2,f0
            self.assertEqual(word(negate_y), 0xFC200850)  # fneg f1,f1
            self.assertEqual(word(y), 0xEC0100BA)


if __name__ == "__main__":
    unittest.main()
