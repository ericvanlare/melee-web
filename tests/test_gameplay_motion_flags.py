"""Run the production motion metadata producer and Cape category consumer."""
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


class MotionFlagsTests(unittest.TestCase):
    def test_word_aliases_and_production_cape_consumer(self):
        sdk = ROOT / ".deps/emsdk"
        compiler = sdk / "upstream/emscripten/emcc.py"
        if not compiler.is_file():
            self.skipTest("project SDK unavailable")
        source = prepare_sources()
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / ".emscripten"),
                   EMSDK_PYTHON=sys.executable)
        with tempfile.TemporaryDirectory(prefix="melee-motion-flags-") as tmp:
            target = Path(tmp) / "motion-flags.js"
            original = (ROOT / ".deps/melee/src/melee/ft/types.h").read_text()
            start = original.index("union Struct2070 {")
            end = original.index("\n};", start) + 3
            (Path(tmp) / "legacy_motion_word.h").write_text(
                original[start:end].replace("Struct2070", "LegacyStruct2070"))
            command = [sys.executable, str(compiler), "-O2", "-std=c11",
                       "-DTARGET_PC", "-ffunction-sections", "-fdata-sections",
                       "-fno-strict-aliasing", "-ffp-contract=off",
                       "-I", str(ROOT / "src"), "-I", str(source), "-I", tmp,
                       "-I", str(ROOT / ".deps/aurora/include"),
                       "-include", str(ROOT / "src/gameplay_compat.h"),
                       str(source / "melee/ft/ft_0892.c"),
                       str(source / "melee/ft/kinds/ftCommon/ftCo_DamageSong.c"),
                       str(ROOT / "tests/gameplay_motion_flags_trace.c"),
                       "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-o", str(target)]
            built = subprocess.run(command, env=env, capture_output=True,
                                   text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(node_runtime()), str(target)],
                                 capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("all Cape categories passed", run.stdout)

    def test_owned_dol_category_and_catch_word(self):
        configured = os.environ.get("MELEE_CPU_DOL")
        if not configured:
            self.skipTest("owned DOL not configured (MELEE_CPU_DOL)")
        sys.path.insert(0, str(ROOT / "tools"))
        from retail_allocation_profile import Dol, read_symbols
        from original_boot_context import words
        dol = Dol(Path(configured))
        symbols = read_symbols(ROOT / ".deps/melee/config/GALE01/symbols.txt")
        # MotionState has three words followed by five callback pointers.
        address = symbols["ftData_MotionStateList"]["address"] + 212 * 32 + 4
        self.assertEqual(int.from_bytes(dol.read(address, 4), "big"), 0x00a00033)
        body = words(dol, symbols["ftCo_800C3538"])
        self.assertIn(0x88042071, body)  # lbz r0,0x2071(r4)
        self.assertIn(0x5400e73e, body)  # high nibble of that source byte
        self.assertIn(0x2c00000c, body)  # category >= 12 uses Cape-only route
        self.assertIn(0x2c000009, body)  # categories 9..11 reject that route


if __name__ == "__main__":
    unittest.main()
