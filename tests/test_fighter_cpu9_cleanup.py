"""Asset-free controls execute actual CPU9 harness cleanup and finally code."""
from pathlib import Path
import subprocess
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_gameplay import node_runtime
class FighterCpu9CleanupTests(unittest.TestCase):
    def test_actual_owned_cleanup(self):
        result = subprocess.run([str(node_runtime(ROOT)), '--test',
            str(ROOT / 'tests/fighter_cpu9_cleanup.test.mjs')],
            cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
