"""Run the registered actual full-pair harness controls; retained data is optional."""
from pathlib import Path
import os
import shutil
import subprocess
import unittest

ROOT = Path(os.environ.get('MELEE_PAIRED_FULL_TEST_ROOT', Path(__file__).resolve().parents[1]))

class StadiumPairedV11FullTests(unittest.TestCase):
    def test_actual_full_pair_harness_controls(self):
        node = shutil.which('node')
        if not node:
            self.skipTest('Node is unavailable')
        result = subprocess.run([node, '--test', str(ROOT / 'tests/stadium_paired_v11_full.test.mjs')],
                                capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('# fail 0', result.stdout)
        if os.environ.get('MELEE_PAIRED_FULL_PROJECTION') and os.environ.get('MELEE_PAIRED_FULL_ENDPOINTS'):
            self.assertIn('# pass 7', result.stdout)
            self.assertIn('# skipped 0', result.stdout)
        else:
            self.assertIn('# pass 3', result.stdout)
            self.assertIn('# skipped 4', result.stdout)
