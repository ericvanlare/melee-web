"""Compile and run the bounded Results source-frame pause scheduler test."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ResultsSourceFramePauseTests(unittest.TestCase):
    def test_exact_pause_boundaries_and_missed_frame_contract(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        self.assertIsNotNone(compiler, "A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="melee results pause ") as directory:
            binary = Path(directory) / "results_source_frame_pause"
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++20",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-O1",
                    "-I",
                    str(ROOT / "src"),
                    str(ROOT / "tests/results_source_frame_pause_test.cpp"),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
                timeout=30,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
