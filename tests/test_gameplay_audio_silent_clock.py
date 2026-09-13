"""Compile and run the public audio-disabled source-clock contract test."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class GameplayAudioSilentClockTests(unittest.TestCase):
    def test_fractional_source_clock_is_partition_invariant(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        if compiler is None:
            self.skipTest("A C11 compiler is required for the silent clock test")

        with tempfile.TemporaryDirectory(prefix="melee silent audio clock ") as directory:
            binary = Path(directory) / "gameplay_audio_silent_clock_test"
            result = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(ROOT / "src"),
                 str(ROOT / "tests" / "gameplay_audio_silent_clock_test.c"),
                 "-o", str(binary)],
                cwd=ROOT, capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run(
                [str(binary)], cwd=ROOT, capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Silent AX source clock", result.stdout)


if __name__ == "__main__":
    unittest.main()
