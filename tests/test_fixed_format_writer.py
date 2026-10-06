"""Compile and run the dependency-light fixed-format writer test.

The menu browser's JSON observers format one field per FixedFormatWriter call.
This checks that a fragment sequence writes the same bytes and returns the
same length as one std::snprintf over the concatenated format, for every
buffer size including truncation.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class FixedFormatWriterTests(unittest.TestCase):
    def test_fragments_match_single_snprintf_at_every_buffer_size(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        self.assertIsNotNone(compiler, "A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="melee fixed format writer ") as directory:
            binary = Path(directory) / "fixed_format_writer"
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++20",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wformat=2",
                    "-O1",
                    "-I",
                    str(ROOT / "src"),
                    str(ROOT / "tests/fixed_format_writer_test.cpp"),
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
            self.assertIn("matches single snprintf for every buffer size", ran.stdout)


if __name__ == "__main__":
    unittest.main()
