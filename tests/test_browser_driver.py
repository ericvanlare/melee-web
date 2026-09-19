"""Fast shared-driver checks; run the real-browser contract explicitly as documented."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_gameplay import node_runtime


class BrowserDriverTests(unittest.TestCase):
    def test_input_serialization_releases_and_deadline(self):
        result = subprocess.run([str(node_runtime()), str(ROOT / 'tests/browser_driver_test.mjs')],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_shared_browser_configuration(self):
        result = subprocess.run([str(node_runtime()), str(ROOT / 'tests/browser_tools_test.mjs')],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
