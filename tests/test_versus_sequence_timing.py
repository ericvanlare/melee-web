"""Focused pure timing-plan accounting checks; no browser or build is required."""

from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime  # noqa: E402


class VersusSequenceTimingTests(unittest.TestCase):
    def test_timing_accounting_boundaries(self):
        result = subprocess.run(
            [str(node_runtime()), str(ROOT / "tests/versus_sequence_timing_test.mjs")],
            cwd=ROOT, capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Versus sequence timing accounting boundaries passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
