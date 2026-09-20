"""Typed fighter attributes, exact motion identity and owned animation storage."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DatFighterRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="melee fighter runtime ")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "fighter_runtime_test"
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            raise RuntimeError("A C++20 compiler is required")
        result = subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                                 "-I", str(ROOT / "src"), *[str(ROOT / "src" / (name + ".cpp")) for name in
                                 ("dat_archive", "dat_animation", "fighter_binding", "dat_fighter_runtime")],
                                 str(ROOT / "tests/dat_fighter_runtime_test.cpp"), "-o", str(cls.binary)],
                                capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def test_checked_fighter_data_and_storage(self):
        for case in ("decoded_values", "malformed_attributes", "malformed_actions",
                     "owned_command_boundary", "selected_motion_identity", "hurtbox_dynamics"):
            with self.subTest(case=case):
                result = subprocess.run([str(self.binary), case], capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_real_donkey_attribute_boundary(self):
        asset = ROOT / "assets-local/full-game-donkey/PlDk.dat"
        if not asset.is_file():
            self.skipTest("owned Donkey fighter archive is unavailable")
        result = subprocess.run([str(self.binary), "real_donkey", str(asset)],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Donkey 0x74 attributes, signed counters, cargo floats, authored dynamics and null x48: passed",
                      result.stdout)

    def test_real_luigi_attribute_boundary(self):
        asset = ROOT / "assets-local/full-game-luigi/PlLg.dat"
        effects = ROOT / "assets-local/full-game-luigi/EfLgData.dat"
        if not asset.is_file() or not effects.is_file():
            self.skipTest("owned Luigi fighter/effect archives are unavailable")
        result = subprocess.run([str(self.binary), "real_luigi", str(asset), str(effects)],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_real_pikachu_attribute_boundary(self):
        asset = ROOT / "assets-local/full-game-pikachu/PlPk.dat"
        if not asset.is_file():
            self.skipTest("owned Pikachu fighter archive is unavailable")
        result = subprocess.run([str(self.binary), "real_pikachu", str(asset)],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_real_pichu_attribute_boundary(self):
        asset = ROOT / "assets-local/full-game-pichu/PlPc.dat"
        if not asset.is_file():
            self.skipTest("owned Pichu fighter archive is unavailable")
        result = subprocess.run([str(self.binary), "real_pichu", str(asset)],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_real_purin_attribute_boundary(self):
        asset = ROOT / "assets-local/full-game-jigglypuff/PlPr.dat"
        if not asset.is_file():
            self.skipTest("owned Purin fighter archive is unavailable")
        result = subprocess.run([str(self.binary), "real_purin", str(asset)],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
