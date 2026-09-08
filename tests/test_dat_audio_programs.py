"""Authored SEM boundaries and optional user-owned original sound programs."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class AudioProgramsTests(unittest.TestCase):
    def test_programs(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            self.skipTest("A C++20 compiler is required")
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "sem"
            subprocess.run([compiler, "-std=c++20", "-I", str(ROOT / "src"), str(ROOT / "src/dat_audio_programs.cpp"), str(ROOT / "tests/dat_audio_programs_test.cpp"), "-o", str(binary)], check=True, capture_output=True, text=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("10 malformed checks passed", result.stdout)
            asset = ROOT / "assets-local/next-gate/smash2.sem"
            if asset.is_file():
                result = subprocess.run([str(binary), str(asset)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("4035 programs validated", result.stdout)
if __name__ == "__main__":
    unittest.main()
