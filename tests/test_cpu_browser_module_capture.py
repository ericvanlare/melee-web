"""Check replay-failure observation without claiming native gameplay evidence."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class CpuBrowserModuleCaptureTests(unittest.TestCase):
    def test_loader_callbacks_partial_output_and_bounds(self):
        result = subprocess.run(
            [str(node_runtime()), str(ROOT / "tests/cpu_browser_module_capture_test.mjs")],
            capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("retains bounded overflow evidence", result.stdout)
