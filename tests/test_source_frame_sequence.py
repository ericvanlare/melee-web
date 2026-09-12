"""Compile and run the dependency-light source frame sequence test."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SourceFrameSequenceTests(unittest.TestCase):
    def test_source_draw_order_and_failure_contract(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        self.assertIsNotNone(compiler, "A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="melee source frame sequence ") as directory:
            binary = Path(directory) / "source_frame_sequence"
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
                    str(ROOT / "tests/source_frame_sequence_test.cpp"),
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
