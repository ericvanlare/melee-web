"""Actual stockout comparator controls; original retained rows are optional."""
from pathlib import Path
import shutil
import subprocess
import unittest

class StadiumStockoutPairBrowserTests(unittest.TestCase):
    def test_actual_stockout_pair_comparator(self):
        node = shutil.which('node')
        if not node:
            self.skipTest('Node is unavailable')
        source = Path(__file__).with_name('stadium_stockout_pair.test.mjs')
        result = subprocess.run([node, '--test', str(source)], capture_output=True,
                                text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('# fail 0', result.stdout)
