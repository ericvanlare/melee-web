"""Focused checks for the opt-in browser registration export boundary."""
from pathlib import Path
import shutil
import subprocess
import unittest

class StadiumRegistrationBrowserTests(unittest.TestCase):
    def test_actual_browser_adapter(self):
        node = shutil.which('node')
        if not node:
            self.skipTest('Node is unavailable')
        script = Path(__file__).with_name('stadium_registration_browser.test.mjs')
        result = subprocess.run([node, '--test', str(script)], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('# pass 4', result.stdout)
        self.assertIn('# fail 0', result.stdout)
