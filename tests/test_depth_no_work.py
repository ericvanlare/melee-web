"""Private component controls; no native gameplay/restore admission."""
from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from check_gameplay import node_runtime
class DepthNoWorkTests(unittest.TestCase):
    def test_actual_guard_factory(self):
        result=subprocess.run([str(node_runtime()),str(ROOT/'tests/texture_readonly_guard.test.mjs')],capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('depth no-work actual factory:',result.stdout)
    def test_actual_private_boot_helper(self):
        result=subprocess.run([str(node_runtime()),str(ROOT/'tests/depth_no_work_boot.test.mjs')],capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('depth no-work actual boot helper:',result.stdout)
