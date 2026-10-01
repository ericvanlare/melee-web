"""Exercise Kirby's real Falco-copy costume material animation independently."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class KirbyCopyMaterialAssetTests(unittest.TestCase):
    def test_falco_copy_part_and_costume_native_graphs(self):
        assets = ROOT / "assets-local/next-gate"
        required = [assets / name for name in
                    ("PlKbCpFc.dat", "PlKbNrCpFc.dat", "PlKb.dat", "PlKbAJ.dat")]
        if not all(path.is_file() for path in required):
            self.skipTest("Owned GALE01 revision 2 Kirby/Falco costume asset is absent")
        if not (ROOT / "build/gameplay-source/src/sysdolphin/baselib/mobj.h").is_file():
            self.skipTest("Prepared original gameplay headers unavailable; build gameplay first")
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            raise RuntimeError("A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="melee Kirby Falco copy ") as directory:
            binary = Path(directory) / "kirby_copy_material_trace"
            result = subprocess.run(
                [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
                 "-DTARGET_PC", "-I", str(ROOT / "src"),
                 "-I", str(ROOT / "build/gameplay-source/src"),
                 "-I", str(ROOT / ".deps/aurora/include"),
                 *(str(ROOT / "src" / (name + ".cpp")) for name in
                   ("dat_archive", "dat_animation", "dat_fighter_runtime",
                    "fighter_binding", "dat_texture", "dat_material",
                    "rigid_model", "dat_native_joint", "dat_material_animation")),
                 str(ROOT / "tests/kirby_copy_material_trace.cpp"), "-o", str(binary)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run(
                [str(binary), *(str(path) for path in required)],
                capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertRegex(result.stdout,
                             r"field\+0x0 word=0x[1-9a-f][0-9a-f]* relocation=0")
            self.assertRegex(result.stdout,
                             r"field\+0x14 word=0x[0-9a-f]+ relocation=1")
            self.assertRegex(result.stdout,
                             r"Kirby Falco-copy source hat graph root=DAT\+0xf954 joints=48 PObjs=10")
            self.assertIn("Kirby Falco-copy costume material graph", result.stdout)
            for motion in (23, 42, 455):
                self.assertIn(f"Kirby copy motion {motion} flags=0x", result.stdout)
            print(result.stdout, end="")


if __name__ == "__main__":
    unittest.main()
