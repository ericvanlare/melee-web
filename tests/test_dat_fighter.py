"""Check original fighter visibility table layouts using synthetic game-free bytes."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class DatFighterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="melee fighter visibility ")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "dat_fighter_test"
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            raise RuntimeError("A C++20 compiler is required")
        subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                        "-I", str(ROOT / "src"), str(ROOT / "src/dat_archive.cpp"),
                        str(ROOT / "src/dat_fighter.cpp"), str(ROOT / "tests/dat_fighter_test.cpp"),
                        "-o", str(cls.binary)], check=True, capture_output=True, timeout=120)
    def test_fighter_visibility(self):
        for case in ("normal_selection", "costume_fallback", "ambiguous_variants",
                     "counts_and_indices", "malformed_regions"):
            with self.subTest(case=case):
                result = subprocess.run([str(self.binary), case], capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
if __name__ == "__main__":
    unittest.main()
