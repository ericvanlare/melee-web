from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from check_gameplay import node_runtime
class AudioTransportTests(unittest.TestCase):
    def test_browser_pcm_queue(self):
        result=subprocess.run([str(node_runtime()),str(ROOT/'tests/audio_ring_test.mjs')],capture_output=True,text=True,timeout=15)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
