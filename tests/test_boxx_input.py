"""Compile and run the standalone B0XX mapping vectors."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class BoxxInputTests(unittest.TestCase):
    def test_reference_vectors(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if compiler is None:
            raise unittest.SkipTest("A C++20 compiler is required for B0XX mapping vectors")
        with tempfile.TemporaryDirectory(prefix="melee boxx input ") as directory:
            binary = Path(directory) / "boxx_input_test"
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++20",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-pedantic",
                    "-O1",
                    "-I",
                    str(ROOT / "src"),
                    str(ROOT / "tests" / "boxx_input_test.cpp"),
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
            self.assertEqual(ran.stdout, "boxx input vectors passed\n")


if __name__ == "__main__":
    unittest.main()
