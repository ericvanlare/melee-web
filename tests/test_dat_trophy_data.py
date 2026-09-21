"""Typed TyDatai source tables; no Results scene or source-lifetime claim."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DatTrophyDataTests(unittest.TestCase):
    def test_source_tables_and_sentinels(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        self.assertIsNotNone(compiler, "A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="melee TyDatai ") as directory:
            binary = Path(directory) / "dat_trophy_data"
            compiled = subprocess.run(
                [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
                 "-I", str(ROOT / "src"), str(ROOT / "src/dat_archive.cpp"),
                 str(ROOT / "src/dat_trophy_data.cpp"),
                 str(ROOT / "tests/dat_trophy_data_test.cpp"), "-o", str(binary)],
                capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(compiled.returncode, 0,
                             compiled.stdout + compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True,
                                    timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            asset = ROOT / "assets-local" / "results-mario" / "TyDatai.usd"
            if asset.is_file():
                result = subprocess.run([str(binary), "--assets", str(asset)],
                                        capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("typed TyDatai", result.stdout)


if __name__ == "__main__":
    unittest.main()
