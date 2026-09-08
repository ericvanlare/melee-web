"""Actual owned byte transfers with deferred completion and explicit failure."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class GameplayIoTests(unittest.TestCase):
    def test_real_bytes_order_bounds_cancellation_and_restart(self):
        with tempfile.TemporaryDirectory() as tmp:
            compiler = shutil.which('clang++') or shutil.which('c++')
            self.assertIsNotNone(compiler)
            binary = Path(tmp)/'io_trace'
            args = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT/'src'), str(ROOT/'src/gameplay_io.cpp'), str(ROOT/'tests/gameplay_io_test.cpp'), '-o', str(binary)]
            result = subprocess.run(args, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result = subprocess.run([str(binary)],capture_output=True,text=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
