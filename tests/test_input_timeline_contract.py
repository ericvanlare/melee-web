# SPDX-License-Identifier: MIT
"""Expose the mock-only Node input-timeline controls to flat discovery."""

from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class InputTimelineContractTests(unittest.TestCase):
    def test_mock_orchestration_contract(self):
        result = subprocess.run(
            [str(node_runtime(ROOT)), "--test",
             str(ROOT / "src/slippi_profile/tests/input_timeline_contract_test.mjs")],
            cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
