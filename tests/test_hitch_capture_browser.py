"""Run the bounded browser hitch collector's synthetic boundary checks."""

from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime  # noqa: E402


class HitchCaptureBrowserTests(unittest.TestCase):
    def test_driver_served_identity_and_bounded_trace(self):
        result = subprocess.run(
            [str(node_runtime()), str(ROOT / "tests/hitch_driver_test.mjs")],
            capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Hitch driver served identity", result.stdout)

    def test_bounded_collector_boundaries_and_snapshots(self):
        result = subprocess.run(
            [str(node_runtime()), str(ROOT / "tests/hitch_capture_test.mjs")],
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Bounded hitch capture boundaries", result.stdout)


if __name__ == "__main__":
    unittest.main()
