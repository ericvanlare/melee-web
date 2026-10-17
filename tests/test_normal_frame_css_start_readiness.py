"""Asset-free controls for the one-frame diagnostic CSS Start readiness boundary."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class NormalFrameCssStartReadinessTests(unittest.TestCase):
    def test_actual_guarded_readiness_and_single_start(self):
        result = subprocess.run(
            [str(node_runtime()), "--test",
             str(ROOT / "tests/normal_frame_css_start_readiness.test.mjs")],
            capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
