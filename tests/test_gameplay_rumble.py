"""Owned data and original rumble interpreter boundary; no hardware claim."""
from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from check_gameplay import node_runtime
class RumbleTests(unittest.TestCase):
    def test_vs_startup_reuses_exact_published_source_rows(self):
        target=ROOT/"build/browser/gameplay_rumble_trace.js"
        if not target.is_file():
            self.skipTest("Build gameplay_rumble_trace")
        result=subprocess.run([str(node_runtime()),str(target)],cwd=ROOT,capture_output=True,text=True,timeout=60)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn("source rumble owner gate: passed",result.stdout)

    def test_owned_programs_and_source_interpreter(self):
        target=ROOT/"build/browser/gameplay_rumble_trace.js"
        asset=ROOT/"assets-local/next-gate/LbRb.dat"
        if not target.is_file() or not asset.is_file():
            self.skipTest("Build gameplay_rumble_trace and provide owned LbRb.dat")
        result=subprocess.run([str(node_runtime()),str(target),str(asset)],cwd=ROOT,capture_output=True,text=True,timeout=60)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn("source motor sequence, restart and malformed commands passed",result.stdout)
