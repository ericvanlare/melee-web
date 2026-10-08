"""Fast report gates; the real HTTP/Chrome CLI contract is run explicitly."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_gameplay import node_runtime


class WholeSessionCaptureTests(unittest.TestCase):
    def test_shared_runtime_inventory_uses_real_http_and_rejects_missing_or_changed_modules(self):
        result = subprocess.run(
            [str(node_runtime()), str(ROOT / 'tests/browser_artifact_inventory_test.mjs')],
            capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('Current 34-file inventory', result.stdout)

    def test_natural_pause_diagnostic_ownership_and_export_boundaries(self):
        result = subprocess.run(
            [str(node_runtime()), str(ROOT / 'tests/natural_pause_diagnostic_test.mjs')],
            capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('Stopped-scene pair target, source interval, order, and scoped image completeness passed.',
                      result.stdout)

    def test_final_success_requires_complete_replay_clean_diagnostics_and_exports(self):
        result = subprocess.run(
            [str(node_runtime()), str(ROOT / 'tests/whole_session_capture_result_test.mjs')],
            capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('rejection controls passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
