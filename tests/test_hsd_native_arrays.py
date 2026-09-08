"""Bounded native array registry requires no GPU or proprietary assets."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class NativeArraysTests(unittest.TestCase):
    def test_exact_bounds_and_independent_registration_lifetime(self):
        compiler=shutil.which("clang") or shutil.which("cc")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/"arrays"
            result=subprocess.run([compiler,"-std=c11","-Wall","-Wextra","-Werror","-I",str(ROOT/"src"),
                str(ROOT/"src/hsd_native_arrays.c"),str(ROOT/"tests/hsd_native_arrays_test.c"),"-o",str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=="__main__":unittest.main()
