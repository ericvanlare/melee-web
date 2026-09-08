"""Owned player-light decoding and malformed descriptor rejection."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class DatLightsTests(unittest.TestCase):
    def test_owned_descriptors_and_rejections(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / 'lights'
            compiler = shutil.which('clang++') or shutil.which('c++')
            self.assertIsNotNone(compiler)
            compiled = subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT/'src'), str(ROOT/'src/dat_archive.cpp'), str(ROOT/'src/dat_lights.cpp'), str(ROOT/'tests/dat_lights_test.cpp'), '-o', str(binary)], capture_output=True, text=True, timeout=120)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
