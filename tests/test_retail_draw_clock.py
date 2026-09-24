from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class RetailDrawClockTests(unittest.TestCase):
    def test_original_clock_predictions_and_callback_split(self):
        compiler=shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as folder:
            binary=Path(folder)/'clock'
            built=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',
                '-I',str(ROOT/'src'),str(ROOT/'tests/retail_draw_clock_test.cpp'),
                '-o',str(binary)],capture_output=True,text=True,timeout=30)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
