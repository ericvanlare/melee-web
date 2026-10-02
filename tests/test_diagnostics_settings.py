from pathlib import Path
import shutil
import subprocess
import unittest


class DiagnosticsSettingsTests(unittest.TestCase):
    def test_preferences_and_explicit_inactive_export(self):
        node = shutil.which('node')
        if not node:
            self.skipTest('Node.js is unavailable')
        result = subprocess.run([node, str(Path(__file__).with_name('diagnostics_settings_test.mjs'))],
                                text=True, capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('Diagnostics Settings preference', result.stdout)
