"""Checked original SIS byte streams; no game assets required."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class DatSisTests(unittest.TestCase):
    def test_owned_fonts_streams_aliases_and_rejections(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        self.assertIsNotNone(compiler, "A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="melee sis ") as directory:
            binary = Path(directory) / "sis"
            run = subprocess.run([compiler, "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT/"src"), str(ROOT/"src/dat_archive.cpp"),
                str(ROOT/"src/dat_sis.cpp"), str(ROOT/"tests/dat_sis_test.cpp"),
                "-o", str(binary)], capture_output=True, text=True, timeout=120)
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)

if __name__ == "__main__": unittest.main()
