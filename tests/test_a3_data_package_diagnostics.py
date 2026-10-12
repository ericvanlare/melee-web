"""Focused passive data-package observer controls."""
from pathlib import Path
import subprocess
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime
class DataPackageDiagnosticsTests(unittest.TestCase):
    def test_lifecycle_and_readonly_seed_controls(self):
        result = subprocess.run([str(node_runtime(ROOT)), "--test", str(ROOT / "tests/a3_data_package_diagnostics.test.mjs")], cwd=ROOT, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("pass 4", result.stdout)
        self.assertIn("fail 0", result.stdout)
