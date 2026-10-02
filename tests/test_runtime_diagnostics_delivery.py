"""Focused Node coverage for the inactive diagnostics delivery adapter."""

from pathlib import Path
import shutil
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]


class RuntimeDiagnosticsDeliveryTests(unittest.TestCase):
    def test_bounded_private_inactive_delivery(self):
        node = shutil.which("node")
        if node is None:
            self.skipTest("system Node.js is unavailable")
        result = subprocess.run(
            [node, str(ROOT / "tests/runtime_diagnostics_delivery_test.mjs")],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "Runtime diagnostics delivery bounds, privacy, scheduling, persistence and environment checks passed",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
