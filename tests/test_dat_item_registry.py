"""Source-sized character Article reference registry decoding."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class DatItemRegistryTests(unittest.TestCase):
    def test_exact_extent_and_invalid_references(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / 'item_registry'
            compiler = shutil.which('clang++') or shutil.which('c++')
            self.assertIsNotNone(compiler)
            args = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT/'src'), str(ROOT/'src/dat_archive.cpp'), str(ROOT/'src/dat_item_registry.cpp'), str(ROOT/'tests/dat_item_registry_test.cpp'), '-o', str(binary)]
            result = subprocess.run(args, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
