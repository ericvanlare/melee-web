"""Asset-free DSP SRC phase/history arithmetic tests."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class AudioResampleTests(unittest.TestCase):
    def test_scalar_conversion(self):
        compiler=shutil.which("clang") or shutil.which("cc")
        if not compiler: self.skipTest("A C compiler is required")
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/"src"
            subprocess.run([compiler,"-std=c11","-fsanitize=address,undefined","-I",str(ROOT/"src"),str(ROOT/"src/gameplay_audio_resample.c"),str(ROOT/"src/gameplay_audio_itd.c"),str(ROOT/"tests/gameplay_audio_resample_test.c"),"-o",str(binary)],check=True,capture_output=True,text=True)
            result=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn("nearest passed",result.stdout)
if __name__=="__main__":unittest.main()
