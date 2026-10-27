"""Run the registered actual full-pair harness controls; retained data is optional."""
from pathlib import Path
import os
import re
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
        # First-form and Water query controls are always active; the four
        # retained original fixture controls remain optional, never discounted.
        counts = {name: int(value) for name, value in re.findall(
            r'^# (tests|pass|fail|cancelled|skipped) (\d+)$', result.stdout, re.MULTILINE)}
        retained = bool(os.environ.get('MELEE_PAIRED_FULL_PROJECTION') and
                        os.environ.get('MELEE_PAIRED_FULL_ENDPOINTS'))
        self.assertEqual(counts, {'tests': 15, 'pass': 15 if retained else 11,
                                 'fail': 0, 'cancelled': 0,
                                 'skipped': 0 if retained else 4}, result.stdout)
