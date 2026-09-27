"""Headless reference-driver failure propagation; no gameplay acceptance."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_gameplay import node_runtime


class CharacterReferenceHealthTests(unittest.TestCase):
    def test_missing_observations_fail_closed(self):
        result = subprocess.run(
            [str(node_runtime()), str(ROOT / 'tests/character_reference_health_test.mjs')],
            capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('rejects disconnected', result.stdout)


if __name__ == '__main__':
    unittest.main()
