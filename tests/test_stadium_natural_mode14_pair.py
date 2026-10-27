"""Register actual natural-mode14 browser controls; retained fixtures are optional."""
from pathlib import Path
import os
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]

class StadiumNaturalMode14PairTests(unittest.TestCase):
    def test_actual_natural_mode14_controls(self):
        node = shutil.which('node')
        if not node:
            self.skipTest('Node is unavailable')
        env = os.environ.copy()
        env.setdefault('MELEE_SOURCE_ROOT', str(ROOT))
        test_root = Path(env.get('MELEE_NATURAL14_TEST_ROOT', ROOT))
        result = subprocess.run([node, '--test', str(test_root / 'tests/stadium_natural_mode14_pair.test.mjs')],
                                env=env, capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('# fail 0', result.stdout)
        if not any(env.get(key) for key in ('MELEE_NATURAL14_PROJECTION', 'MELEE_NATURAL14_REPORT',
                                           'MELEE_STOCKOUT_PROJECTION', 'MELEE_NATURAL14_SUPERVISOR')):
            self.assertIn('# pass 1', result.stdout)
            self.assertIn('# skipped 5', result.stdout)
