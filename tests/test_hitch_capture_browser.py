"""Run the bounded browser hitch collector's synthetic boundary checks."""

from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime  # noqa: E402


class HitchCaptureBrowserTests(unittest.TestCase):
    def test_browser_capture_modes_are_explicit_and_foreground_jobs_require_headed(self):
        result = subprocess.run(
            [str(node_runtime()), str(ROOT / "tests/foreground_browser_guard_test.mjs")],
            capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Capture mode guards reject implicit/conflicting modes", result.stdout)

    def test_frozen_profile_requires_complete_bound_artifact_inventories(self):
        result = subprocess.run(
            [str(node_runtime()), str(ROOT / "tests/hitch_profile_test.mjs")],
            capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Hitch profile inventory checks passed", result.stdout)

    def test_diagnostic_paint_control_geometry_and_failure_restoration(self):
        result = subprocess.run(
            [str(node_runtime()), str(ROOT / "tests/replay_paint_control_test.mjs")],
            capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

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
