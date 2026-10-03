"""Run strict schema and transactional backend checks against real SQLite."""
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_gameplay import node_runtime


class DiagnosticsWorkerTests(unittest.TestCase):
    def test_wire_validation_sqlite_caps_and_authenticated_expiry(self):
        try:
            node = str(node_runtime(ROOT))
        except (OSError, ValueError):
            node = shutil.which('node')
        if not node:
            self.skipTest('Node.js is unavailable')
        probe = subprocess.run([node, '--input-type=module', '-e', "await import('node:sqlite')"],
                               capture_output=True, text=True, timeout=10)
        if probe.returncode:
            self.skipTest('Node.js SQLite support is unavailable')
        result = subprocess.run([node, '--test', str(ROOT / 'diagnostics/tests/worker_test.mjs')],
                                cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('fail 0', result.stdout)
